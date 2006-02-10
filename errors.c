/* ------------------------------------------------------------------------- */
/*   "errors" : Warnings, errors and fatal errors                            */
/*              (with error throwback code for RISC OS machines)             */
/*                                                                           */
/*   Part of Inform 6.31                                                     */
/*   copyright (c) Graham Nelson 1993 - 2006                                 */
/*                                                                           */
/* ------------------------------------------------------------------------- */

#include "header.h"

static char error_message_buff[256];

/* ------------------------------------------------------------------------- */
/*   Error preamble printing.                                                */
/* ------------------------------------------------------------------------- */

ErrorPosition ErrorReport;             /*  Maintained by "lexer.c"           */

static void print_preamble(void)
{
    /*  Only really prints the preamble to an error or warning message:

        e.g.  "jigsaw.apollo", line 24:

        The format is controllable (from an ICL switch) since this assists
        the working of some development environments.                        */

    int j, with_extension_flag = FALSE; char *p;

    j = ErrorReport.file_number;
    if (j <= 0) p = ErrorReport.source;
    else p = InputFiles[j-1].filename;

    if (!p) p = ""; /* ###-call me paranoid */
    
    switch(error_format)
    {
        case 0:  /* RISC OS error message format */

            if (!(ErrorReport.main_flag)) printf("\"%s\", ", p);
            printf("line %d: ", ErrorReport.line_number);
            break;

        case 1:  /* Microsoft error message format */

            for (j=0; p[j]!=0; j++)
            {   if (p[j] == FN_SEP) with_extension_flag = TRUE;
                if (p[j] == '.') with_extension_flag = FALSE;
            }
            printf("%s", p);
            if (with_extension_flag) printf("%s", Source_Extension);
            printf("(%d): ", ErrorReport.line_number);
            break;

        case 2:  /* Macintosh Programmer's Workshop error message format */

            printf("File \"%s\"; Line %d\t# ", p, ErrorReport.line_number);
            break;
    }
}

static char trimmed_text[128];

static void trim_text(char *s)
{   int i;
    if (strlen(s) < 128) { strcpy(trimmed_text, s); return; }
    for (i=0; i<120; i++) trimmed_text[i] = s[i];
    trimmed_text[i++] = '.';
    trimmed_text[i++] = '.';
    trimmed_text[i++] = '.';
    trimmed_text[i++] = 0;
    return;
}

/* ------------------------------------------------------------------------- */
/*   Fatal errors (which have style 0)                                       */
/* ------------------------------------------------------------------------- */

extern void fatalerror(char *s)
{   print_preamble();

    printf("Fatal error: %s\n",s);
    if (no_compiler_errors > 0) print_sorry_message();

#ifdef ARC_THROWBACK
    throwback(0, s);
    throwback_end();
#endif
#ifdef MAC_FACE
    close_all_source();
    if (temporary_files_switch) remove_temp_files();
    abort_transcript_file();
    free_arrays();
    if (store_the_text)
        my_free(&all_text,"transcription text");
    longjmp(g_fallback, 1);
#endif
    exit(1);
}

extern void fatalerror_named(char *m, char *fn)
{   trim_text(fn);
    sprintf(error_message_buff, "%s \"%s\"", m, trimmed_text);
    fatalerror(error_message_buff);
}

extern void memory_out_error(int32 size, int32 howmany, char *name)
{   if (howmany == 1)
        sprintf(error_message_buff,
            "Run out of memory allocating %d bytes for %s", size, name);
    else
        sprintf(error_message_buff,
            "Run out of memory allocating array of %dx%d bytes for %s",
                howmany, size, name);
    fatalerror(error_message_buff);
}

extern void memoryerror(char *s, int32 size)
{
    sprintf(error_message_buff,
        "The memory setting %s (which is %ld at present) has been \
exceeded.  Try running Inform again with $%s=<some-larger-number> on the \
command line.",s,(long int) size,s);
    fatalerror(error_message_buff);
}

/* ------------------------------------------------------------------------- */
/*   Survivable diagnostics:                                                 */
/*      compilation errors   style 1                                         */
/*      warnings             style 2                                         */
/*      linkage errors       style 3                                         */
/*      compiler errors      style 4 (these should never happen and          */
/*                                    indicate a bug in Inform)              */
/* ------------------------------------------------------------------------- */

static int errors[MAX_ERRORS];

int no_errors, no_warnings, no_suppressed_warnings, no_link_errors,
    no_compiler_errors;

char *forerrors_buff;
int  forerrors_pointer;

static void message(int style, char *s)
{   int throw_style = style;
    if (hash_printed_since_newline) printf("\n");
    hash_printed_since_newline = FALSE;
    print_preamble();
    switch(style)
    {   case 1: printf("Error: "); no_errors++; break;
        case 2: printf("Warning: "); no_warnings++; break;
        case 3: printf("Error:  [linking '%s']  ", current_module_filename);
                no_link_errors++; no_errors++; throw_style=1; break;
        case 4: printf("*** Compiler error: ");
                no_compiler_errors++; throw_style=1; break;
    }
    printf(" %s\n", s);
#ifdef ARC_THROWBACK
    throwback(throw_style, s);
#endif
#ifdef MAC_FACE
    ProcessEvents (&g_proc);
    if (g_proc != true)
    {   free_arrays();
        if (store_the_text)
            my_free(&all_text,"transcription text");
        close_all_source ();
        if (temporary_files_switch) remove_temp_files();
        abort_transcript_file();
        longjmp (g_fallback, 1);
    }
#endif
    if ((!concise_switch) && (forerrors_pointer > 0) && (style <= 2))
    {   forerrors_buff[forerrors_pointer] = 0;
        sprintf(forerrors_buff+68,"  ...etc");
        printf("> %s\n",forerrors_buff);
    }
}

/* ------------------------------------------------------------------------- */
/*   Style 1: Error message routines                                         */
/* ------------------------------------------------------------------------- */

extern void error(char *s)
{   if (no_errors == MAX_ERRORS)
        fatalerror("Too many errors: giving up");
    errors[no_errors] = no_syntax_lines;
    message(1,s);
}

extern void error_named(char *s1, char *s2)
{   trim_text(s2);
    sprintf(error_message_buff,"%s \"%s\"",s1,trimmed_text);
    error(error_message_buff);
}

extern void error_numbered(char *s1, int val)
{
    sprintf(error_message_buff,"%s %d.",s1,val);
    error(error_message_buff);
}

extern void error_named_at(char *s1, char *s2, int32 report_line)
{   int i;

    ErrorPosition E = ErrorReport;
    if (report_line != -1)
    {   ErrorReport.file_number = report_line/0x10000;
        ErrorReport.line_number = report_line%0x10000;
        ErrorReport.main_flag = (ErrorReport.file_number == 1);
    }

    trim_text(s2);
    sprintf(error_message_buff,"%s \"%s\"",s1,trimmed_text);

    i = concise_switch; concise_switch = TRUE;
    error(error_message_buff);
    ErrorReport = E; concise_switch = i;
}

extern void no_such_label(char *lname)
{   error_named("No such label as",lname);
}

extern void ebf_error(char *s1, char *s2)
{   trim_text(s2);
    sprintf(error_message_buff, "Expected %s but found %s", s1, trimmed_text);
    error(error_message_buff);
}

extern void char_error(char *s, int ch)
{   int32 uni;

    uni = iso_to_unicode(ch);

    if (uni >= 0x100)
    {   sprintf(error_message_buff,
            "%s (unicode) $%04x = (ISO %s) $%02x", s, uni,
            name_of_iso_set(character_set_setting), ch);
    }
    else
        sprintf(error_message_buff, "%s (ISO Latin1) $%02x", s, uni);

    if (((uni>=32) && (uni<127))
        || (((uni >= 0xa1) && (uni <= 0xff)) && (character_set_setting==1)))
        sprintf(error_message_buff+strlen(error_message_buff),
            ", i.e., '%c'", uni);

    error(error_message_buff);
}

extern void unicode_char_error(char *s, int32 uni)
{
    if (uni >= 0x100)
        sprintf(error_message_buff, "%s (unicode) $%04x", s, uni);
    else
        sprintf(error_message_buff, "%s (ISO Latin1) $%02x", s, uni);

    if (((uni>=32) && (uni<127))
        || (((uni >= 0xa1) && (uni <= 0xff)) && (character_set_setting==1)))
        sprintf(error_message_buff+strlen(error_message_buff),
            ", i.e., '%c'", uni);

    error(error_message_buff);
}

/* ------------------------------------------------------------------------- */
/*   Style 2: Warning message routines                                       */
/* ------------------------------------------------------------------------- */

extern void warning(char *s1)
{   if (nowarnings_switch) { no_suppressed_warnings++; return; }
    message(2,s1);
}

extern void warning_numbered(char *s1, int val)
{   if (nowarnings_switch) { no_suppressed_warnings++; return; }
    sprintf(error_message_buff,"%s %d.", s1, val);
    message(2,error_message_buff);
}

extern void warning_named(char *s1, char *s2)
{
    trim_text(s2);
    if (nowarnings_switch) { no_suppressed_warnings++; return; }
    sprintf(error_message_buff,"%s \"%s\"", s1, trimmed_text);
    message(2,error_message_buff);
}

extern void dbnu_warning(char *type, char *name, int32 report_line)
{   int i;
    ErrorPosition E = ErrorReport;
    if (nowarnings_switch) { no_suppressed_warnings++; return; }
    if (report_line != -1)
    {   ErrorReport.file_number = report_line/0x10000;
        ErrorReport.line_number = report_line%0x10000;
        ErrorReport.main_flag = (ErrorReport.file_number == 1);
    }
    sprintf(error_message_buff, "%s \"%s\" declared but not used", type, name);
    i = concise_switch; concise_switch = TRUE;
    message(2,error_message_buff);
    concise_switch = i;
    ErrorReport = E;
}

extern void obsolete_warning(char *s1)
{   if (is_systemfile()==1) return;
    if (obsolete_switch || nowarnings_switch)
    {   no_suppressed_warnings++; return; }
    sprintf(error_message_buff, "Obsolete usage: %s",s1);
    message(2,error_message_buff);
}

/* ------------------------------------------------------------------------- */
/*   Style 3: Link error message routines                                    */
/* ------------------------------------------------------------------------- */

extern void link_error(char *s)
{   if (no_errors==MAX_ERRORS) fatalerror("Too many errors: giving up");
    errors[no_errors] = no_syntax_lines;
    message(3,s);
}

extern void link_error_named(char *s1, char *s2)
{   trim_text(s2);
    sprintf(error_message_buff,"%s \"%s\"",s1,trimmed_text);
    link_error(error_message_buff);
}

/* ------------------------------------------------------------------------- */
/*   Style 4: Compiler error message routines                                */
/* ------------------------------------------------------------------------- */

extern void print_sorry_message(void)
{   printf(
"***********************************************************************\n\
* 'Compiler errors' should never occur if Inform is working properly. *\n\
* This is version %d.%02d of Inform, dated %20s: so      *\n\
* if that was more than six months ago, there may be a more recent    *\n\
* version available, from which the problem may have been removed.    *\n\
* If not, please report this fault to:   graham@gnelson.demon.co.uk   *\n\
* and if at all possible, please include your source code, as faults  *\n\
* such as these are rare and often difficult to reproduce.  Sorry.    *\n\
***********************************************************************\n",
    (RELEASE_NUMBER/100)%10, RELEASE_NUMBER%100, RELEASE_DATE);
}

extern int compiler_error(char *s)
{   if (no_link_errors > 0) return FALSE;
    if (no_errors > 0) return FALSE;
    if (no_compiler_errors==MAX_ERRORS)
        fatalerror("Too many compiler errors: giving up");
    message(4,s);
    return TRUE;
}

extern int compiler_error_named(char *s1, char *s2)
{   if (no_link_errors > 0) return FALSE;
    if (no_errors > 0) return FALSE;
    trim_text(s2);
    sprintf(error_message_buff,"%s \"%s\"",s1,trimmed_text);
    compiler_error(error_message_buff);
    return TRUE;
}

/* ------------------------------------------------------------------------- */
/*   Code for the Acorn RISC OS operating system, donated by Robin Watts,    */
/*   to provide error throwback under the DDE environment                    */
/* ------------------------------------------------------------------------- */

#ifdef ARC_THROWBACK

#define DDEUtils_ThrowbackStart 0x42587
#define DDEUtils_ThrowbackSend  0x42588
#define DDEUtils_ThrowbackEnd   0x42589

#include "kernel.h"

extern void throwback_start(void)
{    _kernel_swi_regs regs;
     if (throwback_switch)
         _kernel_swi(DDEUtils_ThrowbackStart, &regs, &regs);
}

extern void throwback_end(void)
{   _kernel_swi_regs regs;
    if (throwback_switch)
        _kernel_swi(DDEUtils_ThrowbackEnd, &regs, &regs);
}

int throwback_started = FALSE;

extern void throwback(int severity, char * error)
{   _kernel_swi_regs regs;
    if (!throwback_started)
    {   throwback_started = TRUE;
        throwback_start();
    }
    if (throwback_switch)
    {   regs.r[0] = 1;
        if ((ErrorReport.file_number == -1)
            || (ErrorReport.file_number == 0))
            regs.r[2] = (int) (InputFiles[0].filename);
        else regs.r[2] = (int) (InputFiles[ErrorReport.file_number-1].filename);
        regs.r[3] = ErrorReport.line_number;
        regs.r[4] = (2-severity);
        regs.r[5] = (int) error;
       _kernel_swi(DDEUtils_ThrowbackSend, &regs, &regs);
    }
}

#endif

/* ========================================================================= */
/*   Data structure management routines                                      */
/* ------------------------------------------------------------------------- */

extern void init_errors_vars(void)
{   forerrors_buff = NULL;
    no_errors = 0; no_warnings = 0; no_suppressed_warnings = 0;
    no_compiler_errors = 0;
}

extern void errors_begin_pass(void)
{   ErrorReport.line_number = 0;
    ErrorReport.file_number = -1;
    ErrorReport.source = "<no text read yet>";
    ErrorReport.main_flag = FALSE;
}

extern void errors_allocate_arrays(void)
{   forerrors_buff = my_malloc(512, "errors buffer");
}

extern void errors_free_arrays(void)
{   my_free(&forerrors_buff, "errors buffer");
}

/* ========================================================================= */

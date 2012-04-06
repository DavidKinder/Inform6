/* ------------------------------------------------------------------------- */
/*   "directs" : Directives (# commands)                                     */
/*                                                                           */
/*   Part of Inform 6.32                                                     */
/*   copyright (c) Graham Nelson 1993 - 2012                                 */
/*                                                                           */
/* ------------------------------------------------------------------------- */

#include "header.h"

int no_routines,                   /* Number of routines compiled so far     */
    no_named_routines,             /* Number not embedded in objects         */
    no_locals,                     /* Number of locals in current routine    */
    no_termcs;                     /* Number of terminating characters       */
int terminating_characters[32];

int32 routine_starts_line;         /* Source code line on which the current
                                      routine starts.  (Useful for reporting
                                      "unused variable" warnings on the start
                                      line rather than the end line.)        */

static int constant_made_yet;      /* Have any constants been defined yet?   */

static int ifdef_stack[32], ifdef_sp;

/* ------------------------------------------------------------------------- */

extern int parse_given_directive(void)
{   int *trace_level; int32 i, j, k, n, flag;

    switch(token_value)
    {

    /* --------------------------------------------------------------------- */
    /*   Abbreviate "string1" ["string2" ...]                                */
    /* --------------------------------------------------------------------- */

    case ABBREVIATE_CODE:

        do
        {  get_next_token();
           if ((token_type == SEP_TT) && (token_value == SEMICOLON_SEP))
               return FALSE;

           /* Glulx doesn't have a 64-abbrev limit */
           if (!glulx_mode && MAX_ABBREVS==64)
           {   if (no_abbreviations==64)
                   error("All 64 abbreviations already declared");
           }
           else
           {   if (no_abbreviations==MAX_ABBREVS)
                   memoryerror("MAX_ABBREVS", MAX_ABBREVS);
           }

           if (abbrevs_lookup_table_made)
           {   error("All abbreviations must be declared together");
               break;
           }
           if (token_type != DQ_TT)
               ebf_error("abbreviation string", token_text);
           else
           if (strlen(token_text)<2)
           {   error_named("It's not worth abbreviating", token_text);
               break;
           }
           make_abbreviation(token_text);
        } while (TRUE);

    /* --------------------------------------------------------------------- */
    /*   Array arrayname array...                                            */
    /* --------------------------------------------------------------------- */

    case ARRAY_CODE: make_global(TRUE, FALSE); break;      /* See "tables.c" */

    /* --------------------------------------------------------------------- */
    /*   Attribute newname [alias oldname]                                   */
    /* --------------------------------------------------------------------- */

    case ATTRIBUTE_CODE:
        make_attribute(); break;                          /* See "objects.c" */

    /* --------------------------------------------------------------------- */
    /*   Class classname ...                                                 */
    /* --------------------------------------------------------------------- */

    case CLASS_CODE: make_class(NULL); return FALSE;       /* See "tables.c" */

    /* --------------------------------------------------------------------- */
    /*   Constant newname [[=] value] [, ...]                                */
    /* --------------------------------------------------------------------- */

    case CONSTANT_CODE:
        constant_made_yet=TRUE;

      ParseConstantSpec:
        get_next_token(); i = token_value;

        if ((token_type != SYMBOL_TT)
            || (!(sflags[i] & (UNKNOWN_SFLAG + REDEFINABLE_SFLAG))))
        {   ebf_error("new constant name", token_text);
            panic_mode_error_recovery(); break;
        }

        assign_symbol(i, 0, CONSTANT_T);

        get_next_token();

        if ((token_type == SEP_TT) && (token_value == COMMA_SEP))
            goto ParseConstantSpec;

        if ((token_type == SEP_TT) && (token_value == SEMICOLON_SEP))
            return FALSE;

        if (!((token_type == SEP_TT) && (token_value == SETEQUALS_SEP)))
            put_token_back();

        {   assembly_operand AO = parse_expression(CONSTANT_CONTEXT);
            if (AO.marker != 0)
            {   assign_marked_symbol(i, AO.marker, AO.value,
                    CONSTANT_T);
                sflags[i] |= CHANGE_SFLAG;
                if (i == grammar_version_symbol)
                    error(
                "Grammar__Version must be given an explicit constant value");
            }
            else
            {   assign_symbol(i, AO.value, CONSTANT_T);
                if (i == grammar_version_symbol)
                {   if ((grammar_version_number != AO.value)
                        && (no_fake_actions > 0))
                        error(
                "Once a fake action has been defined it is too late to \
change the grammar version. (If you are using the library, move any \
Fake_Action directives to a point after the inclusion of \"Parser\".)");
                    grammar_version_number = AO.value;
                }
            }
        }
        get_next_token();
        if ((token_type == SEP_TT) && (token_value == COMMA_SEP))
            goto ParseConstantSpec;
        put_token_back();
        break;

    /* --------------------------------------------------------------------- */
    /*   Default constantname integer                                        */
    /* --------------------------------------------------------------------- */

    case DEFAULT_CODE:
        if (module_switch)
        {   error("'Default' cannot be used in -M (Module) mode");
            break;
        }

        get_next_token();
        if (token_type != SYMBOL_TT)
        {   ebf_error("name", token_text);
            panic_mode_error_recovery(); break;
        }

        i = -1;
        if (sflags[token_value] & UNKNOWN_SFLAG)
        {   i = token_value;
            sflags[i] |= DEFCON_SFLAG;
        }

        get_next_token();
        if (!((token_type == SEP_TT) && (token_value == SETEQUALS_SEP)))
            put_token_back();

        {   assembly_operand AO;
            AO = parse_expression(CONSTANT_CONTEXT);
            if (i != -1)
            {   if (AO.marker != 0)
                {   assign_marked_symbol(i, AO.marker, AO.value,
                        CONSTANT_T);
                    sflags[i] |= CHANGE_SFLAG;
                }
                else assign_symbol(i, AO.value, CONSTANT_T);
            }
        }

        break;

    /* --------------------------------------------------------------------- */
    /*   Dictionary 'word'                                                   */
    /*   Dictionary 'word' val1                                              */
    /*   Dictionary 'word' val1 val3                                         */
    /* --------------------------------------------------------------------- */

    case DICTIONARY_CODE:
        /* In Inform 5, this directive had the form
             Dictionary SYMBOL "word";
           This was deprecated as of I6 (if not earlier), and is no longer
           supported at all. The current form just creates a dictionary word,
           with the given values for dict_par1 and dict_par3. If the word
           already exists, the values are bit-or'd in with the existing
           values.
           (We don't offer a way to set dict_par2, because that is entirely
           reserved for the verb number. Or'ing values into it would create
           garbage.)
         */
        get_next_token();
        if (token_type != SQ_TT && token_type != DQ_TT) {
            ebf_error("dictionary word", token_text);
            break;
        }

        {
            char *wd = token_text;
            int val1 = 0;
            int val3 = 0;

            get_next_token();
            if ((token_type == SEP_TT) && (token_value == SEMICOLON_SEP)) {
                put_token_back();
            }
            else {
                put_token_back();
                assembly_operand AO;
                AO = parse_expression(CONSTANT_CONTEXT);
                if (module_switch && (AO.marker != 0))
                    error("A definite value must be given as a Dictionary flag");
                else
                    val1 = AO.value;

                get_next_token();
                if ((token_type == SEP_TT) && (token_value == SEMICOLON_SEP)) {
                    put_token_back();
                }
                else {
                    put_token_back();
                    assembly_operand AO;
                    AO = parse_expression(CONSTANT_CONTEXT);
                    if (module_switch && (AO.marker != 0))
                        error("A definite value must be given as a Dictionary flag");
                    else
                        val3 = AO.value;
                }
            }

            if (!glulx_mode) {
                if ((val1 & ~0xFF) || (val3 & ~0xFF)) {
                    warning("Dictionary flag values cannot exceed $FF in Z-code");
                }
            }
            else {
                if ((val1 & ~0xFFFF) || (val3 & ~0xFFFF)) {
                    warning("Dictionary flag values cannot exceed $FFFF in Glulx");
                }
            }

            dictionary_add(wd, val1, 0, val3);
        }
        break;

    /* --------------------------------------------------------------------- */
    /*   End                                                                 */
    /* --------------------------------------------------------------------- */

    case END_CODE: return(TRUE);

    case ENDIF_CODE:
        if (ifdef_sp == 0) error("'Endif' without matching 'If...'");
        else ifdef_sp--;
        break;

    /* --------------------------------------------------------------------- */
    /*   Extend ...                                                          */
    /* --------------------------------------------------------------------- */

    case EXTEND_CODE: extend_verb(); return FALSE;         /* see "tables.c" */

    /* --------------------------------------------------------------------- */
    /*   Fake_Action name                                                    */
    /* --------------------------------------------------------------------- */

    case FAKE_ACTION_CODE:
        make_fake_action(); break;                         /* see "tables.c" */

    /* --------------------------------------------------------------------- */
    /*   Global variable [= value / array...]                                */
    /* --------------------------------------------------------------------- */

    case GLOBAL_CODE: make_global(FALSE, FALSE); break;    /* See "tables.c" */

    /* --------------------------------------------------------------------- */
    /*   If...                                                               */
    /*                                                                       */
    /*   Note that each time Inform tests an If... condition, it stacks the  */
    /*   result (TRUE or FALSE) on ifdef_stack: thus, the top of this stack  */
    /*   reveals what clause of the current If... is being compiled:         */
    /*                                                                       */
    /*               If...;  ...  Ifnot;  ...  Endif;                        */
    /*   top of stack:       TRUE        FALSE                               */
    /*                                                                       */
    /*   This is used to detect "two Ifnots in same If" errors.              */
    /* --------------------------------------------------------------------- */

    case IFDEF_CODE:
        flag = TRUE;
        goto DefCondition;
    case IFNDEF_CODE:
        flag = FALSE;

      DefCondition:
        get_next_token();
        if (token_type != SYMBOL_TT)
        {   ebf_error("symbol name", token_text);
            break;
        }

        if ((token_text[0] == 'V')
            && (token_text[1] == 'N')
            && (token_text[2] == '_')
            && (strlen(token_text)==7))
        {   i = atoi(token_text+3);
            if (VNUMBER < i) flag = (flag)?FALSE:TRUE;
            goto HashIfCondition;
        }

        if (sflags[token_value] & UNKNOWN_SFLAG) flag = (flag)?FALSE:TRUE;
        else sflags[token_value] |= USED_SFLAG;
        goto HashIfCondition;

    case IFNOT_CODE:
        if (ifdef_sp == 0)
            error("'Ifnot' without matching 'If...'");
        else
        if (!(ifdef_stack[ifdef_sp-1]))
            error("Second 'Ifnot' for the same 'If...' condition");
        else
        {   dont_enter_into_symbol_table = -2; n = 1;
            directives.enabled = TRUE;
            do
            {   get_next_token();
                if (token_type == EOF_TT)
                {   error("End of file reached in code 'If...'d out");
                    directives.enabled = FALSE;
                    return TRUE;
                }
                if (token_type == DIRECTIVE_TT)
                {   switch(token_value)
                    {   case ENDIF_CODE:
                            n--; break;
                        case IFV3_CODE:
                        case IFV5_CODE:
                        case IFDEF_CODE:
                        case IFNDEF_CODE:
                        case IFTRUE_CODE:
                        case IFFALSE_CODE:
                            n++; break;
                        case IFNOT_CODE:
                            if (n == 1)
                            {   error(
                              "Second 'Ifnot' for the same 'If...' condition");
                                break;
                            }
                    }
                }
            } while (n > 0);
            ifdef_sp--; 
            dont_enter_into_symbol_table = FALSE;
            directives.enabled = FALSE;
        }
        break;

    case IFV3_CODE:
        flag = FALSE; if (version_number == 3) flag = TRUE;
        goto HashIfCondition;

    case IFV5_CODE:
        flag = TRUE; if (version_number == 3) flag = FALSE;
        goto HashIfCondition;

    case IFTRUE_CODE:
        {   assembly_operand AO;
            AO = parse_expression(CONSTANT_CONTEXT);
            if (module_switch && (AO.marker != 0))
            {   error("This condition can't be determined");
                flag = 0;
            }
            else flag = (AO.value != 0);
        }
        goto HashIfCondition;

    case IFFALSE_CODE:
        {   assembly_operand AO;
            AO = parse_expression(CONSTANT_CONTEXT);
            if (module_switch && (AO.marker != 0))
            {   error("This condition can't be determined");
                flag = 1;
            }
            else flag = (AO.value == 0);
        }
        goto HashIfCondition;

    HashIfCondition:
        get_next_token();
        if (!((token_type == SEP_TT) && (token_value == SEMICOLON_SEP)))
        {   ebf_error("semicolon after 'If...' condition", token_text);
            break;
        }

        if (flag)
        {   ifdef_stack[ifdef_sp++] = TRUE; return FALSE; }
        else
        {   dont_enter_into_symbol_table = -2; n = 1;
            directives.enabled = TRUE;
            do
            {   get_next_token();
                if (token_type == EOF_TT)
                {   error("End of file reached in code 'If...'d out");
                    directives.enabled = FALSE;
                    return TRUE;
                }
                if (token_type == DIRECTIVE_TT)
                {
                    switch(token_value)
                    {   case ENDIF_CODE:
                            n--; break;
                        case IFV3_CODE:
                        case IFV5_CODE:
                        case IFDEF_CODE:
                        case IFNDEF_CODE:
                        case IFTRUE_CODE:
                        case IFFALSE_CODE:
                            n++; break;
                        case IFNOT_CODE:
                            if (n == 1)
                            {   ifdef_stack[ifdef_sp++] = FALSE;
                                n--; break;
                            }
                    }
                }
            } while (n > 0);
            directives.enabled = FALSE;
            dont_enter_into_symbol_table = FALSE;
        }
        break;

    /* --------------------------------------------------------------------- */
    /*   Import global <varname> [, ...]                                     */
    /*                                                                       */
    /* (Further imported goods may be allowed later.)                        */
    /* --------------------------------------------------------------------- */

    case IMPORT_CODE:
        if (!module_switch)
        {   error("'Import' can only be used in -M (Module) mode");
            break;
        }
        directives.enabled = TRUE;
        do
        {   get_next_token();
            if ((token_type == DIRECTIVE_TT) && (token_value == GLOBAL_CODE))
                 make_global(FALSE, TRUE);
            else error_named("'Import' cannot import things of this type:",
                 token_text);
            get_next_token();
        } while ((token_type == SEP_TT) && (token_value == COMMA_SEP));
        put_token_back();
        directives.enabled = FALSE;
        break;

    /* --------------------------------------------------------------------- */
    /*   Include "[>]filename"                                               */
    /* --------------------------------------------------------------------- */

    case INCLUDE_CODE:
        get_next_token();
        if (token_type != DQ_TT)
        {   ebf_error("filename in double-quotes", token_text);
            panic_mode_error_recovery(); return FALSE;
        }

        {   char *name = token_text;

            get_next_token();
            if (!((token_type == SEP_TT) && (token_value == SEMICOLON_SEP)))
                ebf_error("semicolon ';' after Include filename", token_text);

            if (strcmp(name, "language__") == 0)
                 load_sourcefile(Language_Name, 0);
            else if (name[0] == '>')
                 load_sourcefile(name+1, 1);
            else load_sourcefile(name, 0);
            return FALSE;
        }

    /* --------------------------------------------------------------------- */
    /*   Link "filename"                                                     */
    /* --------------------------------------------------------------------- */

    case LINK_CODE:
        get_next_token();
        if (token_type != DQ_TT)
        {   ebf_error("filename in double-quotes", token_text);
            panic_mode_error_recovery(); return FALSE;
        }
        link_module(token_text);                           /* See "linker.c" */
        break;

    /* --------------------------------------------------------------------- */
    /*   Lowstring constantname "text of string"                             */
    /* --------------------------------------------------------------------- */
    /* Unlike most constant creations, these do not require backpatching:    */
    /* the low strings always occupy a table at a fixed offset in the        */
    /* Z-machine (after the abbreviations table has finished, at 0x100).     */
    /* --------------------------------------------------------------------- */

    case LOWSTRING_CODE:
        if (module_switch)
        {   error("'LowString' cannot be used in -M (Module) mode"); break;
        }
        get_next_token(); i = token_value;
        if ((token_type != SYMBOL_TT) || (!(sflags[i] & UNKNOWN_SFLAG)))
        {   ebf_error("new low string name", token_text);
            panic_mode_error_recovery(); return FALSE;
        }

        get_next_token();
        if (token_type != DQ_TT)
        {   ebf_error("literal string in double-quotes", token_text);
            panic_mode_error_recovery(); return FALSE;
        }

        assign_symbol(i, compile_string(token_text, TRUE, TRUE), CONSTANT_T);
        break;

    /* --------------------------------------------------------------------- */
    /*   Message | "information"                                             */
    /*           | error "error message"                                     */
    /*           | fatalerror "fatal error message"                          */
    /*           | warning "warning message"                                 */
    /* --------------------------------------------------------------------- */

    case MESSAGE_CODE:
        directive_keywords.enabled = TRUE;
        get_next_token();
        directive_keywords.enabled = FALSE;
        if (token_type == DQ_TT)
        {   int i;
            if (hash_printed_since_newline) printf("\n");
            for (i=0; token_text[i]!=0; i++)
            {   if (token_text[i] == '^') printf("\n");
                else
                if (token_text[i] == '~') printf("\"");
                else printf("%c", token_text[i]);
            }
            printf("\n");
            break;
        }
        if ((token_type == DIR_KEYWORD_TT) && (token_value == ERROR_DK))
        {   get_next_token();
            if (token_type != DQ_TT)
            {   ebf_error("error message in double-quotes", token_text);
            }
            error(token_text); break;
        }
        if ((token_type == DIR_KEYWORD_TT) && (token_value == FATALERROR_DK))
        {   get_next_token();
            if (token_type != DQ_TT)
            {   ebf_error("fatal error message in double-quotes", token_text);
            }
            fatalerror(token_text); break;
        }
        if ((token_type == DIR_KEYWORD_TT) && (token_value == WARNING_DK))
        {   get_next_token();
            if (token_type != DQ_TT)
            {   ebf_error("warning message in double-quotes", token_text);
            }
            warning(token_text); break;
        }
        ebf_error("a message in double-quotes, 'error', 'fatalerror' or 'warning'",
            token_text);
        break;

    /* --------------------------------------------------------------------- */
    /*   Nearby objname "short name" ...                                     */
    /* --------------------------------------------------------------------- */

    case NEARBY_CODE: make_object(TRUE, NULL, -1, -1, -1);
        return FALSE;                                     /* See "objects.c" */

    /* --------------------------------------------------------------------- */
    /*   Object objname "short name" ...                                     */
    /* --------------------------------------------------------------------- */

    case OBJECT_CODE: make_object(FALSE, NULL, -1, -1, -1);
        return FALSE;                                     /* See "objects.c" */

    /* --------------------------------------------------------------------- */
    /*   Property [long] [additive] name [alias oldname]                     */
    /* --------------------------------------------------------------------- */

    case PROPERTY_CODE: make_property(); break;           /* See "objects.c" */

    /* --------------------------------------------------------------------- */
    /*   Release <number>                                                    */
    /* --------------------------------------------------------------------- */

    case RELEASE_CODE:
        {   assembly_operand AO;
            AO = parse_expression(CONSTANT_CONTEXT);
            if (module_switch && (AO.marker != 0))
                error("A definite value must be given as release number");
            else
                release_number = AO.value;
        }
        break;

    /* --------------------------------------------------------------------- */
    /*   Replace routine                                                     */
    /* --------------------------------------------------------------------- */

    case REPLACE_CODE:
        /* You can also replace system functions normally implemented in     */
        /* the "hardware" of the Z-machine, like "random()":                 */

        system_functions.enabled = TRUE;
        directives.enabled = FALSE;
        directive_keywords.enabled = FALSE;

        get_next_token();
        if (token_type == SYSFUN_TT)
        {   if (system_function_usage[token_value] == 1)
                error("You can't 'Replace' a system function already used");
            else system_function_usage[token_value] = 2;
            break;
        }

        if (token_type != SYMBOL_TT)
        {   ebf_error("name of routine to replace", token_text);
            break;
        }

        if (!(sflags[token_value] & UNKNOWN_SFLAG))
        {   ebf_error("name of routine not yet defined", token_text);
            break;
        }
        sflags[token_value] |= REPLACE_SFLAG;

        break;

    /* --------------------------------------------------------------------- */
    /*   Serial "yymmdd"                                                     */
    /* --------------------------------------------------------------------- */

    case SERIAL_CODE:
        get_next_token();
        if ((token_type != DQ_TT) || (strlen(token_text)!=6))
        {   error("The serial number must be a 6-digit date in double-quotes");
            break;
        }
        for (i=0; i<6; i++) if (isdigit(token_text[i])==0)
        {   error("The serial number must be a 6-digit date in double-quotes");
            break;
        }
        strcpy(serial_code_buffer, token_text);
        serial_code_given_in_program = TRUE;
        break;

    /* --------------------------------------------------------------------- */
    /*   Statusline score/time                                               */
    /* --------------------------------------------------------------------- */

    case STATUSLINE_CODE:
        if (module_switch)
            warning("This does not set the final game's statusline");

        directive_keywords.enabled = TRUE;
        get_next_token();
        directive_keywords.enabled = FALSE;
        if ((token_type != DIR_KEYWORD_TT)
            || ((token_value != SCORE_DK) && (token_value != TIME_DK)))
        {   ebf_error("'score' or 'time' after 'statusline'", token_text);
            break;
        }
        if (token_value == SCORE_DK) statusline_flag = SCORE_STYLE;
        else statusline_flag = TIME_STYLE;
        break;

    /* --------------------------------------------------------------------- */
    /*   Stub routinename number-of-locals                                   */
    /* --------------------------------------------------------------------- */

    case STUB_CODE:

        get_next_token();
        if (token_type != SYMBOL_TT)
        {   ebf_error("routine name to stub", token_text);
            panic_mode_error_recovery(); return FALSE;
        }

        i = token_value; flag = FALSE;

        if (sflags[i] & UNKNOWN_SFLAG)
        {   sflags[i] |= STUB_SFLAG;
            flag = TRUE;
        }

        get_next_token(); k = token_value;
        if (token_type != NUMBER_TT)
        {   ebf_error("number of local variables", token_text);
            k = 0;
        }
        if ((k>4) || (k<0))
        {   error("Must specify 0 to 4 local variables for 'Stub' routine");
            k = 0;
        }

        if (flag)
        {
            /*  Give these parameter-receiving local variables names
                for the benefit of the debugging information file,
                and for assembly tracing to look sensible.                   */

            local_variable_texts[0] = "dummy1";
            local_variable_texts[1] = "dummy2";
            local_variable_texts[2] = "dummy3";
            local_variable_texts[3] = "dummy4";

            assign_symbol(i,
                assemble_routine_header(k, FALSE, (char *) symbs[i],
                    &token_line_ref, FALSE, i),
                ROUTINE_T);

            /*  Ensure the return value of a stubbed routine is false,
                since this is necessary to make the library work properly    */

            if (!glulx_mode)
                assemblez_0(rfalse_zc);
            else
                assembleg_1(return_gc, zero_operand);

            /*  Inhibit "local variable unused" warnings  */

            for (i=1; i<=k; i++) variable_usage[i] = 1;
            sequence_point_follows = FALSE;
            assemble_routine_end(FALSE, &token_line_ref);
        }
        break;

    /* --------------------------------------------------------------------- */
    /*   Switches switchblock                                                */
    /* (this directive is ignored if the -i switch was set at command line)  */
    /* --------------------------------------------------------------------- */

    case SWITCHES_CODE:
        dont_enter_into_symbol_table = TRUE;
        get_next_token();
        dont_enter_into_symbol_table = FALSE;
        if (token_type != DQ_TT)
        {   ebf_error("string of switches", token_text);
            break;
        }
        if (!ignore_switches_switch)
        {   if (constant_made_yet)
                error("A 'Switches' directive must must come before \
the first constant definition");
            switches(token_text, 0);                       /* see "inform.c" */
        }
        break;

    /* --------------------------------------------------------------------- */
    /*   System_file                                                         */
    /*                                                                       */
    /* Some files are declared as "system files": this information is used   */
    /* by Inform only to skip the definition of a routine X if the designer  */
    /* has indicated his intention to Replace X.                             */
    /* --------------------------------------------------------------------- */

    case SYSTEM_CODE:
        declare_systemfile(); break;                        /* see "files.c" */

    /* --------------------------------------------------------------------- */
    /*   Trace dictionary                                                    */
    /*         objects                                                       */
    /*         symbols                                                       */
    /*         verbs                                                         */
    /*                      [on/off]                                         */
    /*         assembly     [on/off]                                         */
    /*         expressions  [on/off]                                         */
    /*         lines        [on/off]                                         */
    /* --------------------------------------------------------------------- */

    case TRACE_CODE:
        directives.enabled = FALSE;
        trace_keywords.enabled = TRUE;
        get_next_token();
        trace_keywords.enabled = FALSE;
        directives.enabled = TRUE;
        if ((token_type == SEP_TT) && (token_value == SEMICOLON_SEP))
        {   asm_trace_level = 1; return FALSE; }

        if (token_type != TRACE_KEYWORD_TT)
        {   ebf_error("debugging keyword", token_text);
            panic_mode_error_recovery(); return FALSE;
        }

        trace_keywords.enabled = TRUE;

        i = token_value; j = 0;
        switch(i)
        {   case DICTIONARY_TK: break;
            case OBJECTS_TK:    break;
            case VERBS_TK:      break;
            default:
                switch(token_value)
                {   case ASSEMBLY_TK:
                        trace_level = &asm_trace_level;  break;
                    case EXPRESSIONS_TK:
                        trace_level = &expr_trace_level; break;
                    case LINES_TK:
                        trace_level = &line_trace_level; break;
                    case TOKENS_TK:
                        trace_level = &tokens_trace_level; break;
                    case LINKER_TK:
                        trace_level = &linker_trace_level; break;
                    case SYMBOLS_TK:
                        trace_level = NULL; break;
                    default:
                        put_token_back();
                        trace_level = &asm_trace_level; break;
                }
                j = 1;
                get_next_token();
                if ((token_type == SEP_TT) &&
                    (token_value == SEMICOLON_SEP))
                {   put_token_back(); break;
                }
                if (token_type == NUMBER_TT)
                {   j = token_value; break; }
                if ((token_type == TRACE_KEYWORD_TT) && (token_value == ON_TK))
                {   j = 1; break; }
                if ((token_type == TRACE_KEYWORD_TT) && (token_value == OFF_TK))
                {   j = 0; break; }
                put_token_back(); break;
        }

        switch(i)
        {   case DICTIONARY_TK: show_dictionary();  break;
            case OBJECTS_TK:    list_object_tree(); break;
            case SYMBOLS_TK:    list_symbols(j);     break;
            case VERBS_TK:      list_verb_table();  break;
            default:
                *trace_level = j;
                break;
        }
        trace_keywords.enabled = FALSE;
        break;

    /* --------------------------------------------------------------------- */
    /*   Verb ...                                                            */
    /* --------------------------------------------------------------------- */

    case VERB_CODE: make_verb(); return FALSE;             /* see "tables.c" */

    /* --------------------------------------------------------------------- */
    /*   Version <number>                                                    */
    /* --------------------------------------------------------------------- */

    case VERSION_CODE:

        /* Ignore this if a version has already been set on the command line */
        if (version_set_switch) break;

        {   assembly_operand AO;
            AO = parse_expression(CONSTANT_CONTEXT);
            if (module_switch && (AO.marker != 0))
                error("A definite value must be given as version number");
            else 
            if (glulx_mode) 
            {
              warning("The Version directive does not work in Glulx. Use \
-vX.Y.Z instead, as either a command-line argument or a header comment.");
              break;
            }
            else
            {   i = AO.value;
                if ((i<3) || (i>8))
                {   error("The version number must be in the range 3 to 8");
                    break;
                }
                select_version(i);
            }
        }
        break;                                             /* see "inform.c" */

    /* --------------------------------------------------------------------- */
    /*   Zcharacter table <num> ...                                          */
    /*   Zcharacter table + <num> ...                                        */
    /*   Zcharacter <string> <string> <string>                               */
    /*   Zcharacter <char>                                                   */
    /* --------------------------------------------------------------------- */

    case ZCHARACTER_CODE:

        if (glulx_mode) {
            error("The Zcharacter directive has no meaning in Glulx.");
            return TRUE;
        }

        directive_keywords.enabled = TRUE;
        get_next_token();
        directive_keywords.enabled = FALSE;

        switch(token_type)
        {   case DQ_TT:
                new_alphabet(token_text, 0);
                get_next_token();
                if (token_type != DQ_TT)
                    ebf_error("double-quoted alphabet string", token_text);
                else new_alphabet(token_text, 1);
                get_next_token();
                if (token_type != DQ_TT)
                    ebf_error("double-quoted alphabet string", token_text);
                else new_alphabet(token_text, 2);
            break;

            case SQ_TT:
                map_new_zchar(text_to_unicode(token_text));
                if (token_text[textual_form_length] != 0)
                    ebf_error("single character value", token_text);
            break;

            case DIR_KEYWORD_TT:
            switch(token_value)
            {   case TABLE_DK:
                {   int plus_flag = FALSE;
                    get_next_token();
                    if ((token_type == SEP_TT) && (token_value == PLUS_SEP))
                    {   plus_flag = TRUE;
                        get_next_token();
                    }
                    while ((token_type!=SEP_TT) || (token_value!=SEMICOLON_SEP))
                    {   switch(token_type)
                        {   case NUMBER_TT:
                                new_zscii_character(token_value, plus_flag);
                                plus_flag = TRUE; break;
                            case SQ_TT:
                                new_zscii_character(text_to_unicode(token_text),
                                    plus_flag);
                                if (token_text[textual_form_length] != 0)
                                    ebf_error("single character value",
                                        token_text);
                                plus_flag = TRUE;
                                break;
                            default:
                                ebf_error("character or Unicode number",
                                    token_text); break;
                        }
                        get_next_token();
                    }
                    if (plus_flag) new_zscii_finished();
                    put_token_back();
                }
                    break;
                case TERMINATING_DK:
                    get_next_token();
                    while ((token_type!=SEP_TT) || (token_value!=SEMICOLON_SEP))
                    {   switch(token_type)
                        {   case NUMBER_TT:
                                terminating_characters[no_termcs++]
                                    = token_value;
                                break;
                            default:
                                ebf_error("ZSCII number", token_text); break;
                        }
                        get_next_token();
                    }
                    put_token_back();
                    break;
                default:
                    ebf_error("'table', 'terminating', a string or a constant",
                        token_text);
            }
                break;
            default:
                ebf_error("three alphabet strings, a 'table' or 'terminating' \
command or a single character", token_text);
            break;
        }
        break;

    /* ===================================================================== */

    }
    get_next_token();
    if ((token_type != SEP_TT) || (token_value != SEMICOLON_SEP))
    {   ebf_error("';'", token_text);
        put_token_back();
    }
    return FALSE;
}

/* ========================================================================= */
/*   Data structure management routines                                      */
/* ------------------------------------------------------------------------- */

extern void init_directs_vars(void)
{
}

extern void directs_begin_pass(void)
{   no_routines = 0;
    no_named_routines = 0;
    no_locals = 0;
    no_termcs = 0;
    constant_made_yet = FALSE;
    ifdef_sp = 0;
}

extern void directs_allocate_arrays(void)
{
}

extern void directs_free_arrays(void)
{
}

/* ========================================================================= */

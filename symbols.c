/* ------------------------------------------------------------------------- */
/*   "symbols" :  The symbols table; creating stock of reserved words        */
/*                                                                           */
/*   Part of Inform 6.32                                                     */
/*   copyright (c) Graham Nelson 1993 - 2012                                 */
/*                                                                           */
/* ------------------------------------------------------------------------- */

#include "header.h"

/* ------------------------------------------------------------------------- */
/*   This section of Inform is a service detached from the rest.             */
/*   Only two variables are accessible from the outside:                     */
/* ------------------------------------------------------------------------- */

int no_symbols;                        /* Total number of symbols defined    */
int no_named_constants;                         /* Copied into story file    */

/* ------------------------------------------------------------------------- */
/*   Plus four arrays.  Each symbol has its own index n (an int32) and       */
/*                                                                           */
/*       svals[n]   is its value (must be 32 bits wide, i.e. an int32, tho'  */
/*                  it is used to hold an unsigned 16 bit Z-machine value)   */
/*       sflags[n]  holds flags (see "header.h" for a list)                  */
/*       stypes[n]  is the "type", distinguishing between the data type of   */
/*                  different kinds of constants/variables.                  */
/*                  (See the "typename()" below.)                            */
/*       symbs[n]   (needs to be cast to (char *) to be used) is the name    */
/*                  of the symbol, in the same case form as when created.    */
/*       slines[n]  is the source line on which the symbol value was first   */
/*                  assigned                                                 */
/*                                                                           */
/*   Comparison is case insensitive.                                         */
/*   Note that local variable names are not entered into the symbols table,  */
/*   as their numbers and scope are too limited for this to be efficient.    */
/* ------------------------------------------------------------------------- */
/*   Caveat editor: some array types are set up to work even on machines     */
/*   where sizeof(int32 *) differs from, e.g., sizeof(char *): so do not     */
/*   alter the types unless you understand what is going on!                 */
/* ------------------------------------------------------------------------- */

  int32  **symbs;
  int32  *svals;
  int    *smarks;            /* Glulx-only */
  int32  *slines;
  int    *sflags;
#ifdef VAX
  char   *stypes;            /* In VAX C, insanely, "signed char" is illegal */
#else
  signed char *stypes;
#endif

/* ------------------------------------------------------------------------- */
/*   Memory to hold the text of symbol names: note that this memory is       */
/*   allocated as needed in chunks of size SYMBOLS_CHUNK_SIZE.               */
/* ------------------------------------------------------------------------- */

#define MAX_SYMBOL_CHUNKS (100)

static uchar *symbols_free_space,       /* Next byte free to hold new names  */
           *symbols_ceiling;            /* Pointer to the end of the current
                                           allocation of memory for names    */

static char** symbol_name_space_chunks; /* For chunks of memory used to hold
                                           the name strings of symbols       */
static int no_symbol_name_space_chunks;

/* ------------------------------------------------------------------------- */
/*   The symbols table is "hash-coded" into a disjoint union of linked       */
/*   lists, so that for any symbol i, next_entry[i] is either -1 (meaning    */
/*   that it's the last in its list) or the next in the list.                */
/*                                                                           */
/*   Each list contains, in alphabetical order, all the symbols which share  */
/*   the same "hash code" (a numerical function of the text of the symbol    */
/*   name, designed with the aim that roughly equal numbers of symbols are   */
/*   given each possible hash code).  The hash codes are 0 to HASH_TAB_SIZE  */
/*   (which is a memory setting) minus 1: start_of_list[h] gives the first   */
/*   symbol with hash code h, or -1 if no symbol exists with hash code h.    */
/*                                                                           */
/*   Note that the running time of the symbol search algorithm is about      */
/*                                                                           */
/*       O ( n^2 / HASH_TAB_SIZE )                                           */
/*                                                                           */
/*   (where n is the number of symbols in the program) so that it is a good  */
/*   idea to choose HASH_TAB_SIZE as large as conveniently possible.         */
/* ------------------------------------------------------------------------- */

static int   *next_entry;
static int32 *start_of_list;

/* ------------------------------------------------------------------------- */
/*   Initialisation.                                                         */
/* ------------------------------------------------------------------------- */

static void init_symbol_banks(void)
{   int i;
    for (i=0; i<HASH_TAB_SIZE; i++) start_of_list[i] = -1;
}

/* ------------------------------------------------------------------------- */
/*   The hash coding we use is quite standard; the variable hashcode is      */
/*   expected to overflow a good deal.  (The aim is to produce a number      */
/*   so that similar names do not produce the same number.)  Note that       */
/*   30011 is prime.  It doesn't matter if the unsigned int to int cast      */
/*   behaves differently on different ports.                                 */
/* ------------------------------------------------------------------------- */

int case_conversion_grid[128];

static void make_case_conversion_grid(void)
{
    /*  Assumes that A to Z are contiguous in the host OS character set:
        true for ASCII but not for EBCDIC, for instance.                     */

    int i;
    for (i=0; i<128; i++) case_conversion_grid[i] = i;
    for (i=0; i<26; i++) case_conversion_grid['A'+i]='a'+i;
}

extern int hash_code_from_string(char *p)
{   uint32 hashcode=0;
    for (; *p; p++) hashcode=hashcode*30011 + case_conversion_grid[(uchar)*p];
    return (int) (hashcode % HASH_TAB_SIZE);
}

extern int strcmpcis(char *p, char *q)
{
    /*  Case insensitive strcmp  */

    int i, j, pc, qc;
    for (i=0;p[i] != 0;i++)
    {   pc = p[i]; if (isupper(pc)) pc = tolower(pc);
        qc = q[i]; if (isupper(qc)) qc = tolower(qc);
        j = pc - qc;
        if (j!=0) return j;
    }
    qc = q[i]; if (isupper(qc)) qc = tolower(qc);
    return -qc;
}

/* ------------------------------------------------------------------------- */
/*   Symbol finding/creating.                                                */
/* ------------------------------------------------------------------------- */

extern int symbol_index(char *p, int hashcode)
{
    /*  Return the index in the symbs/svals/sflags/stypes arrays of symbol
        "p", creating a new symbol with that name if it isn't already there.

        New symbols are created with fundamental type UNKNOWN_CONSTANT_FT,
        value 0x100 (a 2-byte quantity in Z-machine terms) and type
        CONSTANT_T.

        The string "p" is undamaged.                                         */

    int32 new_entry, this, last; char *r;

    if (hashcode == -1) hashcode = hash_code_from_string(p);

    this = start_of_list[hashcode]; last = -1;

    do
    {   if (this == -1) break;

        r = (char *)symbs[this];
        new_entry = strcmpcis(r, p);
        if (new_entry == 0) return this;
        if (new_entry > 0) break;

        last = this;
        this = next_entry[this];
    } while (this != -1);

    if (no_symbols >= MAX_SYMBOLS)
        memoryerror("MAX_SYMBOLS", MAX_SYMBOLS);

    if (last == -1)
    {   next_entry[no_symbols]=start_of_list[hashcode];
        start_of_list[hashcode]=no_symbols;
    }
    else
    {   next_entry[no_symbols]=this;
        next_entry[last]=no_symbols;
    }

    if (symbols_free_space+strlen(p)+1 >= symbols_ceiling)
    {   symbols_free_space
            = my_malloc(SYMBOLS_CHUNK_SIZE, "symbol names chunk");
        symbols_ceiling = symbols_free_space + SYMBOLS_CHUNK_SIZE;
        /* If we've passed MAX_SYMBOL_CHUNKS chunks, we print an error
           message telling the user to increase SYMBOLS_CHUNK_SIZE.
           That is the correct cure, even though the error comes out
           worded inaccurately. */
        if (no_symbol_name_space_chunks >= MAX_SYMBOL_CHUNKS)
            memoryerror("SYMBOLS_CHUNK_SIZE", SYMBOLS_CHUNK_SIZE);
        symbol_name_space_chunks[no_symbol_name_space_chunks++]
            = (char *) symbols_free_space;
        if (symbols_free_space+strlen(p)+1 >= symbols_ceiling)
            memoryerror("SYMBOLS_CHUNK_SIZE", SYMBOLS_CHUNK_SIZE);
    }

    strcpy((char *) symbols_free_space, p);
    symbs[no_symbols] = (int32 *) symbols_free_space;
    symbols_free_space += strlen((char *)symbols_free_space) + 1;

    svals[no_symbols]   =  0x100; /* ###-wrong? Would this fix the
                                     unbound-symbol-causes-asm-error? */
    sflags[no_symbols]  =  UNKNOWN_SFLAG;
    stypes[no_symbols]  =  CONSTANT_T;
    slines[no_symbols]  =  ErrorReport.line_number
                           + 0x10000*ErrorReport.file_number;

    return(no_symbols++);
}

/* ------------------------------------------------------------------------- */
/*   Printing diagnostics                                                    */
/* ------------------------------------------------------------------------- */

extern char *typename(int type)
{   switch(type)
    {
        /*  These are the possible symbol types.  Note that local variables
            do not reside in the symbol table (for scope and efficiency
            reasons) and actions have their own name-space (via routine
            names with "Sub" appended).                                      */

        case ROUTINE_T:             return("Routine");
        case LABEL_T:               return("Label");
        case GLOBAL_VARIABLE_T:     return("Global variable");
        case ARRAY_T:               return("Array");
        case CONSTANT_T:            return("Defined constant");
        case ATTRIBUTE_T:           return("Attribute");
        case PROPERTY_T:            return("Property");
        case INDIVIDUAL_PROPERTY_T: return("Individual property");
        case OBJECT_T:              return("Object");
        case CLASS_T:               return("Class");
        case FAKE_ACTION_T:         return("Fake action");

        default:                   return("(Unknown type)");
    }
}

static void describe_flags(int flags)
{   if (flags & UNKNOWN_SFLAG)  printf("(?) ");
    if (flags & USED_SFLAG)     printf("(used) ");
    if (flags & REPLACE_SFLAG)  printf("(Replaced) ");
    if (flags & DEFCON_SFLAG)   printf("(Defaulted) ");
    if (flags & STUB_SFLAG)     printf("(Stubbed) ");
    if (flags & CHANGE_SFLAG)   printf("(value will change) ");
    if (flags & IMPORT_SFLAG)   printf("(Imported) ");
    if (flags & EXPORT_SFLAG)   printf("(Exported) ");
    if (flags & SYSTEM_SFLAG)   printf("(System) ");
    if (flags & INSF_SFLAG)     printf("(created in sys file) ");
    if (flags & UERROR_SFLAG)   printf("('Unknown' error issued) ");
    if (flags & ALIASED_SFLAG)  printf("(aliased) ");
    if (flags & ACTION_SFLAG)   printf("(Action name) ");
    if (flags & REDEFINABLE_SFLAG) printf("(Redefinable) ");
}

extern void describe_symbol(int k)
{   printf("%4d  %-16s  %2d:%04d  %04x  %s  ",
        k, (char *) (symbs[k]), slines[k]/0x10000, slines[k]%0x10000,
        svals[k], typename(stypes[k]));
    describe_flags(sflags[k]);
}

extern void list_symbols(int level)
{   int k;
    for (k=0; k<no_symbols; k++)
    {   if ((level==2) ||
            ((sflags[k] & (SYSTEM_SFLAG + UNKNOWN_SFLAG + INSF_SFLAG)) == 0))
        {   describe_symbol(k); printf("\n");
        }
    }
}

extern void issue_unused_warnings(void)
{   int32 i;

    if (module_switch) return;

    /*  Update any ad-hoc variables that might help the library  */
    if (glulx_mode)
    {   global_initial_value[10]=statusline_flag;
    }
    /*  Now back to mark anything necessary as used  */

    i = symbol_index("Main", -1);
    if (!(sflags[i] & UNKNOWN_SFLAG)) sflags[i] |= USED_SFLAG;

    for (i=0;i<no_symbols;i++)
    {   if (((sflags[i]
             & (SYSTEM_SFLAG + UNKNOWN_SFLAG + EXPORT_SFLAG
                + INSF_SFLAG + USED_SFLAG + REPLACE_SFLAG)) == 0)
             && (stypes[i] != OBJECT_T))
            dbnu_warning(typename(stypes[i]), (char *) symbs[i], slines[i]);
    }
}

/* ------------------------------------------------------------------------- */
/*   These are arrays used only during story file (never module) creation,   */
/*   and not allocated until then.                                           */

       int32 *individual_name_strings; /* Packed addresses of Z-encoded
                                          strings of the names of the
                                          properties: this is an array
                                          indexed by the property ID         */
       int32 *action_name_strings;     /* Ditto for actions                  */
       int32 *attribute_name_strings;  /* Ditto for attributes               */
       int32 *array_name_strings;      /* Ditto for arrays                   */

extern void write_the_identifier_names(void)
{   int i, j, k, t, null_value; char idname_string[256];
    static char unknown_attribute[20] = "<unknown attribute>";

    for (i=0; i<no_individual_properties; i++)
        individual_name_strings[i] = 0;

    if (module_switch) return;

    veneer_mode = TRUE;

    null_value = compile_string(unknown_attribute, FALSE, FALSE);
    for (i=0; i<NUM_ATTR_BYTES*8; i++) attribute_name_strings[i] = null_value;

    for (i=0; i<no_symbols; i++)
    {   t=stypes[i];
        if ((t == INDIVIDUAL_PROPERTY_T) || (t == PROPERTY_T))
        {   if (sflags[i] & ALIASED_SFLAG)
            {   if (individual_name_strings[svals[i]] == 0)
                {   sprintf(idname_string, "%s", (char *) symbs[i]);

                    for (j=i+1, k=0; (j<no_symbols && k<3); j++)
                    {   if ((stypes[j] == stypes[i])
                            && (svals[j] == svals[i]))
                        {   sprintf(idname_string+strlen(idname_string),
                                "/%s", (char *) symbs[j]);
                            k++;
                        }
                    }

                    if (debugfile_switch)
                    {   write_debug_byte(PROP_DBR);
                        write_debug_byte(svals[i]/256);
                        write_debug_byte(svals[i]%256);
                        write_debug_string(idname_string);
                    }

                    individual_name_strings[svals[i]]
                        = compile_string(idname_string, FALSE, FALSE);
                }
            }
            else
            {   sprintf(idname_string, "%s", (char *) symbs[i]);

                if (debugfile_switch)
                {   write_debug_byte(PROP_DBR);
                    write_debug_byte(svals[i]/256);
                    write_debug_byte(svals[i]%256);
                    write_debug_string(idname_string);
                }

                individual_name_strings[svals[i]]
                    = compile_string(idname_string, FALSE, FALSE);
            }
        }
        if (t == ATTRIBUTE_T)
        {   if (sflags[i] & ALIASED_SFLAG)
            {   if (attribute_name_strings[svals[i]] == null_value)
                {   sprintf(idname_string, "%s", (char *) symbs[i]);

                    for (j=i+1, k=0; (j<no_symbols && k<3); j++)
                    {   if ((stypes[j] == stypes[i])
                            && (svals[j] == svals[i]))
                        {   sprintf(idname_string+strlen(idname_string),
                                "/%s", (char *) symbs[j]);
                            k++;
                        }
                    }

                    if (debugfile_switch)
                    {   write_debug_byte(ATTR_DBR);
                        write_debug_byte(svals[i]/256);
                        write_debug_byte(svals[i]%256);
                        write_debug_string(idname_string);
                    }

                    attribute_name_strings[svals[i]]
                        = compile_string(idname_string, FALSE, FALSE);
                }
            }
            else
            {   sprintf(idname_string, "%s", (char *) symbs[i]);

                if (debugfile_switch)
                {   write_debug_byte(ATTR_DBR);
                    write_debug_byte(svals[i]/256);
                    write_debug_byte(svals[i]%256);
                    write_debug_string(idname_string);
                }

                attribute_name_strings[svals[i]]
                    = compile_string(idname_string, FALSE, FALSE);
            }
        }
        if (sflags[i] & ACTION_SFLAG)
        {   sprintf(idname_string, "%s", (char *) symbs[i]);
            idname_string[strlen(idname_string)-3] = 0;

            if (debugfile_switch)
            {   write_debug_byte(ACTION_DBR);
                write_debug_byte(svals[i]/256);
                write_debug_byte(svals[i]%256);
                write_debug_string(idname_string);
            }

            action_name_strings[svals[i]]
                = compile_string(idname_string, FALSE, FALSE);
        }
    }

    for (i=0; i<no_symbols; i++)
    {   if (stypes[i] == FAKE_ACTION_T)
        {   sprintf(idname_string, "%s", (char *) symbs[i]);
            idname_string[strlen(idname_string)-3] = 0;

            if (debugfile_switch)
            {   write_debug_byte(ACTION_DBR);
                write_debug_byte(svals[i]/256);
                write_debug_byte(svals[i]%256);
                write_debug_string(idname_string);
            }

            action_name_strings[svals[i]
                    - ((grammar_version_number==1)?256:4096) + no_actions]
                = compile_string(idname_string, FALSE, FALSE);
        }
    }

    for (j=0; j<no_arrays; j++)
    {   i = array_symbols[j];
        sprintf(idname_string, "%s", (char *) symbs[i]);

        if (debugfile_switch)
        {   write_debug_byte(ARRAY_DBR);
            write_debug_byte(svals[i]/256);
            write_debug_byte(svals[i]%256);
            write_debug_string(idname_string);
        }

        array_name_strings[j]
            = compile_string(idname_string, FALSE, FALSE);
    }
  if (define_INFIX_switch)
  { for (i=0; i<no_symbols; i++)
    {   if (stypes[i] == GLOBAL_VARIABLE_T)
        {   sprintf(idname_string, "%s", (char *) symbs[i]);
            array_name_strings[no_arrays + svals[i] -16]
                = compile_string(idname_string, FALSE, FALSE);
        }
    }

    for (i=0; i<no_named_routines; i++)
    {   sprintf(idname_string, "%s", (char *) symbs[named_routine_symbols[i]]);
            array_name_strings[no_arrays + no_globals + i]
                = compile_string(idname_string, FALSE, FALSE);
    }

    for (i=0, no_named_constants=0; i<no_symbols; i++)
    {   if (((stypes[i] == OBJECT_T) || (stypes[i] == CLASS_T)
            || (stypes[i] == CONSTANT_T))
            && ((sflags[i] & (UNKNOWN_SFLAG+ACTION_SFLAG))==0))
        {   sprintf(idname_string, "%s", (char *) symbs[i]);
            array_name_strings[no_arrays + no_globals + no_named_routines
                + no_named_constants++]
                = compile_string(idname_string, FALSE, FALSE);
        }
    }
  }

    veneer_mode = FALSE;
}
/* ------------------------------------------------------------------------- */
/*   Creating symbols                                                        */
/* ------------------------------------------------------------------------- */

static void assign_symbol_base(int index, int32 value, int type)
{   svals[index]  = value;
    stypes[index] = type;
    if (sflags[index] & UNKNOWN_SFLAG)
    {   sflags[index] &= (~UNKNOWN_SFLAG);
        if (is_systemfile()) sflags[index] |= INSF_SFLAG;
        slines[index] = ErrorReport.line_number
                        + 0x10000*ErrorReport.file_number;
    }
}

extern void assign_symbol(int index, int32 value, int type)
{
    if (!glulx_mode) {
        assign_symbol_base(index, value, type);
    }
    else {
        smarks[index] = 0;
        assign_symbol_base(index, value, type);
    }
}

extern void assign_marked_symbol(int index, int marker, int32 value, int type)
{
    if (!glulx_mode) {
        assign_symbol_base(index, (int32)marker*0x10000 + (value % 0x10000),
            type);
    }
    else {
        smarks[index] = marker;
        assign_symbol_base(index, value, type);
    }
}


static void create_symbol(char *p, int32 value, int type)
{   int i = symbol_index(p, -1);
    svals[i] = value; stypes[i] = type; slines[i] = 0;
    sflags[i] = USED_SFLAG + SYSTEM_SFLAG;
}

static void create_rsymbol(char *p, int value, int type)
{   int i = symbol_index(p, -1);
    svals[i] = value; stypes[i] = type; slines[i] = 0;
    sflags[i] = USED_SFLAG + SYSTEM_SFLAG + REDEFINABLE_SFLAG;
}

static void stockup_symbols(void)
{
    if (!glulx_mode)
        create_symbol("TARGET_ZCODE", 0, CONSTANT_T);
    else 
        create_symbol("TARGET_GLULX", 0, CONSTANT_T);

    create_symbol("nothing",        0, OBJECT_T);
    create_symbol("name",           1, PROPERTY_T);

    create_symbol("true",           1, CONSTANT_T);
    create_symbol("false",          0, CONSTANT_T);

    /* Glulx defaults to GV2; Z-code to GV1 */
    if (!glulx_mode)
        create_rsymbol("Grammar__Version", 1, CONSTANT_T);
    else
        create_rsymbol("Grammar__Version", 2, CONSTANT_T);
    grammar_version_symbol = symbol_index("Grammar__Version", -1);

    if (module_switch)
        create_rsymbol("MODULE_MODE",0, CONSTANT_T);

    if (runtime_error_checking_switch)
        create_rsymbol("STRICT_MODE",0, CONSTANT_T);

    if (define_DEBUG_switch)
        create_rsymbol("DEBUG",      0, CONSTANT_T);

    if (define_USE_MODULES_switch)
        create_rsymbol("USE_MODULES",0, CONSTANT_T);

    if (define_INFIX_switch)
    {   create_rsymbol("INFIX",      0, CONSTANT_T);
        create_symbol("infix__watching", 0, ATTRIBUTE_T);
    }

    create_symbol("WORDSIZE",        WORDSIZE, CONSTANT_T);
    create_symbol("DICT_ENTRY_BYTES", DICT_ENTRY_BYTE_LENGTH, CONSTANT_T);
    if (!glulx_mode) {
        create_symbol("DICT_WORD_SIZE", ((version_number==3)?4:6), CONSTANT_T);
        create_symbol("NUM_ATTR_BYTES", ((version_number==3)?4:6), CONSTANT_T);
    }
    else {
        create_symbol("DICT_WORD_SIZE",     DICT_WORD_SIZE, CONSTANT_T);
        create_symbol("DICT_CHAR_SIZE",     DICT_CHAR_SIZE, CONSTANT_T);
        if (DICT_CHAR_SIZE != 1)
            create_symbol("DICT_IS_UNICODE", 1, CONSTANT_T);
        create_symbol("NUM_ATTR_BYTES",     NUM_ATTR_BYTES, CONSTANT_T);
        create_symbol("INDIV_PROP_START",   INDIV_PROP_START, CONSTANT_T);
    }    

    if (!glulx_mode) {
        create_symbol("temp_global",  255, GLOBAL_VARIABLE_T);
        create_symbol("temp__global2", 254, GLOBAL_VARIABLE_T);
        create_symbol("temp__global3", 253, GLOBAL_VARIABLE_T);
        create_symbol("temp__global4", 252, GLOBAL_VARIABLE_T);
        create_symbol("self",         251, GLOBAL_VARIABLE_T);
        create_symbol("sender",       250, GLOBAL_VARIABLE_T);
        create_symbol("sw__var",      249, GLOBAL_VARIABLE_T);
        
        create_symbol("sys__glob0",     16, GLOBAL_VARIABLE_T);
        create_symbol("sys__glob1",     17, GLOBAL_VARIABLE_T);
        create_symbol("sys__glob2",     18, GLOBAL_VARIABLE_T);
        
        create_symbol("create",        64, INDIVIDUAL_PROPERTY_T);
        create_symbol("recreate",      65, INDIVIDUAL_PROPERTY_T);
        create_symbol("destroy",       66, INDIVIDUAL_PROPERTY_T);
        create_symbol("remaining",     67, INDIVIDUAL_PROPERTY_T);
        create_symbol("copy",          68, INDIVIDUAL_PROPERTY_T);
        create_symbol("call",          69, INDIVIDUAL_PROPERTY_T);
        create_symbol("print",         70, INDIVIDUAL_PROPERTY_T);
        create_symbol("print_to_array",71, INDIVIDUAL_PROPERTY_T);
    }
    else {
        /* In Glulx, these system globals are entered in order, not down 
           from 255. */
        create_symbol("temp_global",  MAX_LOCAL_VARIABLES+0, 
          GLOBAL_VARIABLE_T);
        create_symbol("temp__global2", MAX_LOCAL_VARIABLES+1, 
          GLOBAL_VARIABLE_T);
        create_symbol("temp__global3", MAX_LOCAL_VARIABLES+2, 
          GLOBAL_VARIABLE_T);
        create_symbol("temp__global4", MAX_LOCAL_VARIABLES+3, 
          GLOBAL_VARIABLE_T);
        create_symbol("self",         MAX_LOCAL_VARIABLES+4, 
          GLOBAL_VARIABLE_T);
        create_symbol("sender",       MAX_LOCAL_VARIABLES+5, 
          GLOBAL_VARIABLE_T);
        create_symbol("sw__var",      MAX_LOCAL_VARIABLES+6, 
          GLOBAL_VARIABLE_T);

        /* These are almost certainly meaningless, and can be removed. */
        create_symbol("sys__glob0",     MAX_LOCAL_VARIABLES+7, 
          GLOBAL_VARIABLE_T);
        create_symbol("sys__glob1",     MAX_LOCAL_VARIABLES+8, 
          GLOBAL_VARIABLE_T);
        create_symbol("sys__glob2",     MAX_LOCAL_VARIABLES+9, 
          GLOBAL_VARIABLE_T);

        /* value of statusline_flag to be written later */
        create_symbol("sys_statusline_flag",  MAX_LOCAL_VARIABLES+10, 
          GLOBAL_VARIABLE_T);

        /* These are created in order, but not necessarily at a fixed
           value. */
        create_symbol("create",        INDIV_PROP_START+0, 
          INDIVIDUAL_PROPERTY_T);
        create_symbol("recreate",      INDIV_PROP_START+1, 
          INDIVIDUAL_PROPERTY_T);
        create_symbol("destroy",       INDIV_PROP_START+2, 
          INDIVIDUAL_PROPERTY_T);
        create_symbol("remaining",     INDIV_PROP_START+3, 
          INDIVIDUAL_PROPERTY_T);
        create_symbol("copy",          INDIV_PROP_START+4, 
          INDIVIDUAL_PROPERTY_T);
        create_symbol("call",          INDIV_PROP_START+5, 
          INDIVIDUAL_PROPERTY_T);
        create_symbol("print",         INDIV_PROP_START+6, 
          INDIVIDUAL_PROPERTY_T);
        create_symbol("print_to_array",INDIV_PROP_START+7, 
          INDIVIDUAL_PROPERTY_T);

        /* Floating-point constants. Note that FLOAT_NINFINITY is not
           -FLOAT_INFINITY, because float negation doesn't work that
           way. Also note that FLOAT_NAN is just one of many possible
           "not-a-number" values. */
        create_symbol("FLOAT_INFINITY",  0x7F800000, CONSTANT_T);
        create_symbol("FLOAT_NINFINITY", 0xFF800000, CONSTANT_T);
        create_symbol("FLOAT_NAN",       0x7FC00000, CONSTANT_T);
    }
}

/* ========================================================================= */
/*   Data structure management routines                                      */
/* ------------------------------------------------------------------------- */

extern void init_symbols_vars(void)
{
    symbs = NULL;
    svals = NULL;
    smarks = NULL;
    stypes = NULL;
    sflags = NULL;
    next_entry = NULL;
    start_of_list = NULL;

    symbol_name_space_chunks = NULL;
    no_symbol_name_space_chunks = 0;
    symbols_free_space=NULL;
    symbols_ceiling=symbols_free_space;

    no_symbols = 0;

    make_case_conversion_grid();
}

extern void symbols_begin_pass(void) { }

extern void symbols_allocate_arrays(void)
{
    symbs      = my_calloc(sizeof(char *),  MAX_SYMBOLS, "symbols");
    svals      = my_calloc(sizeof(int32),   MAX_SYMBOLS, "symbol values");
    if (glulx_mode)
        smarks = my_calloc(sizeof(int),     MAX_SYMBOLS, "symbol markers");
    slines     = my_calloc(sizeof(int32),   MAX_SYMBOLS, "symbol lines");
    stypes     = my_calloc(sizeof(char),    MAX_SYMBOLS, "symbol types");
    sflags     = my_calloc(sizeof(int),     MAX_SYMBOLS, "symbol flags");
    next_entry = my_calloc(sizeof(int),     MAX_SYMBOLS,
                     "symbol linked-list forward links");
    start_of_list = my_calloc(sizeof(int32), HASH_TAB_SIZE,
                     "hash code list beginnings");

    symbol_name_space_chunks
        = my_calloc(sizeof(char *), MAX_SYMBOL_CHUNKS, "symbol names chunk addresses");

    init_symbol_banks();
    stockup_symbols();

    /*  Allocated during story file construction, not now  */
    individual_name_strings = NULL;
    attribute_name_strings = NULL;
    action_name_strings = NULL;
    array_name_strings = NULL;
}

extern void symbols_free_arrays(void)
{   int i;

    for (i=0; i<no_symbol_name_space_chunks; i++)
        my_free(&(symbol_name_space_chunks[i]),
            "symbol names chunk");

    my_free(&symbol_name_space_chunks, "symbol names chunk addresses");

    my_free(&symbs, "symbols");
    my_free(&svals, "symbol values");
    my_free(&smarks, "symbol markers");
    my_free(&slines, "symbol lines");
    my_free(&stypes, "symbol types");
    my_free(&sflags, "symbol flags");
    my_free(&next_entry, "symbol linked-list forward links");
    my_free(&start_of_list, "hash code list beginnings");

    if (individual_name_strings != NULL)
        my_free(&individual_name_strings, "property name strings");
    if (action_name_strings != NULL)
        my_free(&action_name_strings,     "action name strings");
    if (attribute_name_strings != NULL)
        my_free(&attribute_name_strings,  "attribute name strings");
    if (array_name_strings != NULL)
        my_free(&array_name_strings,      "array name strings");
}

/* ========================================================================= */

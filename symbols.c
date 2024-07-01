/* ------------------------------------------------------------------------- */
/*   "symbols" :  The symbols table; creating stock of reserved words        */
/*                                                                           */
/*   Part of Inform 6.43                                                     */
/*   copyright (c) Graham Nelson 1993 - 2024                                 */
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
/*   Plus an array of symbolinfo.  Each symbol has its own index n (an       */
/*   int32) in the array. The struct there contains:                         */
/*                                                                           */
/*       value   is its value. In Z-code, this holds both the 16-bit value   */
/*                  and the 16-bit backpatch marker, so it is an int32.      */
/*       marker   is the backpatch marker in Glulx.                          */
/*       flags  holds flags (see "header.h" for a list of ?_SFLAGS)          */
/*       type  is the "type", distinguishing between the data type of        */
/*                  different kinds of constants/variables.                  */
/*                  (A ?_T constant; see the "typename()" below.)            */
/*       name   is the name of the symbol, in the same case form as          */
/*                  when created.                                            */
/*       line  is the source line on which the symbol value was first        */
/*                  assigned                                                 */
/*       next_entry  is the forward link in the symbol hash table. (See      */
/*                  start_of_list, below.)                                   */
/*                                                                           */
/*   When generating a debug file (-k switch), we also allocate an array     */
/*   of symboldebuginfo, which contains:                                     */
/*                                                                           */
/*       backpatch_pos                                                       */
/*                  is a file position in the debug information file where   */
/*                  the symbol's value should be written after backpatching, */
/*                  or else the null position if the value was known and     */
/*                  written beforehand                                       */
/*       replacement_backpatch_pos                                           */
/*                  is a file position in the debug information file where   */
/*                  the symbol's name can be erased if it is replaced, or    */
/*                  else null if the name will never need to be replaced     */
/*                                                                           */
/*   Comparison is case insensitive.                                         */
/*   Note that local variable names are not entered into the symbols table,  */
/*   as their numbers and scope are too limited for this to be efficient.    */
/* ------------------------------------------------------------------------- */

symbolinfo *symbols;                           /* Allocated up to no_symbols */
static memory_list symbols_memlist;
symboldebuginfo *symbol_debug_info;            /* Allocated up to no_symbols */
static memory_list symbol_debug_info_memlist;
static char *temp_symbol_buf;        /* used in write_the_identifier_names() */
static memory_list temp_symbol_buf_memlist;

/* ------------------------------------------------------------------------- */
/*   Memory to hold the text of symbol names: note that this memory is       */
/*   allocated as needed in chunks of size SYMBOLS_CHUNK_SIZE. (Or           */
/*   larger, if needed for a particularly enormous symbol.)                  */
/* ------------------------------------------------------------------------- */

#define SYMBOLS_CHUNK_SIZE (4096)

static char *symbols_free_space,        /* Next byte free to hold new names  */
           *symbols_ceiling;            /* Pointer to the end of the current
                                           allocation of memory for names    */

static char** symbol_name_space_chunks; /* For chunks of memory used to hold
                                           the name strings of symbols       */
static int no_symbol_name_space_chunks;
static memory_list symbol_name_space_chunks_memlist;

/* Symbol replacements (used by the "Replace X Y" directive). */

typedef struct value_pair_struct {
    int original_symbol;
    int renamed_symbol;
} value_pair_t;
static value_pair_t *symbol_replacements;
static int symbol_replacements_count;
static int symbol_replacements_size; /* calloced size */

/* Symbol definitions requested at compile time. (There may not be any.)
   These are set up at command-line parse time, not in init_symbols_vars().
   Similarly, they are not cleaned up by symbols_free_arrays(). */

typedef struct keyvalue_pair_struct {
    char *symbol;
    int32 value;
} keyvalue_pair_t;
static keyvalue_pair_t *symbol_definitions = NULL;
static int symbol_definitions_count = 0;
static int symbol_definitions_size = 0; /* calloced size */

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

static int32 *start_of_list; /* Allocated array of size HASH_TAB_SIZE */
/* The next_entry field is part of the symbolinfo struct. */

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

extern void add_config_symbol_definition(char *symbol, int32 value)
{   char *str;

    if (symbol_definitions_count == symbol_definitions_size) {
        int oldsize = symbol_definitions_size;
        if (symbol_definitions_size == 0) 
            symbol_definitions_size = 4;
        else
            symbol_definitions_size *= 2;
        my_recalloc(&symbol_definitions, sizeof(keyvalue_pair_t), oldsize,
            symbol_definitions_size, "symbol definition table");
    }

    str = my_malloc(strlen(symbol)+1, "symbol name");
    strcpy(str, symbol);
    
    symbol_definitions[symbol_definitions_count].symbol = str;
    symbol_definitions[symbol_definitions_count].value = value;
    symbol_definitions_count++;
}

/* ------------------------------------------------------------------------- */
/*   Symbol finding, creating, and removing.                                 */
/* ------------------------------------------------------------------------- */

extern int get_symbol_index(char *p)
{
    /*  Return the index in the symbols array of symbol "p", or -1
        if it isn't there. Does not create a new symbol or mark the
        symbol as used. */

    int32 new_entry, this;
    char *r;
    int hashcode = hash_code_from_string(p);

    this = start_of_list[hashcode];

    do
    {   if (this == -1) break;

        r = symbols[this].name;
        new_entry = strcmpcis(r, p);
        if (new_entry == 0) 
        {
            return this;
        }
        if (new_entry > 0) break;

        this = symbols[this].next_entry;
    } while (this != -1);

    return -1;
}

extern int symbol_index(char *p, int hashcode, int *created)
{
    /*  Return the index in the symbols array of symbol "p", creating a
        new symbol with that name if it isn't already there. This
        always returns a valid symbol index.

        The optional created argument receives TRUE if the symbol
        was newly created.

        Pass in the hashcode of p if you know it, or -1 if you don't.

        New symbols are created with flag UNKNOWN_SFLAG, value 0x100
        (a 2-byte quantity in Z-machine terms) and type CONSTANT_T.

        The string "p" is undamaged.                                         */

    int32 new_entry, this, last;
    char *r;
    int len;    

    if (hashcode == -1) hashcode = hash_code_from_string(p);

    this = start_of_list[hashcode]; last = -1;

    do
    {   if (this == -1) break;

        r = symbols[this].name;
        new_entry = strcmpcis(r, p);
        if (new_entry == 0) 
        {
            if (track_unused_routines)
                df_note_function_symbol(this);
            if (created) *created = FALSE;
            return this;
        }
        if (new_entry > 0) break;

        last = this;
        this = symbols[this].next_entry;
    } while (this != -1);

    if (symdef_trace_setting)
        printf("%s: Encountered symbol %d '%s'\n", current_location_text(), no_symbols, p);
    
    ensure_memory_list_available(&symbols_memlist, no_symbols+1);
    if (debugfile_switch)
        ensure_memory_list_available(&symbol_debug_info_memlist, no_symbols+1);

    if (last == -1)
    {   symbols[no_symbols].next_entry=start_of_list[hashcode];
        start_of_list[hashcode]=no_symbols;
    }
    else
    {   symbols[no_symbols].next_entry=this;
        symbols[last].next_entry=no_symbols;
    }

    len = strlen(p);
    if (!symbols_free_space || symbols_free_space+len+1 >= symbols_ceiling)
    {
        /* Allocate a new chunk whose size is big enough for the current
           symbol, or SYMBOLS_CHUNK_SIZE, whichever is greater. */
        int chunklen = SYMBOLS_CHUNK_SIZE;
        if (chunklen < len+1)
            chunklen = len+1;
        symbols_free_space
            = my_malloc(chunklen, "symbol names chunk");
        symbols_ceiling = symbols_free_space + chunklen;
        ensure_memory_list_available(&symbol_name_space_chunks_memlist, no_symbol_name_space_chunks+1);
        symbol_name_space_chunks[no_symbol_name_space_chunks++]
            = symbols_free_space;
    }

    strcpy(symbols_free_space, p);
    symbols[no_symbols].name   = symbols_free_space;
    symbols_free_space += (len+1);

    symbols[no_symbols].value   =  0x100; /* ###-wrong? Would this fix the
                                     unbound-symbol-causes-asm-error? */
    symbols[no_symbols].flags  =  UNKNOWN_SFLAG;
    symbols[no_symbols].marker =  0;
    symbols[no_symbols].type  =  CONSTANT_T;
    symbols[no_symbols].line  =  get_brief_location(&ErrorReport);
    if (debugfile_switch)
    {   nullify_debug_file_position
            (&symbol_debug_info[no_symbols].backpatch_pos);
        nullify_debug_file_position
            (&symbol_debug_info[no_symbols].replacement_backpatch_pos);
    }

    if (track_unused_routines)
        df_note_function_symbol(no_symbols);
    if (created) *created = TRUE;
    return(no_symbols++);
}

extern void end_symbol_scope(int k, int neveruse)
{
    /* Remove the given symbol from the hash table, making it
       invisible to symbol_index. This is used by the Undef directive
       and put_token_back().

       If you know the symbol has never been used, set neveruse and
       it will be flagged as an error if it *is* used.
       
       If the symbol is not found in the hash table, this silently does
       nothing.
    */

    int j;
    
    symbols[k].flags |= UNHASHED_SFLAG;
    if (neveruse)
        symbols[k].flags |= DISCARDED_SFLAG;
        
    j = hash_code_from_string(symbols[k].name);
    if (start_of_list[j] == k)
    {   start_of_list[j] = symbols[k].next_entry;
        return;
    }
    j = start_of_list[j];
    while (j != -1)
    {
        if (symbols[j].next_entry == k)
        {   symbols[j].next_entry = symbols[k].next_entry;
            return;
        }
        j = symbols[j].next_entry;
    }
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
        case STATIC_ARRAY_T:        return("Static array");
        case CONSTANT_T:            return("Defined constant");
        case ATTRIBUTE_T:           return("Attribute");
        case PROPERTY_T:            return("Property");
        case INDIVIDUAL_PROPERTY_T: return("Individual property");
        case OBJECT_T:              return("Object");
        case CLASS_T:               return("Class");
        case FAKE_ACTION_T:         return("Fake action");
            
        /*  These are not symbol types, but they get printed in errors. */
        case STRING_REQ_T:          return("String");
        case DICT_WORD_REQ_T:       return("Dictionary word");

        default:                   return("(Unknown type)");
    }
}

static void describe_flags(int flags)
{   if (flags & UNKNOWN_SFLAG)  printf("(?) ");
    if (flags & REPLACE_SFLAG)  printf("(Replaced) ");
    if (flags & USED_SFLAG)     printf("(used) ");
    if (flags & DEFCON_SFLAG)   printf("(Defaulted) ");
    if (flags & STUB_SFLAG)     printf("(Stubbed) ");
    if (flags & UNHASHED_SFLAG) printf("(not in hash chain) ");
    if (flags & DISCARDED_SFLAG)  printf("(removed, do not use) ");
    if (flags & ALIASED_SFLAG)  printf("(aliased) ");
    if (flags & CHANGE_SFLAG)   printf("(value will change) ");
    if (flags & SYSTEM_SFLAG)   printf("(System) ");
    if (flags & INSF_SFLAG)     printf("(created in sys file) ");
    if (flags & UERROR_SFLAG)   printf("('Unknown' error issued) ");
    if (flags & ACTION_SFLAG)   printf("(Action name) ");
    if (flags & REDEFINABLE_SFLAG) printf("(Redefinable) ");
    if (flags & STAR_SFLAG)     printf("(*) ");
}

extern void describe_symbol(int k)
{   printf("%4d  %-16s  %2d:%04d  %04x  %s  ",
        k, (symbols[k].name), 
        (int)(symbols[k].line.file_index),
        (int)(symbols[k].line.line_number),
        symbols[k].value, typename(symbols[k].type));
    describe_flags(symbols[k].flags);
}

extern void list_symbols(int level)
{   int k;
    for (k=0; k<no_symbols; k++)
    {   if ((level>=2) ||
            ((symbols[k].flags & (SYSTEM_SFLAG + UNKNOWN_SFLAG + INSF_SFLAG)) == 0))
        {   describe_symbol(k); printf("\n");
        }
    }
}

/* Check that the operand is of the given symbol type (XXX_T). If wanttype2 is nonzero, that's a second allowable type.
   Generate a warning if no match. */
extern void check_warn_symbol_type(const assembly_operand *AO, int wanttype, int wanttype2, char *context)
{
    symbolinfo *sym;
    int symtype;
    
    if (AO->symindex < 0)
    {
        /* This argument is not a symbol; it's a local variable, a literal, or a computed expression. */
        /* We can recognize and type-check some literals. */
        if (AO->marker == DWORD_MV) {
            if (wanttype != DICT_WORD_REQ_T && wanttype2 != DICT_WORD_REQ_T)
                symtype_warning(context, NULL, typename(DICT_WORD_REQ_T), typename(wanttype));
        }
        if (AO->marker == STRING_MV) {
            if (wanttype != STRING_REQ_T && wanttype2 != STRING_REQ_T)
                symtype_warning(context, NULL, typename(STRING_REQ_T), typename(wanttype));
        }
        return;
    }
    
    sym = &symbols[AO->symindex];
    symtype = sym->type;
    
    if (symtype == GLOBAL_VARIABLE_T)
    {
        /* A global variable could have any value. No way to generate a warning. */
        return;
    }
    if (symtype == CONSTANT_T)
    {
        /* A constant could also have any value. This case also includes forward-declared constants (UNKNOWN_SFLAG). */
        /* We try inferring its type by looking at the backpatch marker. Sadly, this only works for objects. (And not in Z-code, where object values are not backpatched.) */
        if (sym->marker == OBJECT_MV) {
            /* Continue with inferred type. */
            symtype = OBJECT_T;
        }
        else {
            /* Give up. */
            return;
        }
    }
    
    if (!(   (symtype == wanttype)
          || (wanttype2 != 0 && symtype == wanttype2)))
    {
        symtype_warning(context, sym->name, typename(symtype), typename(wanttype));
    }
}

/* Similar, but we allow any type that has a metaclass: Object, Class, String, or Routine.
   Generate a warning if no match. */
extern void check_warn_symbol_has_metaclass(const assembly_operand *AO, char *context)
{
    symbolinfo *sym;
    int symtype;
    
    if (AO->symindex < 0)
    {
        /* This argument is not a symbol; it's a local variable, a literal, or a computed expression. */
        /* We can recognize and type-check some literals. */
        if (AO->marker == DWORD_MV) {
            symtype_warning(context, NULL, typename(DICT_WORD_REQ_T), "Object/Class/Routine/String");
        }
        if (AO->marker == STRING_MV) {
            /* Strings are good here. */
        }
        return;
    }
    
    sym = &symbols[AO->symindex];
    symtype = sym->type;
    
    if (symtype == GLOBAL_VARIABLE_T)
    {
        /* A global variable could have any value. No way to generate a warning. */
        return;
    }
    if (symtype == CONSTANT_T)
    {
        /* A constant could also have any value. This case also includes forward-declared constants (UNKNOWN_SFLAG). */
        /* We try inferring its type by looking at the backpatch marker. Sadly, this only works for objects. (And not in Z-code, where object values are not backpatched.) */
        if (sym->marker == OBJECT_MV) {
            /* Continue with inferred type. */
            symtype = OBJECT_T;
        }
        else {
            /* Give up. */
            return;
        }
    }

    if (!(symtype == ROUTINE_T || symtype == CLASS_T || symtype == OBJECT_T))
    {
        symtype_warning(context, sym->name, typename(symtype), "Object/Class/Routine/String");
    }
}

extern void issue_unused_warnings(void)
{   int32 i;

    /*  Update any ad-hoc variables that might help the library  */
    if (glulx_mode)
    {   global_initial_value[10]=statusline_flag;
    }
    /*  Now back to mark anything necessary as used  */

    i = get_symbol_index("Main");
    if (i >= 0 && !(symbols[i].flags & UNKNOWN_SFLAG)) {
        symbols[i].flags |= USED_SFLAG;
    }

    for (i=0;i<no_symbols;i++)
    {   if (((symbols[i].flags
             & (SYSTEM_SFLAG + UNKNOWN_SFLAG
                + INSF_SFLAG + USED_SFLAG + REPLACE_SFLAG)) == 0)
             && (symbols[i].type != OBJECT_T)) {
            dbnu_warning(typename(symbols[i].type), symbols[i].name, symbols[i].line);
        }
        if ((symbols[i].flags & DISCARDED_SFLAG)
            && (symbols[i].flags & USED_SFLAG)) {
            error_named_at("Symbol was removed from the symbol table, but seems to be in use anyway", symbols[i].name, symbols[i].line);
        }
    }
}

extern void issue_debug_symbol_warnings(void)
{
    int value = get_symbol_index("DEBUG");
    if (value >= 0 && (symbols[value].flags & USED_SFLAG) && !(symbols[value].flags & UNKNOWN_SFLAG)) {
        value = get_symbol_index("debug_flag");
        if (value >= 0 && (symbols[value].flags & USED_SFLAG) && (symbols[value].flags & UNKNOWN_SFLAG)) {
            warning("DEBUG mode is on, but this story or library does not appear to support it");
        }
    }
}

/* ------------------------------------------------------------------------- */
/*   These are arrays used only during story file creation, and not          */
/*   allocated until just before write_the_identifier_names() time.          */

       int32 *individual_name_strings; /* Packed addresses of Z-encoded
                                          strings of the names of the
                                          properties: this is an array
                                          indexed by the property ID         */
       int32 *action_name_strings;     /* Ditto for actions and fake actions */
       int32 *attribute_name_strings;  /* Ditto for attributes               */
       int32 *array_name_strings;      /* Ditto for arrays                   */

extern void write_the_identifier_names(void)
{   int i, j, k, t, null_value;
    static char unknown_attribute[20] = "<unknown attribute>";

    for (i=0; i<no_individual_properties; i++)
        individual_name_strings[i] = 0;

    veneer_mode = TRUE;

    null_value = compile_string(unknown_attribute, STRCTX_SYMBOL);
    for (i=0; i<NUM_ATTR_BYTES*8; i++) attribute_name_strings[i] = null_value;

    for (i=0; i<no_symbols; i++)
    {   t=symbols[i].type;
        if ((t == INDIVIDUAL_PROPERTY_T) || (t == PROPERTY_T))
        {   if (symbols[i].flags & ALIASED_SFLAG)
            {   if (individual_name_strings[symbols[i].value] == 0)
                {
                    int sleni = strlen(symbols[i].name);
                    ensure_memory_list_available(&temp_symbol_buf_memlist, sleni+1);
                    sprintf(temp_symbol_buf, "%s", symbols[i].name);

                    for (j=i+1, k=0; (j<no_symbols && k<3); j++)
                    {   if ((symbols[j].type == symbols[i].type)
                            && (symbols[j].value == symbols[i].value))
                        {
                            int slenj = strlen(symbols[j].name);
                            ensure_memory_list_available(&temp_symbol_buf_memlist, strlen(temp_symbol_buf)+1+slenj+1);
                            sprintf(temp_symbol_buf+strlen(temp_symbol_buf),
                                "/%s", symbols[j].name);
                            k++;
                        }
                    }

                    individual_name_strings[symbols[i].value]
                        = compile_string(temp_symbol_buf, STRCTX_SYMBOL);
                }
            }
            else
            {
                individual_name_strings[symbols[i].value]
                    = compile_string(symbols[i].name, STRCTX_SYMBOL);
            }
        }
        if (t == ATTRIBUTE_T)
        {
            if (symbols[i].flags & ALIASED_SFLAG)
            {   if (attribute_name_strings[symbols[i].value] == null_value)
                {
                    int sleni = strlen(symbols[i].name);
                    ensure_memory_list_available(&temp_symbol_buf_memlist, sleni+1);
                    sprintf(temp_symbol_buf, "%s", symbols[i].name);

                    for (j=i+1, k=0; (j<no_symbols && k<3); j++)
                    {   if ((symbols[j].type == symbols[i].type)
                            && (symbols[j].value == symbols[i].value))
                        {
                            int slenj = strlen(symbols[j].name);
                            ensure_memory_list_available(&temp_symbol_buf_memlist, strlen(temp_symbol_buf)+1+slenj+1);
                            sprintf(temp_symbol_buf+strlen(temp_symbol_buf),
                                "/%s", symbols[j].name);
                            k++;
                        }
                    }

                    attribute_name_strings[symbols[i].value]
                        = compile_string(temp_symbol_buf, STRCTX_SYMBOL);
                }
            }
            else
            {
                attribute_name_strings[symbols[i].value]
                    = compile_string(symbols[i].name, STRCTX_SYMBOL);
            }
        }
        
        if (symbols[i].flags & ACTION_SFLAG)
        {
            int sleni = strlen(symbols[i].name);
            ensure_memory_list_available(&temp_symbol_buf_memlist, sleni+1);
            sprintf(temp_symbol_buf, "%s", symbols[i].name);
            temp_symbol_buf[strlen(temp_symbol_buf)-3] = 0;

            if (debugfile_switch)
            {   debug_file_printf("<action>");
                debug_file_printf
                    ("<identifier>##%s</identifier>", temp_symbol_buf);
                debug_file_printf("<value>%d</value>", symbols[i].value);
                debug_file_printf("</action>");
            }

            action_name_strings[symbols[i].value]
                = compile_string(temp_symbol_buf, STRCTX_SYMBOL);
        }
    }

    for (i=0; i<no_symbols; i++)
    {   if (symbols[i].type == FAKE_ACTION_T)
        {
            int sleni = strlen(symbols[i].name);
            ensure_memory_list_available(&temp_symbol_buf_memlist, sleni+1);
            sprintf(temp_symbol_buf, "%s", symbols[i].name);
            temp_symbol_buf[strlen(temp_symbol_buf)-3] = 0;

            action_name_strings[symbols[i].value
                    - lowest_fake_action() + no_actions]
                = compile_string(temp_symbol_buf, STRCTX_SYMBOL);
        }
    }

    for (j=0; j<no_arrays; j++)
    {
        i = arrays[j].symbol;
        array_name_strings[j]
            = compile_string(symbols[i].name, STRCTX_SYMBOL);
    }
    
    if (define_INFIX_switch)
    {
        for (i=0; i<no_symbols; i++)
        {   if (symbols[i].type == GLOBAL_VARIABLE_T)
            {
                array_name_strings[no_arrays + symbols[i].value -16]
                    = compile_string(symbols[i].name, STRCTX_SYMBOL);
            }
        }
        
        for (i=0; i<no_named_routines; i++)
        {
            array_name_strings[no_arrays + no_globals + i]
                = compile_string(symbols[named_routine_symbols[i]].name, STRCTX_SYMBOL);
        }
        
        for (i=0, no_named_constants=0; i<no_symbols; i++)
        {   if (((symbols[i].type == OBJECT_T) || (symbols[i].type == CLASS_T)
                 || (symbols[i].type == CONSTANT_T))
                && ((symbols[i].flags & (UNKNOWN_SFLAG+ACTION_SFLAG))==0))
            {
                array_name_strings[no_arrays + no_globals + no_named_routines
                                   + no_named_constants++]
                    = compile_string(symbols[i].name, STRCTX_SYMBOL);
            }
        }
    }

    veneer_mode = FALSE;
}
/* ------------------------------------------------------------------------- */
/*   Creating symbols                                                        */
/* ------------------------------------------------------------------------- */

static void assign_symbol_base(int index, int32 value, int type)
{   symbols[index].value  = value;
    symbols[index].type = type;
    if (symbols[index].flags & UNKNOWN_SFLAG)
    {   symbols[index].flags &= (~UNKNOWN_SFLAG);
        if (is_systemfile()) symbols[index].flags |= INSF_SFLAG;
        symbols[index].line = get_brief_location(&ErrorReport);
    }
}

extern void assign_symbol(int index, int32 value, int type)
{
    assign_symbol_base(index, value, type);
    symbols[index].marker = 0;
    if (symdef_trace_setting)
        printf("%s: Defined symbol %d '%s' as %d (%s)\n", current_location_text(), index, symbols[index].name, value, typename(type));
}

extern void assign_marked_symbol(int index, int marker, int32 value, int type)
{
    assign_symbol_base(index, value, type);
    symbols[index].marker = marker;
    if (symdef_trace_setting)
        printf("%s: Defined symbol %d '%s' as %s %d (%s)\n", current_location_text(), index, symbols[index].name, describe_mv(marker), value, typename(type));
}

static void emit_debug_information_for_predefined_symbol
    (char *name, int32 symbol, int32 value, int type)
{   if (debugfile_switch)
    {   switch (type)
        {   case CONSTANT_T:
                debug_file_printf("<constant>");
                debug_file_printf("<identifier>%s</identifier>", name);
                write_debug_symbol_optional_backpatch(symbol);
                debug_file_printf("</constant>");
                break;
            case GLOBAL_VARIABLE_T:
                debug_file_printf("<global-variable>");
                debug_file_printf("<identifier>%s</identifier>", name);
                debug_file_printf("<address>");
                write_debug_global_backpatch(value);
                debug_file_printf("</address>");
                debug_file_printf("</global-variable>");
                break;
            case OBJECT_T:
                if (value)
                {   compiler_error("Non-nothing object predefined");
                }
                debug_file_printf("<object>");
                debug_file_printf("<identifier>%s</identifier>", name);
                debug_file_printf("<value>0</value>");
                debug_file_printf("</object>");
                break;
            case ATTRIBUTE_T:
                debug_file_printf("<attribute>");
                debug_file_printf("<identifier>%s</identifier>", name);
                debug_file_printf("<value>%d</value>", value);
                debug_file_printf("</attribute>");
                break;
            case PROPERTY_T:
            case INDIVIDUAL_PROPERTY_T:
                debug_file_printf("<property>");
                debug_file_printf("<identifier>%s</identifier>", name);
                debug_file_printf("<value>%d</value>", value);
                debug_file_printf("</property>");
                break;
            default:
                compiler_error
                    ("Unable to emit debug information for predefined symbol");
            break;
        }
    }
}

static void create_symbol(char *p, int32 value, int type)
{   int i = symbol_index(p, -1, NULL);
    if (!(symbols[i].flags & (UNKNOWN_SFLAG + REDEFINABLE_SFLAG))) {
        /* Symbol already defined! */
        if (symbols[i].value == value && symbols[i].type == type) {
            /* Special case: the symbol was already defined with this same
               value. We let it pass. */
            return;
        }
        else {
            ebf_symbol_error("new symbol", p, typename(symbols[i].type), symbols[i].line);
            return;
        }
    }
    symbols[i].value = value; symbols[i].type = type; symbols[i].line = blank_brief_location;
    /* If the symbol already existed with REDEFINABLE_SFLAG, we keep that. */
    symbols[i].flags = USED_SFLAG + SYSTEM_SFLAG + (symbols[i].flags & REDEFINABLE_SFLAG);
    emit_debug_information_for_predefined_symbol(p, i, value, type);
}

static void create_rsymbol(char *p, int value, int type)
{   int i = symbol_index(p, -1, NULL);
    /* This is only called for a few symbols with known names.
       They will not collide. */
    symbols[i].value = value; symbols[i].type = type; symbols[i].line = blank_brief_location;
    symbols[i].flags = USED_SFLAG + SYSTEM_SFLAG + REDEFINABLE_SFLAG;
    emit_debug_information_for_predefined_symbol(p, i, value, type);
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

    /* Glulx defaults to GV2; Z-code to GV1. These may be modified by
       command-line options, but we haven't applied that change yet. */
    create_rsymbol("Grammar__Version", grammar_version_number, CONSTANT_T);
    grammar_version_symbol = get_symbol_index("Grammar__Version");

    if (runtime_error_checking_switch)
        create_rsymbol("STRICT_MODE",0, CONSTANT_T);

    if (define_DEBUG_switch)
        create_rsymbol("DEBUG",      0, CONSTANT_T);

    if (define_INFIX_switch)
    {   create_rsymbol("INFIX",      0, CONSTANT_T);
        create_symbol("infix__watching", 0, ATTRIBUTE_T);
    }

    if (OMIT_SYMBOL_TABLE)
        create_symbol("OMIT_SYMBOL_TABLE", 0, CONSTANT_T);

    if (GRAMMAR_META_FLAG)
        create_symbol("GRAMMAR_META_FLAG", 0, CONSTANT_T);

    create_symbol("WORDSIZE",        WORDSIZE, CONSTANT_T);
    /* DICT_ENTRY_BYTES must be REDEFINABLE_SFLAG because the Version directive can change it. */
    create_rsymbol("DICT_ENTRY_BYTES", DICT_ENTRY_BYTE_LENGTH, CONSTANT_T);
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
        create_symbol("GOBJFIELD_CHAIN",    GOBJFIELD_CHAIN(), CONSTANT_T);
        create_symbol("GOBJFIELD_NAME",     GOBJFIELD_NAME(), CONSTANT_T);
        create_symbol("GOBJFIELD_PROPTAB",  GOBJFIELD_PROPTAB(), CONSTANT_T);
        create_symbol("GOBJFIELD_PARENT",   GOBJFIELD_PARENT(), CONSTANT_T);
        create_symbol("GOBJFIELD_SIBLING",  GOBJFIELD_SIBLING(), CONSTANT_T);
        create_symbol("GOBJFIELD_CHILD",    GOBJFIELD_CHILD(), CONSTANT_T);
        create_symbol("GOBJ_EXT_START",     1+NUM_ATTR_BYTES+6*WORDSIZE, CONSTANT_T);
        create_symbol("GOBJ_TOTAL_LENGTH",  1+NUM_ATTR_BYTES+6*WORDSIZE+GLULX_OBJECT_EXT_BYTES, CONSTANT_T);
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
        /* Same for double constants. Each of these has a high 32-bit
           word and a low 32-bit word. */
        create_symbol("DOUBLE_HI_INFINITY",  0x7FF00000, CONSTANT_T);
        create_symbol("DOUBLE_LO_INFINITY",  0x00000000, CONSTANT_T);
        create_symbol("DOUBLE_HI_NINFINITY", 0xFFF00000, CONSTANT_T);
        create_symbol("DOUBLE_LO_NINFINITY", 0x00000000, CONSTANT_T);
        create_symbol("DOUBLE_HI_NAN",       0x7FF80000, CONSTANT_T);
        create_symbol("DOUBLE_LO_NAN",       0x00000001, CONSTANT_T);
    }

    if (symbol_definitions && symbol_definitions_count) {
        int ix;
        for (ix=0; ix<symbol_definitions_count; ix++) {
            char *str = symbol_definitions[ix].symbol;
            int32 val = symbol_definitions[ix].value;
            create_symbol(str, val, CONSTANT_T);
        }
    }
}

/* ------------------------------------------------------------------------- */
/*   The symbol replacement table. This is needed only for the               */
/*   "Replace X Y" directive.                                                */
/* ------------------------------------------------------------------------- */

extern void add_symbol_replacement_mapping(int original, int renamed)
{
    int ix;

    if (original == renamed) {
        error_named("A routine cannot be 'Replace'd to itself:", symbols[original].name);
        return;        
    }

    if (symbol_replacements_count == symbol_replacements_size) {
        int oldsize = symbol_replacements_size;
        if (symbol_replacements_size == 0) 
            symbol_replacements_size = 4;
        else
            symbol_replacements_size *= 2;
        my_recalloc(&symbol_replacements, sizeof(value_pair_t), oldsize,
            symbol_replacements_size, "symbol replacement table");
    }

    /* If the original form is already in our table, report an error.
       Same goes if the replaced form is already in the table as an
       original. (Other collision cases have already been
       detected.) */

    for (ix=0; ix<symbol_replacements_count; ix++) {
        if (original == symbol_replacements[ix].original_symbol) {
            error_named("A routine cannot be 'Replace'd to more than one new name:", symbols[original].name);
        }
        if (renamed == symbol_replacements[ix].original_symbol) {
            error_named("A routine cannot be 'Replace'd to a 'Replace'd name:", symbols[original].name);
        }
    }

    symbol_replacements[symbol_replacements_count].original_symbol = original;
    symbol_replacements[symbol_replacements_count].renamed_symbol = renamed;
    symbol_replacements_count++;
}

extern int find_symbol_replacement(int *value)
{
    int changed = FALSE;
    int ix;

    if (!symbol_replacements)
        return FALSE;

    for (ix=0; ix<symbol_replacements_count; ix++) {
        if (*value == symbol_replacements[ix].original_symbol) {
            *value = symbol_replacements[ix].renamed_symbol;
            changed = TRUE;
        }
    }

    return changed;
}

/* ------------------------------------------------------------------------- */
/*   The dead-function removal optimization.                                 */
/* ------------------------------------------------------------------------- */

int track_unused_routines; /* set if either WARN_UNUSED_ROUTINES or
                              OMIT_UNUSED_ROUTINES is nonzero */
int df_dont_note_global_symbols; /* temporarily set at times in parsing */
static int df_tables_closed; /* set at end of compiler pass */

typedef struct df_function_struct df_function_t;
typedef struct df_reference_struct df_reference_t;

struct df_function_struct {
    char *name; /* borrowed reference, generally to the symbs[] table */
    brief_location source_line; /* copied from routine_starts_line */
    int sysfile; /* does this occur in a system file? */
    uint32 address; /* function offset in zcode_area (not the final address) */
    uint32 newaddress; /* function offset after stripping */
    uint32 length;
    int usage;
    df_reference_t *refs; /* chain of references made *from* this function */
    int processed;

    df_function_t *funcnext; /* in forward functions order */
    df_function_t *todonext; /* in the todo chain */
    df_function_t *next; /* in the hash table */
};

struct df_reference_struct {
    uint32 address; /* function offset in zcode_area (not the final address) */
    int symbol; /* index in symbols array */

    df_reference_t *refsnext; /* in the function's refs chain */
    df_reference_t *next; /* in the hash table */
};

/* Bitmask flags for how functions are used: */
#define DF_USAGE_GLOBAL   (1<<0) /* In a global variable, array, etc */
#define DF_USAGE_EMBEDDED (1<<1) /* An anonymous function in a property */
#define DF_USAGE_MAIN     (1<<2) /* Main() or Main__() */
#define DF_USAGE_FUNCTION (1<<3) /* Used from another used function */

#define DF_FUNCTION_HASH_BUCKETS (1023)

/* Table of all compiled functions. (Only created if track_unused_routines
   is set.) This is a hash table. */
static df_function_t **df_functions;
/* List of all compiled functions, in address order. The first entry
   has address DF_NOT_IN_FUNCTION, and stands in for the global namespace. */
static df_function_t *df_functions_head;
static df_function_t *df_functions_tail;
/* Used during output_file(), to track how far the code-area output has
   gotten. */
static df_function_t *df_iterator;

/* Array of all compiled functions in address order. (Does not include
   the global namespace entry.) This is generated only if needed. */
static df_function_t **df_functions_sorted;
static int df_functions_sorted_count;

#define DF_NOT_IN_FUNCTION ((uint32)0xFFFFFFFF)
#define DF_SYMBOL_HASH_BUCKETS (4095)

/* Map of what functions reference what other functions. (Only created if
   track_unused_routines is set.) */
static df_reference_t **df_symbol_map;

/* Globals used while a function is being compiled. When a function
  *isn't* being compiled, df_current_function_addr will be DF_NOT_IN_FUNCTION
  and df_current_function will refer to the global namespace record. */
static df_function_t *df_current_function;
static char *df_current_function_name;
static uint32 df_current_function_addr;

/* Size totals for compiled code. These are only meaningful if
   track_unused_routines is true. (If we're only doing WARN_UNUSED_ROUTINES,
   these values will be set, but the "after" value will not affect the
   final game file.) */
uint32 df_total_size_before_stripping;
uint32 df_total_size_after_stripping;

/* When we begin compiling a function, call this to note that fact.
   Any symbol referenced from now on will be associated with the function.
*/
extern void df_note_function_start(char *name, uint32 address, 
    int embedded_flag, brief_location source_line)
{
    df_function_t *func;
    int bucket;

    if (df_tables_closed)
        error("Internal error in stripping: Tried to start a new function after tables were closed.");

    /* We retain the name only for debugging output. Note that embedded
       functions all show up as "<embedded>" -- their "obj.prop" name
       never gets stored in permanent memory. */
    df_current_function_name = name;
    df_current_function_addr = address;

    func = my_malloc(sizeof(df_function_t), "df function entry");
    memset(func, 0, sizeof(df_function_t));
    func->name = name;
    func->address = address;
    func->source_line = source_line;
    func->sysfile = (address == DF_NOT_IN_FUNCTION || is_systemfile());
    /* An embedded function is stored in an object property, so we
       consider it to be used a priori. */
    if (embedded_flag)
        func->usage |= DF_USAGE_EMBEDDED;

    if (!df_functions_head) {
        df_functions_head = func;
        df_functions_tail = func;
    }
    else {
        df_functions_tail->funcnext = func;
        df_functions_tail = func;
    }

    bucket = address % DF_FUNCTION_HASH_BUCKETS;
    func->next = df_functions[bucket];
    df_functions[bucket] = func;

    df_current_function = func;
}

/* When we're done compiling a function, call this. Any symbol referenced
   from now on will be associated with the global namespace.
*/
extern void df_note_function_end(uint32 endaddress)
{
    df_current_function->length = endaddress - df_current_function->address;

    df_current_function_name = NULL;
    df_current_function_addr = DF_NOT_IN_FUNCTION;
    df_current_function = df_functions_head; /* the global namespace */
}

/* Find the function record for a given address. (Addresses are offsets
   in zcode_area.)
*/
static df_function_t *df_function_for_address(uint32 address)
{
    int bucket = address % DF_FUNCTION_HASH_BUCKETS;
    df_function_t *func;
    for (func = df_functions[bucket]; func; func = func->next) {
        if (func->address == address)
            return func;
    }
    return NULL;
}

/* Whenever a function is referenced, we call this to note who called it.
*/
extern void df_note_function_symbol(int symbol)
{
    int bucket, symtype;
    df_reference_t *ent;

    /* If the compiler pass is over, looking up symbols does not create
       a global reference. */
    if (df_tables_closed)
        return;
    /* In certain cases during parsing, looking up symbols does not
       create a global reference. (For example, when reading the name
       of a function being defined.) */
    if (df_dont_note_global_symbols)
        return;
    /* If we're compiling an unreachable statement, no reference. */
    if (execution_never_reaches_here)
        return;

    /* We are only interested in functions, or forward-declared symbols
       that might turn out to be functions. */
    symtype = symbols[symbol].type;
    if (symtype != ROUTINE_T && symtype != CONSTANT_T)
        return;
    if (symtype == CONSTANT_T && !(symbols[symbol].flags & UNKNOWN_SFLAG))
        return;

    bucket = (df_current_function_addr ^ (uint32)symbol) % DF_SYMBOL_HASH_BUCKETS;
    for (ent = df_symbol_map[bucket]; ent; ent = ent->next) {
        if (ent->address == df_current_function_addr && ent->symbol == symbol)
            return;
    }

    /* Create a new reference entry in df_symbol_map. */
    ent = my_malloc(sizeof(df_reference_t), "df symbol map entry");
    ent->address = df_current_function_addr;
    ent->symbol = symbol;
    ent->next = df_symbol_map[bucket];
    df_symbol_map[bucket] = ent;

    /* Add the reference to the function's entry as well. */
    /* The current function is the most recently added, so it will be
       at the top of its bucket. That makes this call fast. Unless
       we're in global scope, in which case it might be slower.
       (I suppose we could cache the df_function_t pointer of the
       current function, to speed things up.) */
    if (!df_current_function || df_current_function_addr != df_current_function->address) {
        compiler_error("DF: df_current_function does not match current address.");
        return;
    }
    ent->refsnext = df_current_function->refs;
    df_current_function->refs = ent;
}

/* This does the hard work of figuring out what functions are truly dead.
   It's called near the end of run_pass() in inform.c.
*/
extern void locate_dead_functions(void)
{
    df_function_t *func, *tofunc;
    df_reference_t *ent;
    int ix;

    if (!track_unused_routines)
        compiler_error("DF: locate_dead_functions called, but function references have not been mapped");

    df_tables_closed = TRUE;
    df_current_function = NULL;

    /* Note that Main__ was tagged as global implicitly during
       compile_initial_routine(). Main was tagged during
       issue_unused_warnings(). But for the sake of thoroughness,
       we'll mark them specially. */

    ix = get_symbol_index("Main__");
    if (ix >= 0 && symbols[ix].type == ROUTINE_T) {
        uint32 addr = symbols[ix].value * (glulx_mode ? 1 : scale_factor);
        tofunc = df_function_for_address(addr);
        if (tofunc)
            tofunc->usage |= DF_USAGE_MAIN;
    }
    ix = get_symbol_index("Main");
    if (ix >= 0 && symbols[ix].type == ROUTINE_T) {
        uint32 addr = symbols[ix].value * (glulx_mode ? 1 : scale_factor);
        tofunc = df_function_for_address(addr);
        if (tofunc)
            tofunc->usage |= DF_USAGE_MAIN;
    }

    /* Go through all the functions referenced at the global level;
       mark them as used. */

    func = df_functions_head;
    if (!func || func->address != DF_NOT_IN_FUNCTION) {
        compiler_error("DF: Global namespace entry is not at the head of the chain.");
        return;
    }

    for (ent = func->refs; ent; ent=ent->refsnext) {
        uint32 addr;
        int symbol = ent->symbol;
        if (symbols[symbol].type != ROUTINE_T)
            continue;
        addr = symbols[symbol].value * (glulx_mode ? 1 : scale_factor);
        tofunc = df_function_for_address(addr);
        if (!tofunc) {
            error_named("Internal error in stripping: global ROUTINE_T symbol is not found in df_function map:", symbols[symbol].name);
            continue;
        }
        /* A function may be marked here more than once. That's fine. */
        tofunc->usage |= DF_USAGE_GLOBAL;
    }

    /* Perform a breadth-first search through functions, starting with
       the ones that are known to be used at the top level. */
    {
        df_function_t *todo, *todotail;
        df_function_t *func;
        todo = NULL;
        todotail = NULL;

        for (func = df_functions_head; func; func = func->funcnext) {
            if (func->address == DF_NOT_IN_FUNCTION)
                continue;
            if (func->usage == 0)
                continue;
            if (!todo) {
                todo = func;
                todotail = func;
            }
            else {
                todotail->todonext = func;
                todotail = func;
            }
        }
        
        /* todo is a linked list of functions which are known to be
           used. If a function's usage field is nonzero, it must be
           either be on the todo list or have come off already (in
           which case processed will be set). */

        while (todo) {
            /* Pop the next function. */
            func = todo;
            todo = todo->todonext;
            if (!todo)
                todotail = NULL;

            if (func->processed)
                error_named("Internal error in stripping: function has been processed twice:", func->name);

            /* Go through the function's symbol references. Any
               reference to a routine, push it into the todo list (if
               it isn't there already). */

            for (ent = func->refs; ent; ent=ent->refsnext) {
                uint32 addr;
                int symbol = ent->symbol;
                if (symbols[symbol].type != ROUTINE_T)
                    continue;
                addr = symbols[symbol].value * (glulx_mode ? 1 : scale_factor);
                tofunc = df_function_for_address(addr);
                if (!tofunc) {
                    error_named("Internal error in stripping: function ROUTINE_T symbol is not found in df_function map:", symbols[symbol].name);
                    continue;
                }
                if (tofunc->usage)
                    continue;

                /* Not yet known to be used. Add it to the todo list. */
                tofunc->usage |= DF_USAGE_FUNCTION;
                if (!todo) {
                    todo = tofunc;
                    todotail = tofunc;
                }
                else {
                    todotail->todonext = tofunc;
                    todotail = tofunc;
                }
            }

            func->processed = TRUE;
        }
    }

    /* Go through all functions; figure out how much space is consumed,
       with and without useless functions. */

    {
        df_function_t *func;

        df_total_size_before_stripping = 0;
        df_total_size_after_stripping = 0;

        for (func = df_functions_head; func; func = func->funcnext) {
            if (func->address == DF_NOT_IN_FUNCTION)
                continue;

            if (func->address != df_total_size_before_stripping)
                compiler_error("DF: Address gap in function list");

            df_total_size_before_stripping += func->length;
            if (func->usage) {
                func->newaddress = df_total_size_after_stripping;
                df_total_size_after_stripping += func->length;
            }

            if (!glulx_mode && (df_total_size_after_stripping % scale_factor != 0))
                compiler_error("DF: New function address is not aligned");

            if (WARN_UNUSED_ROUTINES && !func->usage) {
                if (!func->sysfile || WARN_UNUSED_ROUTINES >= 2)
                    uncalled_routine_warning("Routine", func->name, func->source_line);
            }
        }
    }

    /* df_measure_hash_table_usage(); */
}

/* Given an original function address, return where it winds up after
   unused-function stripping. The function must not itself be unused.

   Both the input and output are offsets, and already scaled by
   scale_factor.

   This is used by the backpatching system.
*/
extern uint32 df_stripped_address_for_address(uint32 addr)
{
    df_function_t *func;

    if (!track_unused_routines)
        compiler_error("DF: df_stripped_address_for_address called, but function references have not been mapped");

    if (!glulx_mode)
        func = df_function_for_address(addr*scale_factor);
    else
        func = df_function_for_address(addr);

    if (!func) {
        compiler_error("DF: Unable to find function while backpatching");
        return 0;
    }
    if (!func->usage)
        compiler_error("DF: Tried to backpatch a function address which should be stripped");

    if (!glulx_mode)
        return func->newaddress / scale_factor;
    else
        return func->newaddress;
}

/* Given an address in the function area, return where it winds up after
   unused-function stripping. The address can be a function or anywhere
   within the function. If the address turns out to be in a stripped
   function, returns 0 (and sets *stripped).

   The input and output are offsets, but *not* scaled.

   This is only used by the debug-file system.
*/
uint32 df_stripped_offset_for_code_offset(uint32 offset, int *stripped)
{
    df_function_t *func;
    int count;
    int beg;
    int end;

    if (!track_unused_routines)
        compiler_error("DF: df_stripped_offset_for_code_offset called, but function references have not been mapped");

    if (!df_functions_sorted) {
        /* To do this efficiently, we need a binary-searchable table. Fine,
           we'll make one. Include both used and unused functions. */

        for (func = df_functions_head, count = 0; func; func = func->funcnext) {
            if (func->address == DF_NOT_IN_FUNCTION)
                continue;
            count++;
        }
        df_functions_sorted_count = count;

        df_functions_sorted = my_calloc(sizeof(df_function_t *), df_functions_sorted_count, "df function sorted table");

        for (func = df_functions_head, count = 0; func; func = func->funcnext) {
            if (func->address == DF_NOT_IN_FUNCTION)
                continue;
            df_functions_sorted[count] = func;
            count++;
        }
    }

    /* Do a binary search. Maintain beg <= res < end, where res is the
       function containing the desired address. */
    beg = 0;
    end = df_functions_sorted_count;

    /* Set stripped flag until we decide on a non-stripped function. */
    *stripped = TRUE;

    while (1) {
        int new;
        if (beg >= end) {
            error("DF: offset_for_code_offset: Could not locate address.");
            return 0;
        }
        if (beg+1 == end) {
            func = df_functions_sorted[beg];
            if (func->usage == 0)
                return 0;
            *stripped = FALSE;
            return func->newaddress + (offset - func->address);
        }
        new = (beg + end) / 2;
        if (new <= beg || new >= end)
            compiler_error("DF: binary search went off the rails");

        func = df_functions_sorted[new];
        if (offset >= func->address) {
            if (offset < func->address+func->length) {
                /* We don't need to loop further; decide here. */
                if (func->usage == 0)
                    return 0;
                *stripped = FALSE;
                return func->newaddress + (offset - func->address);
            }
            beg = new;
        }
        else {
            end = new;
        }
    }
}

/* The output_file() routines in files.c have to run down the list of
   functions, deciding who is in and who is out. But I don't want to
   export the df_function_t list structure. Instead, I provide this
   silly iterator pair. Set it up with df_prepare_function_iterate();
   then repeatedly call df_next_function_iterate().
*/

extern void df_prepare_function_iterate(void)
{
    df_iterator = df_functions_head;
    if (!df_iterator || df_iterator->address != DF_NOT_IN_FUNCTION) {
        compiler_error("DF: Global namespace entry is not at the head of the chain.");
        return;
    }
    if (!df_iterator->funcnext || df_iterator->funcnext->address != 0) {
        compiler_error("DF: First function entry is not second in the chain.");
        return;
    }
}

/* This returns the end of the next function, and whether the next function
   is used (live).
*/
extern uint32 df_next_function_iterate(int *funcused)
{
    if (df_iterator)
        df_iterator = df_iterator->funcnext;
    if (!df_iterator) {
        *funcused = TRUE;
        return df_total_size_before_stripping+1;
    }
    *funcused = (df_iterator->usage != 0);
    return df_iterator->address + df_iterator->length;
}

/* ========================================================================= */
/*   Data structure management routines                                      */
/* ------------------------------------------------------------------------- */

extern void init_symbols_vars(void)
{
    symbols = NULL;
    start_of_list = NULL;
    symbol_debug_info = NULL;
    temp_symbol_buf = NULL;

    symbol_name_space_chunks = NULL;
    no_symbol_name_space_chunks = 0;
    symbols_free_space = NULL;
    symbols_ceiling = NULL;

    no_symbols = 0;

    symbol_replacements = NULL;
    symbol_replacements_count = 0;
    symbol_replacements_size = 0;

    make_case_conversion_grid();

    track_unused_routines = (WARN_UNUSED_ROUTINES || OMIT_UNUSED_ROUTINES);
    df_tables_closed = FALSE;
    df_symbol_map = NULL;
    df_functions = NULL;
    df_functions_head = NULL;
    df_functions_tail = NULL;
    df_current_function = NULL;
    df_functions_sorted = NULL;
    df_functions_sorted_count = 0;
}

extern void symbols_begin_pass(void) 
{
    df_total_size_before_stripping = 0;
    df_total_size_after_stripping = 0;
    df_dont_note_global_symbols = FALSE;
    df_iterator = NULL;
}

extern void symbols_allocate_arrays(void)
{
    initialise_memory_list(&symbols_memlist,
        sizeof(symbolinfo), 6400, (void**)&symbols,
        "symbols");
    if (debugfile_switch)
    {
        initialise_memory_list(&symbol_debug_info_memlist,
            sizeof(symboldebuginfo), 6400, (void**)&symbol_debug_info,
            "symbol debug backpatch info");
    }
    
    initialise_memory_list(&temp_symbol_buf_memlist,
        sizeof(char), 64, (void**)&temp_symbol_buf,
        "temporary symbol name");
        
    start_of_list = my_calloc(sizeof(int32), HASH_TAB_SIZE,
                     "hash code list beginnings");

    initialise_memory_list(&symbol_name_space_chunks_memlist,
        sizeof(char *), 32, (void**)&symbol_name_space_chunks,
        "symbol names chunk addresses");

    if (track_unused_routines) {
        df_tables_closed = FALSE;

        df_symbol_map = my_calloc(sizeof(df_reference_t *), DF_SYMBOL_HASH_BUCKETS, "df symbol-map hash table");
        memset(df_symbol_map, 0, sizeof(df_reference_t *) * DF_SYMBOL_HASH_BUCKETS);

        df_functions = my_calloc(sizeof(df_function_t *), DF_FUNCTION_HASH_BUCKETS, "df function hash table");
        memset(df_functions, 0, sizeof(df_function_t *) * DF_FUNCTION_HASH_BUCKETS);
        df_functions_head = NULL;
        df_functions_tail = NULL;

        df_functions_sorted = NULL;
        df_functions_sorted_count = 0;

        df_note_function_start("<global namespace>", DF_NOT_IN_FUNCTION, FALSE, blank_brief_location);
        df_note_function_end(DF_NOT_IN_FUNCTION);
        /* Now df_current_function is df_functions_head. */
    }

    init_symbol_banks();
    stockup_symbols();

    /*  Allocated as needed  */
    symbol_replacements = NULL;

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

    deallocate_memory_list(&symbol_name_space_chunks_memlist);

    deallocate_memory_list(&symbols_memlist);
    if (debugfile_switch)
    {
        deallocate_memory_list(&symbol_debug_info_memlist);
    }
    deallocate_memory_list(&temp_symbol_buf_memlist);
    
    my_free(&start_of_list, "hash code list beginnings");

    if (symbol_replacements)
        my_free(&symbol_replacements, "symbol replacement table");

    if (df_symbol_map) {
        for (i=0; i<DF_SYMBOL_HASH_BUCKETS; i++) {
            df_reference_t *ent = df_symbol_map[i];
            while (ent) {
                df_reference_t *next = ent->next;
                my_free(&ent, "df symbol map entry");
                ent = next;
            }
        }
        my_free(&df_symbol_map, "df symbol-map hash table");
    }
    if (df_functions_sorted) {
        my_free(&df_functions, "df function sorted table");
    }
    if (df_functions) {
        for (i=0; i<DF_FUNCTION_HASH_BUCKETS; i++) {
            df_function_t *func = df_functions[i];
            while (func) {
                df_function_t *next = func->next;
                my_free(&func, "df function entry");
                func = next;
            }
        }
        my_free(&df_functions, "df function hash table");
    }
    df_functions_head = NULL;
    df_functions_tail = NULL;

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

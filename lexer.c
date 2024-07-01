/* ------------------------------------------------------------------------- */
/*   "lexer" : Lexical analyser                                              */
/*                                                                           */
/*   Part of Inform 6.43                                                     */
/*   copyright (c) Graham Nelson 1993 - 2024                                 */
/*                                                                           */
/* ------------------------------------------------------------------------- */

#include "header.h"

int total_source_line_count,            /* Number of source lines so far     */

    no_hash_printed_yet,                /* Have not yet printed the first #  */
    hash_printed_since_newline,         /* A hash has been printed since the
                                           most recent new-line was printed
                                           (generally as a result of an error
                                           message or the start of pass)     */
    dont_enter_into_symbol_table,       /* Return names as text (with
                                           token type UQ_TT) and not as
                                           entries in the symbol table,
                                           when TRUE. If -2, only the
                                           keyword table is searched.        */
    return_sp_as_variable;              /* When TRUE, the word "sp" denotes
                                           the stack pointer variable
                                           (used in assembly language only)  */
int next_token_begins_syntax_line;      /* When TRUE, start a new syntax
                                           line (for error reporting, etc.)
                                           on the source code line where
                                           the next token appears            */

int32 last_mapped_line;  /* Last syntax line reported to debugging file      */

/* ------------------------------------------------------------------------- */
/*   The lexer's output is a sequence of structs, each called a "token",     */
/*   representing one lexical unit (or "lexeme") each.  Instead of providing */
/*   "lookahead" (that is, always having available the next token after the  */
/*   current one, so that syntax analysers higher up in Inform can have      */
/*   advance knowledge of what is coming), the lexer instead has a system    */
/*   where tokens can be read in and then "put back again".                  */
/*   The meaning of the number (and to some extent the text) supplied with   */
/*   a token depends on its type: see "header.h" for the list of types.      */
/*   For example, the lexeme "$1e3" is understood by Inform as a hexadecimal */
/*   number, and translated to the token:                                    */
/*     type NUMBER_TT, value 483, text "$1e3"                                */
/* ------------------------------------------------------------------------- */
/*   These three variables are set to the current token on a call to         */
/*   get_next_token() (but are not changed by a call to put_token_back()).   */
/*   (It would be tidier to use a token_data structure, rather than having   */
/*   get_next_token() unpack three values. But this is the way it is.)       */
/* ------------------------------------------------------------------------- */

int token_type;
int32 token_value;
char *token_text;

/* ------------------------------------------------------------------------- */
/*   The next two variables are the head and tail of a singly linked list.   */
/*   The tail stores the portion most recently read from the current         */
/*   lexical block; its end values therefore describe the location of the    */
/*   current token, and are updated whenever the three variables above are   */
/*   via set_token_location(...).  Earlier vertices, if any, represent the   */
/*   regions of lexical blocks read beforehand, where new vertices are       */
/*   only introduced by interruptions like a file inclusion or an EOF.       */
/*   Vertices are deleted off of the front of the list once they are no      */
/*   longer referenced by pending debug information records.                 */
/* ------------------------------------------------------------------------- */

static debug_locations *first_token_locations;
static debug_locations *last_token_location;

extern debug_location get_token_location(void)
{   debug_location result;
    debug_location *location = &(last_token_location->location);
    result.file_index = location->file_index;
    result.beginning_byte_index = location->end_byte_index;
    result.end_byte_index = location->end_byte_index;
    result.beginning_line_number = location->end_line_number;
    result.end_line_number = location->end_line_number;
    result.beginning_character_number = location->end_character_number;
    result.end_character_number = location->end_character_number;
    result.orig_file_index = location->orig_file_index;
    result.orig_beg_line_number = location->orig_beg_line_number;
    result.orig_beg_char_number = location->orig_beg_char_number;
    return result;
}

extern debug_locations get_token_locations(void)
{   debug_locations result;
    result.location = get_token_location();
    result.next = NULL;
    result.reference_count = 0;
    return result;
}

static void set_token_location(debug_location location)
{   if (location.file_index == last_token_location->location.file_index)
    {   last_token_location->location.end_byte_index =
            location.end_byte_index;
        last_token_location->location.end_line_number =
            location.end_line_number;
        last_token_location->location.end_character_number =
            location.end_character_number;
        last_token_location->location.orig_file_index =
            location.orig_file_index;
        last_token_location->location.orig_beg_line_number =
            location.orig_beg_line_number;
        last_token_location->location.orig_beg_char_number =
            location.orig_beg_char_number;
    } else
    {   debug_locations*successor =
            my_malloc
                (sizeof(debug_locations),
                 "debug locations of recent tokens");
        successor->location = location;
        successor->next = NULL;
        successor->reference_count = 0;
        last_token_location->next = successor;
        last_token_location = successor;
    }
}

extern debug_location_beginning get_token_location_beginning(void)
{   debug_location_beginning result;
    ++(last_token_location->reference_count);
    result.head = last_token_location;
    result.beginning_byte_index =
        last_token_location->location.end_byte_index;
    result.beginning_line_number =
        last_token_location->location.end_line_number;
    result.beginning_character_number =
        last_token_location->location.end_character_number;
    result.orig_file_index = last_token_location->location.orig_file_index;
    result.orig_beg_line_number = last_token_location->location.orig_beg_line_number;
    result.orig_beg_char_number = last_token_location->location.orig_beg_char_number;

    return result;
}

static void cleanup_token_locations(debug_location_beginning*beginning)
{   if (first_token_locations)
    {   while (first_token_locations &&
               !first_token_locations->reference_count)
        {   debug_locations*moribund = first_token_locations;
            first_token_locations = moribund->next;
            my_free(&moribund, "debug locations of recent tokens");
            if (beginning &&
                (beginning->head == moribund || !first_token_locations))
            {   compiler_error
                    ("Records needed by a debug_location_beginning are no "
                     "longer allocated, perhaps because of an invalid reuse "
                     "of this or an earlier beginning");
            }
        }
    } else
    {   if (beginning)
        {   compiler_error
                ("Attempt to use a debug_location_beginning when no token "
                 "locations are defined");
        } else
        {   compiler_error
                ("Attempt to clean up token locations when no token locations "
                 "are defined");
        }
    }
}

extern void discard_token_location(debug_location_beginning beginning)
{   --(beginning.head->reference_count);
}

extern debug_locations get_token_location_end
    (debug_location_beginning beginning)
{   debug_locations result;
    cleanup_token_locations(&beginning);
    --(beginning.head->reference_count);
    /* Sometimes we know what we'll read before we switch to the lexical block
       where we'll read it.  In such cases the beginning will be placed in the
       prior block and last exactly zero bytes there.  It's misleading to
       include such ranges, so we gobble them. */
    if (beginning.head->location.end_byte_index ==
          beginning.beginning_byte_index &&
        beginning.head->next)
    {   beginning.head = beginning.head->next;
        result.location = beginning.head->location;
        result.location.beginning_byte_index = 0;
        result.location.beginning_line_number = 1;
        result.location.beginning_character_number = 1;
    } else
    {   result.location = beginning.head->location;
        result.location.beginning_byte_index =
            beginning.beginning_byte_index;
        result.location.beginning_line_number =
            beginning.beginning_line_number;
        result.location.beginning_character_number =
            beginning.beginning_character_number;
    }

    result.location.orig_file_index =
        beginning.orig_file_index;
    result.location.orig_beg_line_number =
        beginning.orig_beg_line_number;
    result.location.orig_beg_char_number =
        beginning.orig_beg_char_number;
    
    result.next = beginning.head->next;
    result.reference_count = 0;
    return result;
}

/* ------------------------------------------------------------------------- */
/*   In order to be able to put tokens back efficiently, the lexer stores    */
/*   tokens in a "circle": the variable circle_position ranges between       */
/*   0 and CIRCLE_SIZE-1.  We only need a circle size as large as the        */
/*   maximum number of tokens ever put back at once, plus 1 (in effect, the  */
/*   maximum token lookahead ever needed in syntax analysis, plus 1).        */
/*                                                                           */
/*   Note that the circle struct type is lexeme_data, whereas the expression */
/*   code all works in token_data. They have slightly different needs. The   */
/*   data is exported through the token_text, token_value, token_type        */
/*   globals, so there's no need to use the same struct at both levels.      */
/*                                                                           */
/*   Unlike some compilers, Inform does not have a context-free lexer: in    */
/*   fact it has 12288 different possible states.  However, the context only */
/*   affects the interpretation of "identifiers": lexemes beginning with a   */
/*   letter and containing up to 32 chars of alphanumeric and underscore     */
/*   chars.  (For example, "default" may refer to the directive or statement */
/*   of that name, and which token values are returned depends on the        */
/*   current lexical context.)                                               */
/*                                                                           */
/*   Along with each token, we also store the lexical context it was         */
/*   translated under; because if it is called for again, there may need     */
/*   to be a fresh interpretation of it if the context has changed.          */
/* ------------------------------------------------------------------------- */

#define CIRCLE_SIZE 6

/*   (The worst case for token lookahead is distinguishing between an
     old-style "objectloop (a in b)" and a new "objectloop (a in b ...)".)   */

static int circle_position;
static lexeme_data circle[CIRCLE_SIZE];

/* ------------------------------------------------------------------------- */
/*   A complication, however, is that the text of some lexemes needs to be   */
/*   held in Inform's memory for much longer periods: for example, a         */
/*   dictionary word lexeme (like "'south'") must have its text preserved    */
/*   until the code generation time for the expression it occurs in, when    */
/*   the dictionary reference is actually made. We handle this by keeping    */
/*   all lexeme text until the end of the statement (or, for top-level       */
/*   directives, until the end of the directive). Then we call               */
/*   release_token_texts() to start over. The lextexts array will therefore  */
/*   grow to the largest number of lexemes in a single statement or          */
/*   directive.                                                              */
/* ------------------------------------------------------------------------- */

typedef struct lextext_s {
    char *text;
    size_t size; /* Allocated size (including terminal null)                 */
} lextext;

static lextext *lextexts; /* Allocated to no_lextexts */
static memory_list lextexts_memlist;
static int no_lextexts;

static int cur_lextexts;    /* Number of lextexts in current use
                               (cur_lextexts <= no_lextexts)                 */

static int lex_index;       /* Index of lextext being written to             */
static int lex_pos;         /* Current write position in that lextext        */

/* ------------------------------------------------------------------------- */
/*   The lexer itself needs up to 3 characters of lookahead (it uses an      */
/*   LR(3) grammar to translate characters into tokens).                     */
/*                                                                           */
/*   Past the end of the stream, we fill in zeros. This has the awkward      */
/*   side effect that a zero byte in a source file will silently terminate   */
/*   it, rather than producing an "illegal source character" error.          */
/*   On the up side, we can compile veneer routines (which are null-         */
/*   terminated strings) with no extra work.                                 */
/* ------------------------------------------------------------------------- */

#define LOOKAHEAD_SIZE 3

static int current, lookahead,          /* The latest character read, and    */
    lookahead2, lookahead3;             /* the three characters following it */
                                        /* (zero means end-of-stream)        */

static int pipeline_made;               /* Whether or not the pipeline of
                                           characters has been constructed
                                           yet (this pass)                   */

static int (* get_next_char)(void);     /* Routine for reading the stream of
                                           characters: the lexer does not
                                           need any "ungetc" routine for
                                           putting them back again.  End of
                                           stream is signalled by returning
                                           zero.                             */

static char *source_to_analyse;         /* The current lexical source:
                                           NULL for "load from source files",
                                           otherwise this points to a string
                                           containing Inform code            */

static int tokens_put_back;             /* Count of the number of backward
                                           moves made from the last-read
                                           token                             */

/* This gets called for both token_data and lexeme_data structs. It prints
   a description of the common part (the text, value, type fields). 
*/
extern void describe_token_triple(const char *text, int32 value, int type)
{
    /*  Many of the token types are not set in this file, but later on in
        Inform's higher stages (for example, in the expression evaluator);
        but this routine describes them all.                                 */

    printf("{ ");

    switch(type)
    {
        /*  The following token types occur in lexer output:                 */

        case SYMBOL_TT:          printf("symbol ");
                                 describe_symbol(value);
                                 break;
        case NUMBER_TT:          printf("literal number %d", value);
                                 break;
        case DQ_TT:              printf("string \"%s\"", text);
                                 break;
        case SQ_TT:              printf("string '%s'", text);
                                 break;
        case UQ_TT:              printf("barestring %s", text);
                                 break;
        case SEP_TT:             printf("separator '%s'", text);
                                 break;
        case EOF_TT:             printf("end of file");
                                 break;

        case STATEMENT_TT:       printf("statement name '%s'", text);
                                 break;
        case SEGMENT_MARKER_TT:  printf("object segment marker '%s'", text);
                                 break;
        case DIRECTIVE_TT:       printf("directive name '%s'", text);
                                 break;
        case CND_TT:             printf("textual conditional '%s'", text);
                                 break;
        case OPCODE_NAME_TT:     printf("opcode name '%s'", text);
                                 break;
        case SYSFUN_TT:          printf("built-in function name '%s'", text);
                                 break;
        case LOCAL_VARIABLE_TT:  printf("local variable name '%s'", text);
                                 break;
        case MISC_KEYWORD_TT:    printf("statement keyword '%s'", text);
                                 break;
        case DIR_KEYWORD_TT:     printf("directive keyword '%s'", text);
                                 break;
        case TRACE_KEYWORD_TT:   printf("'trace' keyword '%s'", text);
                                 break;
        case SYSTEM_CONSTANT_TT: printf("system constant name '%s'", text);
                                 break;

        /*  The remaining are etoken types, not set by the lexer             */

        case OP_TT:              printf("operator '%s'",
                                     operators[value].description);
                                 break;
        case ENDEXP_TT:          printf("end of expression");
                                 break;
        case SUBOPEN_TT:         printf("open bracket");
                                 break;
        case SUBCLOSE_TT:        printf("close bracket");
                                 break;
        case LARGE_NUMBER_TT:    printf("large number: '%s'=%d",text,value);
                                 break;
        case SMALL_NUMBER_TT:    printf("small number: '%s'=%d",text,value);
                                 break;
        case VARIABLE_TT:        printf("variable '%s'=%d", text, value);
                                 break;
        case DICTWORD_TT:        printf("dictionary word '%s'", text);
                                 break;
        case ACTION_TT:          printf("action name '%s'", text);
                                 break;

        default:
            printf("** unknown token type %d, text='%s', value=%d **",
            type, text, value);
    }
    printf(" }");
}

/* ------------------------------------------------------------------------- */
/*   All but one of the Inform keywords (most of them opcode names used      */
/*   only by the assembler).  (The one left over is "sp", a keyword used in  */
/*   assembly language only.)                                                */
/*                                                                           */
/*   A "keyword group" is a set of keywords to be searched for.  If a match  */
/*   is made on an identifier, the token type becomes that given in the KG   */
/*   and the token value is its index in the KG.                             */
/*                                                                           */
/*   The keyword ordering must correspond with the appropriate #define's in  */
/*   "header.h" but is otherwise not significant.                            */
/* ------------------------------------------------------------------------- */

/* This must exceed the total number of keywords across all groups, 
   including opcodes. */
#define MAX_KEYWORDS (500)

/* The values will be filled in at compile time, when we know
   which opcode set to use. */
keyword_group opcode_names =
{ { "" },
    OPCODE_NAME_TT, FALSE, TRUE
};

static char *opcode_list_z[] = {
    "je", "jl", "jg", "dec_chk", "inc_chk", "jin", "test", "or", "and",
    "test_attr", "set_attr", "clear_attr", "store", "insert_obj", "loadw",
    "loadb", "get_prop", "get_prop_addr", "get_next_prop", "add", "sub",
    "mul", "div", "mod", "call", "storew", "storeb", "put_prop", "sread",
    "print_char", "print_num", "random", "push", "pull", "split_window",
    "set_window", "output_stream", "input_stream", "sound_effect", "jz",
    "get_sibling", "get_child", "get_parent", "get_prop_len", "inc", "dec",
    "print_addr", "remove_obj", "print_obj", "ret", "jump", "print_paddr",
    "load", "not", "rtrue", "rfalse", "print", "print_ret", "nop", "save",
    "restore", "restart", "ret_popped", "pop", "quit", "new_line",
    "show_status", "verify", "call_2s", "call_vs", "aread", "call_vs2",
    "erase_window", "erase_line", "set_cursor", "get_cursor",
    "set_text_style", "buffer_mode", "read_char", "scan_table", "call_1s",
    "call_2n", "set_colour", "throw", "call_vn", "call_vn2", "tokenise",
    "encode_text", "copy_table", "print_table", "check_arg_count", "call_1n",
    "catch", "piracy", "log_shift", "art_shift", "set_font", "save_undo",
    "restore_undo", "draw_picture", "picture_data", "erase_picture",
    "set_margins", "move_window", "window_size", "window_style",
    "get_wind_prop", "scroll_window", "pop_stack", "read_mouse",
    "mouse_window", "push_stack", "put_wind_prop", "print_form",
    "make_menu", "picture_table", "print_unicode", "check_unicode",
    "set_true_colour", "buffer_screen",
    ""
};

static char *opcode_list_g[] = {
    "nop", "add", "sub", "mul", "div", "mod", "neg", "bitand", "bitor",
    "bitxor", "bitnot", "shiftl", "sshiftr", "ushiftr", "jump", "jz",
    "jnz", "jeq", "jne", "jlt", "jge", "jgt", "jle", 
    "jltu", "jgeu", "jgtu", "jleu", 
    "call", "return",
    "catch", "throw", "tailcall", 
    "copy", "copys", "copyb", "sexs", "sexb", "aload",
    "aloads", "aloadb", "aloadbit", "astore", "astores", "astoreb",
    "astorebit", "stkcount", "stkpeek", "stkswap", "stkroll", "stkcopy",
    "streamchar", "streamnum", "streamstr", 
    "gestalt", "debugtrap", "getmemsize", "setmemsize", "jumpabs",
    "random", "setrandom", "quit", "verify",
    "restart", "save", "restore", "saveundo", "restoreundo", "protect",
    "glk", "getstringtbl", "setstringtbl", "getiosys", "setiosys",
    "linearsearch", "binarysearch", "linkedsearch",
    "callf", "callfi", "callfii", "callfiii", 
    "streamunichar",
    "mzero", "mcopy", "malloc", "mfree",
    "accelfunc", "accelparam",
    "hasundo", "discardundo",
    "numtof", "ftonumz", "ftonumn", "ceil", "floor",
    "fadd", "fsub", "fmul", "fdiv", "fmod",
    "sqrt", "exp", "log", "pow",
    "sin", "cos", "tan", "asin", "acos", "atan", "atan2",
    "jfeq", "jfne", "jflt", "jfle", "jfgt", "jfge", "jisnan", "jisinf",
    "numtod", "dtonumz", "dtonumn", "ftod", "dtof", "dceil", "dfloor",
    "dadd", "dsub", "dmul", "ddiv", "dmodr", "dmodq",
    "dsqrt", "dexp", "dlog", "dpow",
    "dsin", "dcos", "dtan", "dasin", "dacos", "datan", "datan2",
    "jdeq", "jdne", "jdlt", "jdle", "jdgt", "jdge", "jdisnan", "jdisinf",
    ""
};

keyword_group opcode_macros =
{ { "" },
  OPCODE_MACRO_TT, FALSE, TRUE
};

static char *opmacro_list_z[] = { "" };

static char *opmacro_list_g[] = {
    "pull", "push", "dload", "dstore",
    ""
};

keyword_group directives =
{ { "abbreviate", "array", "attribute", "class", "constant",
    "default", "dictionary", "end", "endif", "extend", "fake_action",
    "global", "ifdef", "ifndef", "ifnot", "ifv3", "ifv5", "iftrue",
    "iffalse", "import", "include", "link", "lowstring", "message",
    "nearby", "object", "origsource", "property", "release", "replace",
    "serial", "switches", "statusline", "stub", "system_file", "trace",
    "undef", "verb", "version", "zcharacter",
    "" },
    DIRECTIVE_TT, FALSE, FALSE
};

keyword_group trace_keywords =
{ { "dictionary", "symbols", "objects", "verbs",
    "assembly", "expressions", "lines", "tokens", "linker",
    "on", "off", "" },
    TRACE_KEYWORD_TT, FALSE, TRUE
};

keyword_group segment_markers =
{ { "class", "has", "private", "with", "" },
    SEGMENT_MARKER_TT, FALSE, TRUE
};

keyword_group directive_keywords =
{ { "alias", "long", "additive",
    "score", "time",
    "noun", "held", "multi", "multiheld", "multiexcept",
    "multiinside", "creature", "special", "number", "scope", "topic",
    "reverse", "meta", "only", "replace", "first", "last",
    "string", "table", "buffer", "data", "initial", "initstr",
    "with", "private", "has", "class",
    "error", "fatalerror", "warning",
    "terminating", "static", "individual",
    "" },
    DIR_KEYWORD_TT, FALSE, TRUE
};

keyword_group misc_keywords =
{ { "char", "name", "the", "a", "an", "The", "number",
    "roman", "reverse", "bold", "underline", "fixed", "on", "off",
    "to", "address", "string", "object", "near", "from", "property", "A", "" },
    MISC_KEYWORD_TT, FALSE, TRUE
};

keyword_group statements =
{ { "box", "break", "continue", "default", "do", "else", "font", "for",
    "give", "if", "inversion", "jump", "move", "new_line", "objectloop",
    "print", "print_ret", "quit", "read", "remove", "restore", "return",
    "rfalse", "rtrue", "save", "spaces", "string", "style", "switch",
    "until", "while", "" },
    STATEMENT_TT, FALSE, TRUE
};

keyword_group conditions =
{ { "has", "hasnt", "in", "notin", "ofclass", "or", "provides", "" },
    CND_TT, FALSE, TRUE
};

keyword_group system_functions =
{ { "child", "children", "elder", "eldest", "indirect", "parent", "random",
    "sibling", "younger", "youngest", "metaclass", "glk", "" },
    SYSFUN_TT, FALSE, TRUE
};

keyword_group system_constants =
{ { "adjectives_table", "actions_table", "classes_table",
    "identifiers_table", "preactions_table", "version_number",
    "largest_object", "strings_offset", "code_offset",
    "dict_par1", "dict_par2", "dict_par3", "actual_largest_object",
    "static_memory_offset", "array_names_offset", "readable_memory_offset",
    "cpv__start", "cpv__end", "ipv__start", "ipv__end",
    "array__start", "array__end",
    "lowest_attribute_number", "highest_attribute_number",
    "attribute_names_array",
    "lowest_property_number", "highest_property_number",
    "property_names_array",
    "lowest_action_number", "highest_action_number",
    "action_names_array",
    "lowest_fake_action_number", "highest_fake_action_number",
    "fake_action_names_array",
    "lowest_routine_number", "highest_routine_number", "routines_array",
    "routine_names_array", "routine_flags_array",
    "lowest_global_number", "highest_global_number", "globals_array",
    "global_names_array", "global_flags_array",
    "lowest_array_number", "highest_array_number", "arrays_array",
    "array_names_array", "array_flags_array",
    "lowest_constant_number", "highest_constant_number", "constants_array",
    "constant_names_array",
    "lowest_class_number", "highest_class_number", "class_objects_array",
    "lowest_object_number", "highest_object_number",
    "oddeven_packing",
    "grammar_table", "dictionary_table", "dynam_string_table",
    "highest_meta_action_number",
    "" },
    SYSTEM_CONSTANT_TT, FALSE, TRUE
};

keyword_group *keyword_groups[12]
= { NULL, &opcode_names, &directives, &trace_keywords, &segment_markers,
    &directive_keywords, &misc_keywords, &statements, &conditions,
    &system_functions, &system_constants, &opcode_macros};

/* These keywords are set to point to local_variable_names entries when
   a routine header is parsed. See construct_local_variable_tables().        */
keyword_group local_variables =
{ { "" },
    LOCAL_VARIABLE_TT, FALSE, FALSE
};

static int lexical_context(void)
{
    /*  The lexical context is a number representing all of the context
        information in the lexical analyser: the same input text will
        always translate to the same output tokens whenever the context
        is the same.

        (For many years, the "dont_enter_into_symbol_table" variable
        was omitted from this number. But now we can include it.)            */

    int c = 0;
    if (opcode_names.enabled)         c |= 1;
    if (directives.enabled)           c |= 2;
    if (trace_keywords.enabled)       c |= 4;
    if (segment_markers.enabled)      c |= 8;
    if (directive_keywords.enabled)   c |= 16;
    if (misc_keywords.enabled)        c |= 32;
    if (statements.enabled)           c |= 64;
    if (conditions.enabled)           c |= 128;
    if (system_functions.enabled)     c |= 256;
    if (system_constants.enabled)     c |= 512;
    if (local_variables.enabled)      c |= 1024;

    if (return_sp_as_variable)        c |= 2048;
    if (dont_enter_into_symbol_table) c |= 4096;
    
    return(c);
}

static void print_context(int c)
{
    if (c < 0) {
        printf("??? ");
        return;
    }
    if ((c & 1) != 0) printf("OPC ");
    if ((c & 2) != 0) printf("DIR ");
    if ((c & 4) != 0) printf("TK ");
    if ((c & 8) != 0) printf("SEG ");
    if ((c & 16) != 0) printf("DK ");
    if ((c & 32) != 0) printf("MK ");
    if ((c & 64) != 0) printf("STA ");
    if ((c & 128) != 0) printf("CND ");
    if ((c & 256) != 0) printf("SFUN ");
    if ((c & 512) != 0) printf("SCON ");
    if ((c & 1024) != 0) printf("LV ");
    if ((c & 2048) != 0) printf("sp ");
    if ((c & 4096) != 0) printf("dontent ");
}

static int *keywords_hash_table;
static int *keywords_hash_ends_table;
static int *keywords_data_table;

static int *local_variable_hash_table;
static int *local_variable_hash_codes;

/* Note that MAX_LOCAL_VARIABLES is the maximum number of local variables
   for this VM, *including* "sp" (the stack pointer "local").
   This used to be a memory setting. Now it is a constant: 16 for Z-code,
   119 for Glulx.
*/

/* The number of local variables in the current routine. */
int no_locals;

/* Names of local variables in the current routine.
   The values are positions in local_variable_names_memlist.
   This is allocated to MAX_LOCAL_VARIABLES-1. (The stack pointer "local"
   is not included in this array.)

   (This could be a memlist, growing as needed up to MAX_LOCAL_VARIABLES-1.
   But right now we just allocate the max.)
 */
int *local_variable_name_offsets;

static memory_list local_variable_names_memlist;
/* How much of local_variable_names_memlist is used by the no_local locals. */
static int local_variable_names_usage;

static char one_letter_locals[128];

static void make_keywords_tables(void)
{   int i, j, h, tp=0;
    char **oplist, **maclist;

    if (!glulx_mode) {
        oplist = opcode_list_z;
        maclist = opmacro_list_z;
    }
    else {
        oplist = opcode_list_g;
        maclist = opmacro_list_g;
    }

    for (j=0; *(oplist[j]); j++) {
        if (j >= MAX_KEYWORD_GROUP_SIZE) {
            /* Gotta increase MAX_KEYWORD_GROUP_SIZE */
            compiler_error("opcode_list has overflowed opcode_names.keywords");
            break;
        }
        opcode_names.keywords[j] = oplist[j];
    }
    opcode_names.keywords[j] = "";
    
    for (j=0; *(maclist[j]); j++) {
        if (j >= MAX_KEYWORD_GROUP_SIZE) {
            /* Gotta increase MAX_KEYWORD_GROUP_SIZE */
            compiler_error("opmacro_list has overflowed opcode_macros.keywords");
            break;
        }
        opcode_macros.keywords[j] = maclist[j];
    }
    opcode_macros.keywords[j] = "";

    for (i=0; i<HASH_TAB_SIZE; i++)
    {   keywords_hash_table[i] = -1;
        keywords_hash_ends_table[i] = -1;
    }

    for (i=1; i<=11; i++)
    {   keyword_group *kg = keyword_groups[i];
        for (j=0; *(kg->keywords[j]) != 0; j++)
        {
            if (tp >= MAX_KEYWORDS) {
                /* Gotta increase MAX_KEYWORDS */
                compiler_error("keywords_data_table has overflowed MAX_KEYWORDS");
                break;
            }
            h = hash_code_from_string(kg->keywords[j]);
            if (keywords_hash_table[h] == -1)
                keywords_hash_table[h] = tp;
            else
              *(keywords_data_table + 3*(keywords_hash_ends_table[h]) + 2) = tp;
            keywords_hash_ends_table[h] = tp;
            *(keywords_data_table + 3*tp) = i;
            *(keywords_data_table + 3*tp+1) = j;
            *(keywords_data_table + 3*tp+2) = -1;
            tp++;
        }
    }
}

extern void clear_local_variables(void)
{
    no_locals = 0;
    local_variable_names_usage = 0;
}

extern void add_local_variable(char *name)
{
    int len;

    if (no_locals >= MAX_LOCAL_VARIABLES-1) {
        /* This should have been caught before we got here */
        error("too many local variables");
        return;
    }
    
    len = strlen(name)+1;
    ensure_memory_list_available(&local_variable_names_memlist, local_variable_names_usage + len);
    local_variable_name_offsets[no_locals++] = local_variable_names_usage;
    strcpy((char *)local_variable_names_memlist.data+local_variable_names_usage, name);
    local_variable_names_usage += len;
}

extern char *get_local_variable_name(int index)
{
    if (index < 0 || index >= no_locals)
        return "???";   /* shouldn't happen */

    return (char *)local_variable_names_memlist.data + local_variable_name_offsets[index];
}

/* Look at the strings stored in local_variable_names (from 0 to no_locals).
   Set local_variables.keywords to point to these, and also prepare the
   hash tables.
   This must be called after add_local_variable(), but before we start
   compiling function code. */
extern void construct_local_variable_tables(void)
{   int i, h;
    for (i=0; i<HASH_TAB_SIZE; i++) local_variable_hash_table[i] = -1;
    for (i=0; i<128; i++) one_letter_locals[i] = MAX_LOCAL_VARIABLES;

    for (i=0; i<no_locals; i++)
    {
        char *p = (char *)local_variable_names_memlist.data + local_variable_name_offsets[i];
        local_variables.keywords[i] = p;
        if (p[1] == 0)
        {   one_letter_locals[(uchar)p[0]] = i;
            if (isupper((uchar)p[0])) one_letter_locals[tolower((uchar)p[0])] = i;
            if (islower((uchar)p[0])) one_letter_locals[toupper((uchar)p[0])] = i;
        }
        h = hash_code_from_string(p);
        if (local_variable_hash_table[h] == -1)
            local_variable_hash_table[h] = i;
        local_variable_hash_codes[i] = h;
    }
    /* Clear the rest. */
    for (;i<MAX_LOCAL_VARIABLES-1;i++) {
        local_variables.keywords[i] = "";
        local_variable_hash_codes[i] = 0;
    }
}

static void interpret_identifier(char *p, int pos)
{   int index, hashcode;

    /*  An identifier is either a keyword or a "symbol", a name which the
        lexical analyser leaves to higher levels of Inform to understand.    */

    circle[pos].newsymbol = FALSE;
    
    hashcode = hash_code_from_string(p);

    /*  If dont_enter_into_symbol_table is true, we skip all keywords
        (and variables) and just mark the name as an unquoted string.
        Except that if dont_enter_into_symbol_table is -2, we recognize
        directive keywords (only).
    */

    if (dont_enter_into_symbol_table) {

        if (dont_enter_into_symbol_table == -2) {
            /* This is a simplified version of the keyword-checking loop
               below. */
            index = keywords_hash_table[hashcode];
            while (index >= 0)
            {   int *i = keywords_data_table + 3*index;
                keyword_group *kg = keyword_groups[*i];
                if (kg == &directives)
                {   char *q = kg->keywords[*(i+1)];
                    if (((kg->case_sensitive) && (strcmp(p, q)==0))
                        || ((!(kg->case_sensitive)) && (strcmpcis(p, q)==0)))
                    {   circle[pos].type = kg->change_token_type;
                        circle[pos].value = *(i+1);
                        return;
                    }
                }
                index = *(i+2);
            }
        }
        
        circle[pos].type = UQ_TT;
        circle[pos].value = 0;
        return;
    }
    
    /*  If this is assembly language, perhaps it is "sp"?                    */

    if (return_sp_as_variable && (p[0]=='s') && (p[1]=='p') && (p[2]==0))
    {   circle[pos].value = 0; circle[pos].type = LOCAL_VARIABLE_TT;
        return;
    }

    /*  Test for local variables first, quite quickly.                       */

    if (local_variables.enabled)
    {   if (p[1]==0)
        {   index = one_letter_locals[(uchar)p[0]];
            if (index<MAX_LOCAL_VARIABLES)
            {   circle[pos].type = LOCAL_VARIABLE_TT;
                circle[pos].value = index+1;
                return;
            }
        }
        index = local_variable_hash_table[hashcode];
        if (index >= 0)
        {   for (;index<no_locals;index++)
            {   if (hashcode == local_variable_hash_codes[index])
                {
                    char *locname = (char *)local_variable_names_memlist.data + local_variable_name_offsets[index];
                    if (strcmpcis(p, locname)==0)
                    {   circle[pos].type = LOCAL_VARIABLE_TT;
                        circle[pos].value = index+1;
                        return;
                    }
                }
            }
        }
    }

    /*  Now the bulk of the keywords.  Note that the lexer doesn't recognise
        the name of a system function which has been Replaced.               */

    index = keywords_hash_table[hashcode];
    while (index >= 0)
    {   int *i = keywords_data_table + 3*index;
        keyword_group *kg = keyword_groups[*i];
        if (kg->enabled)
        {   char *q = kg->keywords[*(i+1)];
            if (((kg->case_sensitive) && (strcmp(p, q)==0))
                || ((!(kg->case_sensitive)) && (strcmpcis(p, q)==0)))
            {   if ((kg != &system_functions)
                    || (system_function_usage[*(i+1)]!=2))
                {   circle[pos].type = kg->change_token_type;
                    circle[pos].value = *(i+1);
                    return;
                }
            }
        }
        index = *(i+2);
    }

    /*  Search for the name; create it if necessary.                         */

    circle[pos].value = symbol_index(p, hashcode, &circle[pos].newsymbol);
    circle[pos].type = SYMBOL_TT;
}


/* ------------------------------------------------------------------------- */
/*   The tokeniser grid aids a rapid decision about the consequences of a    */
/*   character reached in the buffer.  In effect it is an efficiently stored */
/*   transition table using an algorithm similar to that of S. C. Johnson's  */
/*   "yacc" lexical analyser (see Aho, Sethi and Ullman, section 3.9).       */
/*   My thanks to Dilip Sequeira for suggesting this.                        */
/*                                                                           */
/*       tokeniser_grid[c]   is (16*n + m) if c is the first character of    */
/*                               separator numbers n, n+1, ..., n+m-1        */
/*                           or certain special values (QUOTE_CODE, etc)     */
/*                           or 0 otherwise                                  */
/*                                                                           */
/*   Since 1000/16 = 62, the code numbers below will need increasing if the  */
/*   number of separators supported exceeds 61.                              */
/* ------------------------------------------------------------------------- */

static int tokeniser_grid[256];

#define QUOTE_CODE      1000
#define DQUOTE_CODE     1001
#define NULL_CODE       1002
#define SPACE_CODE      1003
#define NEGATIVE_CODE   1004
#define DIGIT_CODE      1005
#define RADIX_CODE      1006
#define KEYWORD_CODE    1007
#define EOF_CODE        1008
#define WHITESPACE_CODE 1009
#define COMMENT_CODE    1010
#define IDENTIFIER_CODE 1011

/*  This list cannot safely be changed without also changing the header
    separator #defines.  The ordering is significant in that (i) all entries
    beginning with the same character must be adjacent and (ii) that if
    X is a an initial substring of Y then X must come before Y.

    E.g. --> must occur before -- to prevent "-->0" being tokenised
    wrongly as "--", ">", "0" rather than "-->", "0".                        */

static const char separators[NUMBER_SEPARATORS][4] =
{   "->", "-->", "--", "-", "++", "+", "*", "/", "%",
    "||", "|", "&&", "&", "~~",
    "~=", "~", "==", "=", ">=", ">",
    "<=", "<", "(", ")", ",",
    ".&", ".#", "..&", "..#", "..", ".",
    "::", ":", "@", ";", "[", "]", "{", "}",
    "$", "?~", "?",
    "#a$", "#g$", "#n$", "#r$", "#w$", "##", "#"
};

static void make_tokeniser_grid(void)
{
    /*  Construct the grid to the specification above.                       */

    int i, j;

    for (i=0; i<256; i++) tokeniser_grid[i]=0;

    for (i=0; i<NUMBER_SEPARATORS; i++)
    {   j=separators[i][0];
        if (tokeniser_grid[j]==0)
            tokeniser_grid[j]=i*16+1; else tokeniser_grid[j]++;
    }
    tokeniser_grid['\''] = QUOTE_CODE;
    tokeniser_grid['\"'] = DQUOTE_CODE;
    tokeniser_grid[0]    = EOF_CODE;
    tokeniser_grid[' ']  = WHITESPACE_CODE;
    tokeniser_grid['\n'] = WHITESPACE_CODE;
    tokeniser_grid['\r'] = WHITESPACE_CODE;
    tokeniser_grid['$']  = RADIX_CODE;
    tokeniser_grid['!']  = COMMENT_CODE;

    tokeniser_grid['0']  = DIGIT_CODE;
    tokeniser_grid['1']  = DIGIT_CODE;
    tokeniser_grid['2']  = DIGIT_CODE;
    tokeniser_grid['3']  = DIGIT_CODE;
    tokeniser_grid['4']  = DIGIT_CODE;
    tokeniser_grid['5']  = DIGIT_CODE;
    tokeniser_grid['6']  = DIGIT_CODE;
    tokeniser_grid['7']  = DIGIT_CODE;
    tokeniser_grid['8']  = DIGIT_CODE;
    tokeniser_grid['9']  = DIGIT_CODE;

    tokeniser_grid['a']  = IDENTIFIER_CODE;
    tokeniser_grid['b']  = IDENTIFIER_CODE;
    tokeniser_grid['c']  = IDENTIFIER_CODE;
    tokeniser_grid['d']  = IDENTIFIER_CODE;
    tokeniser_grid['e']  = IDENTIFIER_CODE;
    tokeniser_grid['f']  = IDENTIFIER_CODE;
    tokeniser_grid['g']  = IDENTIFIER_CODE;
    tokeniser_grid['h']  = IDENTIFIER_CODE;
    tokeniser_grid['i']  = IDENTIFIER_CODE;
    tokeniser_grid['j']  = IDENTIFIER_CODE;
    tokeniser_grid['k']  = IDENTIFIER_CODE;
    tokeniser_grid['l']  = IDENTIFIER_CODE;
    tokeniser_grid['m']  = IDENTIFIER_CODE;
    tokeniser_grid['n']  = IDENTIFIER_CODE;
    tokeniser_grid['o']  = IDENTIFIER_CODE;
    tokeniser_grid['p']  = IDENTIFIER_CODE;
    tokeniser_grid['q']  = IDENTIFIER_CODE;
    tokeniser_grid['r']  = IDENTIFIER_CODE;
    tokeniser_grid['s']  = IDENTIFIER_CODE;
    tokeniser_grid['t']  = IDENTIFIER_CODE;
    tokeniser_grid['u']  = IDENTIFIER_CODE;
    tokeniser_grid['v']  = IDENTIFIER_CODE;
    tokeniser_grid['w']  = IDENTIFIER_CODE;
    tokeniser_grid['x']  = IDENTIFIER_CODE;
    tokeniser_grid['y']  = IDENTIFIER_CODE;
    tokeniser_grid['z']  = IDENTIFIER_CODE;

    tokeniser_grid['A']  = IDENTIFIER_CODE;
    tokeniser_grid['B']  = IDENTIFIER_CODE;
    tokeniser_grid['C']  = IDENTIFIER_CODE;
    tokeniser_grid['D']  = IDENTIFIER_CODE;
    tokeniser_grid['E']  = IDENTIFIER_CODE;
    tokeniser_grid['F']  = IDENTIFIER_CODE;
    tokeniser_grid['G']  = IDENTIFIER_CODE;
    tokeniser_grid['H']  = IDENTIFIER_CODE;
    tokeniser_grid['I']  = IDENTIFIER_CODE;
    tokeniser_grid['J']  = IDENTIFIER_CODE;
    tokeniser_grid['K']  = IDENTIFIER_CODE;
    tokeniser_grid['L']  = IDENTIFIER_CODE;
    tokeniser_grid['M']  = IDENTIFIER_CODE;
    tokeniser_grid['N']  = IDENTIFIER_CODE;
    tokeniser_grid['O']  = IDENTIFIER_CODE;
    tokeniser_grid['P']  = IDENTIFIER_CODE;
    tokeniser_grid['Q']  = IDENTIFIER_CODE;
    tokeniser_grid['R']  = IDENTIFIER_CODE;
    tokeniser_grid['S']  = IDENTIFIER_CODE;
    tokeniser_grid['T']  = IDENTIFIER_CODE;
    tokeniser_grid['U']  = IDENTIFIER_CODE;
    tokeniser_grid['V']  = IDENTIFIER_CODE;
    tokeniser_grid['W']  = IDENTIFIER_CODE;
    tokeniser_grid['X']  = IDENTIFIER_CODE;
    tokeniser_grid['Y']  = IDENTIFIER_CODE;
    tokeniser_grid['Z']  = IDENTIFIER_CODE;

    tokeniser_grid['_']  = IDENTIFIER_CODE;
}

/* ------------------------------------------------------------------------- */
/*   Definition of a lexical block: a source file or a string containing     */
/*   text for lexical analysis; an independent source from the point of      */
/*   view of issuing error reports.                                          */
/* ------------------------------------------------------------------------- */

typedef struct LexicalBlock_s
{   char *filename;                              /*  Full translated name    */
    int   main_flag;                             /*  TRUE if the main file
                                                     (the first one opened)  */
    int   sys_flag;                              /*  TRUE if a System_File   */
    int   source_line;                           /*  Line number count       */
    int   line_start;                            /*  Char number within file
                                                     where the current line
                                                     starts                  */
    int   chars_read;                            /*  Char number of read pos */
    int   file_no;                               /*  Or 255 if not from a
                                                     file; used for debug
                                                     information             */
    char *orig_source;                           /* From #origsource direct  */
    int orig_file;
    int32 orig_line;
    int32 orig_char;
} LexicalBlock;

static LexicalBlock NoFileOpen =
    {   "<before compilation>",  FALSE, FALSE, 0, 0, 0, 255, NULL, 0, 0, 0 };

static LexicalBlock MakingOutput =
    {   "<constructing output>", FALSE, FALSE, 0, 0, 0, 255, NULL, 0, 0, 0 };

static LexicalBlock StringLB =
    {   "<veneer routine>",      FALSE, TRUE,  0, 0, 0, 255, NULL, 0, 0, 0 };

static LexicalBlock *CurrentLB;                  /*  The current lexical
                                                     block of input text     */

extern void declare_systemfile(void)
{   CurrentLB->sys_flag = TRUE;
}

extern int is_systemfile(void)
{   return ((CurrentLB->sys_flag)?1:0);
}

extern void set_origsource_location(char *source, int32 line, int32 charnum)
{
    int file_no;

    if (!source) {
        /* Clear the Origsource declaration. */
        CurrentLB->orig_file = 0;
        CurrentLB->orig_source = NULL;
        CurrentLB->orig_line = 0;
        CurrentLB->orig_char = 0;
        return;
    }

    /* Get the file number for a new or existing InputFiles entry. */
    file_no = register_orig_sourcefile(source);

    CurrentLB->orig_file = file_no;
    CurrentLB->orig_source = InputFiles[file_no-1].filename;
    CurrentLB->orig_line = line;
    CurrentLB->orig_char = charnum;
}

/* Error locations. */

extern debug_location get_current_debug_location(void)
{   debug_location result;
    /* Assume that all input characters are one byte. */
    result.file_index = CurrentLB->file_no;
    result.beginning_byte_index = CurrentLB->chars_read - LOOKAHEAD_SIZE;
    result.end_byte_index = result.beginning_byte_index;
    result.beginning_line_number = CurrentLB->source_line;
    result.end_line_number = result.beginning_line_number;
    result.beginning_character_number =
        CurrentLB->chars_read - CurrentLB->line_start;
    result.end_character_number = result.beginning_character_number;
    result.orig_file_index = CurrentLB->orig_file;
    result.orig_beg_line_number = CurrentLB->orig_line;
    result.orig_beg_char_number = CurrentLB->orig_char;
    return result;
}

static debug_location ErrorReport_debug_location;

extern void report_errors_at_current_line(void)
{   ErrorReport.line_number = CurrentLB->source_line;
    ErrorReport.file_number = CurrentLB->file_no;
    if (ErrorReport.file_number == 255)
        ErrorReport.file_number = -1;
    ErrorReport.source      = CurrentLB->filename;
    ErrorReport.main_flag   = CurrentLB->main_flag;
    if (debugfile_switch)
        ErrorReport_debug_location = get_current_debug_location();
    ErrorReport.orig_file = CurrentLB->orig_file;
    ErrorReport.orig_source = CurrentLB->orig_source;
    ErrorReport.orig_line = CurrentLB->orig_line;
    ErrorReport.orig_char = CurrentLB->orig_char;
}

extern debug_location get_error_report_debug_location(void)
{   return ErrorReport_debug_location;
}

extern int32 get_current_line_start(void)
{   return CurrentLB->line_start;
}

brief_location blank_brief_location;

extern brief_location get_brief_location(ErrorPosition *errpos)
{
    brief_location loc;
    loc.file_index = errpos->file_number;
    loc.line_number = errpos->line_number;
    loc.orig_file_index = errpos->orig_file;
    loc.orig_line_number = errpos->orig_line;
    return loc;
}

extern void export_brief_location(brief_location loc, ErrorPosition *errpos)
{
    if (loc.file_index != -1)
    {   errpos->file_number = loc.file_index;
        errpos->line_number = loc.line_number;
        errpos->main_flag = (errpos->file_number == 1);
        errpos->orig_source = NULL;
        errpos->orig_file = loc.orig_file_index;
        errpos->orig_line = loc.orig_line_number;
        errpos->orig_char = 0;
    }
}

/* ------------------------------------------------------------------------- */
/*   Hash printing and line counting                                         */
/* ------------------------------------------------------------------------- */

static void print_hash(void)
{
    /*  Hash-printing is the practice of printing a # character every 100
        lines of source code (the -x switch), reassuring the user that
        progress is being made                                               */

    if (no_hash_printed_yet)
    {   printf("::"); no_hash_printed_yet = FALSE;
    }
    printf("#"); hash_printed_since_newline = TRUE;

#ifndef MAC_FACE
    /*  On some systems, text output is buffered to a line at a time, and
        this would frustrate the point of hash-printing, so:                 */

    fflush(stdout);
#endif
}

static void reached_new_line(void)
{
    /*  Called to signal that a new line has been reached in the source code */

    forerrors_pointer = 0;

    CurrentLB->source_line++;
    CurrentLB->line_start = CurrentLB->chars_read;

    total_source_line_count++;

    if (total_source_line_count%100==0)
    {   if (hash_switch) print_hash();
#ifdef MAC_MPW
        SpinCursor(32);                    /* I.e., allow other tasks to run */
#endif
    }

#ifdef MAC_FACE
    if (total_source_line_count%((**g_pm_hndl).linespercheck) == 0)
    {   ProcessEvents (&g_proc);
        if (g_proc != true)
        {   free_arrays();
            close_all_source();
            abort_transcript_file();
            longjmp (g_fallback, 1);
        }
    }
#endif
}

static void new_syntax_line(void)
{   if (source_to_analyse != NULL) forerrors_pointer = 0;
    report_errors_at_current_line();
}

/* Return 10 raised to the expo power.
 *
 * I'm avoiding the standard pow() function for a rather lame reason:
 * it's in the libmath (-lm) library, and I don't want to change the
 * build model for the compiler. So, this is implemented with a stupid
 * lookup table. It's faster than pow() for small values of expo.
 * Probably not as fast if expo is 200, but "$+1e200" is an overflow
 * anyway, so I don't expect that to be a problem.
 *
 * (For some reason, frexp() and ldexp(), which are used later on, do
 * not require libmath to be linked in.)
 */
static double pow10_cheap(int expo)
{
    #define POW10_RANGE (8)
    static double powers[POW10_RANGE*2+1] = {
        0.00000001, 0.0000001, 0.000001, 0.00001, 0.0001, 0.001, 0.01, 0.1,
        1.0,
        10.0, 100.0, 1000.0, 10000.0, 100000.0, 1000000.0, 10000000.0, 100000000.0
    };

    double res = 1.0;

    if (expo < 0) {
        for (; expo < -POW10_RANGE; expo += POW10_RANGE) {
            res *= powers[0];
        }
        return res * powers[POW10_RANGE+expo];
    }
    else {
        for (; expo > POW10_RANGE; expo -= POW10_RANGE) {
            res *= powers[POW10_RANGE*2];
        }
        return res * powers[POW10_RANGE+expo];
    }
}

/* Return the IEEE-754 single-precision encoding of a floating-point
 * number.
 *
 * The number is provided in the pieces it was parsed in:
 *    [+|-] intv "." fracv "e" [+|-]expo
 *
 * If the magnitude is too large (beyond about 3.4e+38), this returns
 * an infinite value (0x7f800000 or 0xff800000). If the magnitude is too
 * small (below about 1e-45), this returns a zero value (0x00000000 or 
 * 0x80000000). If any of the inputs are NaN, this returns NaN (but the
 * lexer should never do that).
 *
 * Note that using a float constant does *not* set the uses_float_features
 * flag (which would cause the game file to be labelled 3.1.2). Same with 
 * double constants and the uses_double_features flag. There's no VM
 * feature here, just an integer. Of course, any use of the float *opcodes*
 * will set the flag.
 *
 * The math functions in this routine require #including <math.h>, but
 * they should not require linking the math library (-lm). At least,
 * they do not on OSX and Linux.
 */
static int32 construct_float(int signbit, double intv, double fracv, int expo)
{
    double absval = (intv + fracv) * pow10_cheap(expo);
    int32 sign = (signbit ? 0x80000000 : 0x0);
    double mant;
    int32 fbits;
 
    if (isinf(absval)) {
        return sign | 0x7f800000; /* infinity */
    }
    if (isnan(absval)) {
        return sign | 0x7fc00000;
    }

    mant = frexp(absval, &expo);

    /* Normalize mantissa to be in the range [1.0, 2.0) */
    if (0.5 <= mant && mant < 1.0) {
        mant *= 2.0;
        expo--;
    }
    else if (mant == 0.0) {
        expo = 0;
    }
    else {
        return sign | 0x7f800000; /* infinity */
    }

    if (expo >= 128) {
        return sign | 0x7f800000; /* infinity */
    }
    else if (expo < -126) {
        /* Denormalized (very small) number */
        mant = ldexp(mant, 126 + expo);
        expo = 0;
    }
    else if (!(expo == 0 && mant == 0.0)) {
        expo += 127;
        mant -= 1.0; /* Get rid of leading 1 */
    }

    mant *= 8388608.0; /* 2^23 */
    fbits = (int32)(mant + 0.5); /* round mant to nearest int */
    if (fbits >> 23) {
        /* The carry propagated out of a string of 23 1 bits. */
        fbits = 0;
        expo++;
        if (expo >= 255) {
            return sign | 0x7f800000; /* infinity */
        }
    }

    /* At this point, expo is less than 2^8; fbits is less than 2^23; neither is negative. */
    return (sign) | ((int32)(expo << 23)) | (fbits);
}

/* Same as the above, but we return *half* of a 64-bit double, depending on whether wanthigh is true (high half) or false (low half).
 */
static int32 construct_double(int wanthigh, int signbit, double intv, double fracv, int expo)
{
    double absval = (intv + fracv) * pow10_cheap(expo);
    int32 sign = (signbit ? 0x80000000 : 0x0);
    double mant;
    uint32 fhi, flo;
 
    if (isinf(absval)) {
        goto Infinity;
    }
    if (isnan(absval)) {
        goto NotANumber;
    }

    mant = frexp(absval, &expo);

    /* Normalize mantissa to be in the range [1.0, 2.0) */
    if (0.5 <= mant && mant < 1.0) {
        mant *= 2.0;
        expo--;
    }
    else if (mant == 0.0) {
        expo = 0;
    }
    else {
        goto Infinity;
    }

    if (expo >= 1024) {
        goto Infinity;
    }
    else if (expo < -1022) {
        /* Denormalized (very small) number */
        mant = ldexp(mant, 1022 + expo);
        expo = 0;
    }
    else if (!(expo == 0 && mant == 0.0)) {
        expo += 1023;
        mant -= 1.0; /* Get rid of leading 1 */
    }

    /* fhi receives the high 28 bits; flo the low 24 bits (total 52 bits) */
    mant *= 268435456.0;          /* 2^28 */
    fhi = (uint32)mant;           /* Truncate */
    mant -= (double)fhi;
    mant *= 16777216.0;           /* 2^24 */
    flo = (uint32)(mant+0.5);     /* Round */
    
    if (flo >> 24) {
        /* The carry propagated out of a string of 24 1 bits. */
        flo = 0;
        fhi++;
        if (fhi >> 28) {
            /* And it also propagated out of the next 28 bits. */
            fhi = 0;
            expo++;
            if (expo >= 2047) {
                goto Infinity;
            }
        }
    }

    /* At this point, expo is less than 2^11; fhi is less than 2^28; flo is less than 2^24; none are negative. */
    if (wanthigh)
        return (sign) | ((int32)(expo << 20)) | ((int32)(fhi >> 8));
    else
        return (int32)((fhi & 0xFF) << 24) | (int32)(flo);

 Infinity:
    if (wanthigh)
        return sign | 0x7FF00000;
    else
        return 0x00000000;
    
 NotANumber:
    if (wanthigh)
        return sign | 0x7FF80000;
    else
        return 0x00000001;
}

/* ------------------------------------------------------------------------- */
/*   Characters are read via a "pipeline" of variables, allowing us to look  */
/*       up to three characters ahead of the current position.               */
/*                                                                           */
/*   There are two possible sources: from the source files being loaded in,  */
/*   and from a string inside Inform (which is where the code for veneer     */
/*   routines comes from).  Each source has its own get-next-character       */
/*   routine.                                                                */
/* ------------------------------------------------------------------------- */
/*   Source 1: from files                                                    */
/*                                                                           */
/*   Note that file_load_chars(p, size) loads "size" bytes into buffer "p"   */
/*   from the current input file.  If the file runs out, then if it was      */
/*   the last source file 4 null characters are placed in the buffer: if it  */
/*   was only an Include file ending, then a '\n' character is placed there  */
/*   (essentially to force termination of any comment line) followed by      */
/*   three harmless spaces.                                                  */
/*                                                                           */
/*   The routine returns the number of characters it has written, and note   */
/*   that this conveniently ensures that all characters in the buffer come   */
/*   from the same file.                                                     */
/* ------------------------------------------------------------------------- */

#define SOURCE_BUFFER_SIZE 4096                  /*  Typical disc block size */

typedef struct Sourcefile_s
{   char *buffer;                                /*  Input buffer            */
    int   read_pos;                              /*  Read position in buffer */
    int   size;                                  /*  Number of meaningful
                                                     characters in buffer    */
    int   la, la2, la3;                          /*  Three characters of
                                                     lookahead pipeline      */
    int   file_no;                               /*  Internal file number
                                                     (1, 2, 3, ...)          */
    LexicalBlock LB;
} Sourcefile;

static Sourcefile *FileStack;     /*  Allocated to FileStack_max */
static memory_list FileStack_memlist;
static int FileStack_max;         /*  The highest value that File_sp has
                                      reached
                                      (Filestack entries to this depth have
                                      a buffer allocated)                    */

static int File_sp;               /*  Current stack pointer                  */
static Sourcefile *CF;            /*  Top entry on stack (always equal to
                                      FileStack[File_sp-1])                  */

static int last_input_file;

/* Set CF and CurrentLB.
   This does not increment File_sp; the caller must do that. */
static void begin_buffering_file(int i, int file_no)
{   int j, cnt; uchar *p;

    CF = NULL;
    CurrentLB = NULL;
    
    ensure_memory_list_available(&FileStack_memlist, i+1);
    while (i >= FileStack_max) {
        FileStack[FileStack_max++].buffer = my_malloc(SOURCE_BUFFER_SIZE+4, "source file buffer");
    }

    p = (uchar *) FileStack[i].buffer;

    if (i>0)
    {   FileStack[i-1].la  = lookahead;
        FileStack[i-1].la2 = lookahead2;
        FileStack[i-1].la3 = lookahead3;
    }

    FileStack[i].file_no = file_no;
    FileStack[i].size = file_load_chars(file_no,
        (char *) p, SOURCE_BUFFER_SIZE);
    /* If the file is shorter than SOURCE_BUFFER_SIZE, it's now closed already. We still need to set up the file entry though. */
    
    lookahead  = source_to_iso_grid[p[0]];
    lookahead2 = source_to_iso_grid[p[1]];
    lookahead3 = source_to_iso_grid[p[2]];
    if (LOOKAHEAD_SIZE != 3)
        compiler_error
            ("Lexer lookahead size does not match hard-coded lookahead code");
    FileStack[i].read_pos = LOOKAHEAD_SIZE;

    if (file_no==1) FileStack[i].LB.main_flag = TRUE;
               else FileStack[i].LB.main_flag = FALSE;
    FileStack[i].LB.sys_flag = FALSE;
    FileStack[i].LB.source_line = 1;
    FileStack[i].LB.line_start = LOOKAHEAD_SIZE;
    FileStack[i].LB.chars_read = LOOKAHEAD_SIZE;
    FileStack[i].LB.filename = InputFiles[file_no-1].filename;
    FileStack[i].LB.file_no = file_no;
    FileStack[i].LB.orig_source = NULL; FileStack[i].LB.orig_file = 0; 
    FileStack[i].LB.orig_line = 0; FileStack[i].LB.orig_char = 0;

    InputFiles[file_no-1].initial_buffering = FALSE;
    
    CurrentLB = &(FileStack[i].LB);
    CF = &(FileStack[i]);

    /* Check for recursive inclusion */
    cnt = 0;
    for (j=0; j<i; j++)
    {   if (!strcmp(FileStack[i].LB.filename, FileStack[j].LB.filename))
            cnt++;
    }
    if (cnt==1)
        warning_named("File included more than once",
            FileStack[j].LB.filename);
}

static void create_char_pipeline(void)
{
    File_sp = 0;
    begin_buffering_file(File_sp++, 1);
    pipeline_made = TRUE;
    last_input_file = current_input_file;
}

static int get_next_char_from_pipeline(void)
{   uchar *p;

    while (last_input_file < current_input_file)
    {
        /*  An "Include" file must have opened since the last character
            was read. Perhaps more than one. We run forward through the
            list and add them to the include stack. But we ignore
            "Origsource" files (which were never actually opened for
            reading). */

        last_input_file++;
        if (!InputFiles[last_input_file-1].is_input)
            continue;

        begin_buffering_file(File_sp++, last_input_file);
    }
    if (last_input_file != current_input_file)
        compiler_error("last_input_file did not match after Include");

    if (File_sp == 0)
    {   lookahead  = 0; lookahead2 = 0; lookahead3 = 0; return 0;
    }

    if (CF->read_pos == CF->size)
    {   CF->size =
            file_load_chars(CF->file_no, CF->buffer, SOURCE_BUFFER_SIZE);
        CF->read_pos = 0;
    }
    else
    if (CF->read_pos == -(CF->size))
    {   set_token_location(get_current_debug_location());
        File_sp--;
        if (File_sp == 0)
        {   lookahead  = 0; lookahead2 = 0; lookahead3 = 0; return 0;
        }
        CF = &(FileStack[File_sp-1]);
        CurrentLB = &(FileStack[File_sp-1].LB);
        lookahead  = CF->la; lookahead2 = CF->la2; lookahead3 = CF->la3;
        if (CF->read_pos == CF->size)
        {   CF->size =
                file_load_chars(CF->file_no, CF->buffer, SOURCE_BUFFER_SIZE);
            CF->read_pos = 0;
        }
        set_token_location(get_current_debug_location());
    }

    p = (uchar *) (CF->buffer);

    current = lookahead;
    lookahead = lookahead2;
    lookahead2 = lookahead3;
    lookahead3 = source_to_iso_grid[p[CF->read_pos++]];

    CurrentLB->chars_read++;
    if (forerrors_pointer < FORERRORS_SIZE-1)
        forerrors_buff[forerrors_pointer++] = current;

    /* The file is open in binary mode, so we have to do our own newline
       conversion. (We want to do it consistently across all platforms.)

       The strategy is to convert all \r (CR) characters to \n (LF), but
       *don't* advance the line counter for \r if it's followed by \n.
       The rest of the lexer treats multiple \n characters the same as
       one, so the simple conversion will work out okay.

       (Note that, for historical reasons, a ctrl-L (formfeed) is also
       treated as \r. This conversion has already been handled by
       source_to_iso_grid[].)
    */
    if (current == '\n') {
        reached_new_line();
    }
    else if (current == '\r') {
        current = '\n';
        if (lookahead != '\n')
            reached_new_line();
    }
    
    return(current);
}

/* ------------------------------------------------------------------------- */
/*   Source 2: from a (null-terminated) string                               */
/* ------------------------------------------------------------------------- */

static int source_to_analyse_pointer;            /*  Current read position   */

static int get_next_char_from_string(void)
{   uchar *p = (uchar *) source_to_analyse + source_to_analyse_pointer++;
    current = source_to_iso_grid[p[0]];

    if (current == 0)    lookahead  = 0;
                    else lookahead  = source_to_iso_grid[p[1]];
    if (lookahead == 0)  lookahead2 = 0;
                    else lookahead2 = source_to_iso_grid[p[2]];
    if (lookahead2 == 0) lookahead3 = 0;
                    else lookahead3 = source_to_iso_grid[p[3]];

    CurrentLB->chars_read++;
    if (forerrors_pointer < FORERRORS_SIZE-1)
        forerrors_buff[forerrors_pointer++] = current;

    /* We shouldn't have \r when compiling from string (veneer function).
       If we do, just shove it under the carpet. */
    if (current == '\r') current = '\n';
    if (current == '\n') reached_new_line();
    
    return(current);
}

/* ========================================================================= */
/*   The interface between the lexer and Inform's higher levels:             */
/*                                                                           */
/*       put_token_back()            (effectively) move the read position    */
/*                                       back by one token                   */
/*                                                                           */
/*       get_next_token()            copy the token at the current read      */
/*                                       position into the triple            */
/*                                   (token_type, token_value, token_text)   */
/*                                       and move the read position forward  */
/*                                       by one                              */
/*                                                                           */
/*       release_token_texts()       discard all the tokens that have been   */
/*                                       read in, except for put-back ones   */
/*                                                                           */
/*       restart_lexer(source, name) if source is NULL, initialise the lexer */
/*                                       to read from source files;          */
/*                                       otherwise, to read from this null-  */
/*                                       terminated string.                  */
/* ------------------------------------------------------------------------- */

extern void release_token_texts(void)
{
    /* This is called at the beginning of every (top-level) directive and
       every statement. It drops all token usage so that the lextexts
       array can be reused.

       Call this immediately before a get_next_token() call.

       This should *not* be called within parse_expression(). Expression
       code generation relies on token data being retained across the whole
       expression.
    */
    int ix;

    token_text = NULL;
    
    if (tokens_put_back == 0) {
        cur_lextexts = 0;
        return;
    }

    /* If any tokens have been put back, we have to preserve their text.
       Move their lextext usage to the head of the lextexts array. */
    
    for (ix=0; ix<tokens_put_back; ix++) {
        int oldpos;
        lextext temp;
        int pos = circle_position - tokens_put_back + 1 + ix;
        if (pos < 0) pos += CIRCLE_SIZE;

        oldpos = circle[pos].lextext;
        circle[pos].lextext = ix;
        /* Swap the entire lextext structure (including size) */
        temp = lextexts[ix];
        lextexts[ix] = lextexts[oldpos];
        lextexts[oldpos] = temp;
    }
    cur_lextexts = tokens_put_back;
}

extern void put_token_back(void)
{   tokens_put_back++;

    int pos = circle_position - tokens_put_back + 1;
    if (pos<0) pos += CIRCLE_SIZE;

    if (tokens_trace_level > 0)
    {
        printf("<- ");
        if (tokens_trace_level > 1) {
            describe_token(&circle[pos]);
            printf("\n");
        }
    }

    if (circle[pos].type == SYMBOL_TT && circle[pos].newsymbol) {
        /* Remove the symbol from the symbol table. (Or mark it as unreachable
           anyhow.) */
        end_symbol_scope(circle[pos].value, TRUE);
        /* Remove new-symbol flag, and force reinterpretation next time
           we see the symbol. */
        circle[pos].newsymbol = FALSE;
        circle[pos].context = -1;
    }
    
    /*  The following error, of course, should never happen!                 */

    if (tokens_put_back == CIRCLE_SIZE)
    {   compiler_error("The lexical analyser has collapsed because of a wrong \
assumption inside Inform");
        tokens_put_back--;
        return;
    }
}

/* The get_next_token() code reads characters into the current lextext,
   which is lextexts[lex_index]. It uses these routines to add and remove
   characters, reallocing when necessary.

   lex_pos is the current number of characters in the lextext. It is
   not necessarily null-terminated until get_next_token() completes.
 */

/* Add one character */
static void lexaddc(char ch)
{
    if ((size_t)lex_pos >= lextexts[lex_index].size) {
        size_t newsize = lextexts[lex_index].size * 2;
        my_realloc(&lextexts[lex_index].text, lextexts[lex_index].size, newsize, "one lexeme text");
        lextexts[lex_index].size = newsize;
    }
    lextexts[lex_index].text[lex_pos++] = ch;
}

/* Remove the last character and ensure it's null-terminated */
static void lexdelc(void)
{
    if (lex_pos > 0) {
        lex_pos--;
    }
    lextexts[lex_index].text[lex_pos] = 0;
}

/* Return the last character */
static char lexlastc(void)
{
    if (lex_pos == 0) {
        return 0;
    }
    return lextexts[lex_index].text[lex_pos-1];
}

/* Add a string of characters (including the null) */
static void lexadds(char *str)
{
    while (*str) {
        lexaddc(*str);
        str++;
    }
    lexaddc(0);
}

extern void get_next_token(void)
{   int d, i, j, k, quoted_size, e, radix, context;
    uint32 n;
    char *r;
    int floatend;
    int returning_a_put_back_token = TRUE;
    
    context = lexical_context();

    if (tokens_put_back > 0)
    {   i = circle_position - tokens_put_back + 1;
        if (i<0) i += CIRCLE_SIZE;
        tokens_put_back--;
        if (context != circle[i].context)
        {   j = circle[i].type;
            if ((j==0) || ((j>=100) && (j<200)))
                interpret_identifier(circle[i].text, i);
            circle[i].context = context;
        }
        goto ReturnBack;
    }
    returning_a_put_back_token = FALSE;

    if (circle_position == CIRCLE_SIZE-1) circle_position = 0;
    else circle_position++;

    lex_index = cur_lextexts++;
    if (lex_index >= no_lextexts) {
        /* fresh lextext block; must init it */
        no_lextexts = lex_index+1;
        ensure_memory_list_available(&lextexts_memlist, no_lextexts);
        lextexts[lex_index].size = 64;   /* this can grow */
        lextexts[lex_index].text = my_malloc(lextexts[lex_index].size, "one lexeme text");
    }
    lex_pos = 0;
    lextexts[lex_index].text[0] = 0; /* start with an empty string */
    
    circle[circle_position].lextext = lex_index;
    circle[circle_position].text = NULL; /* will fill in later */
    circle[circle_position].value = 0;
    circle[circle_position].type = 0;
    circle[circle_position].newsymbol = FALSE;
    circle[circle_position].context = context;

    StartTokenAgain:
    d = (*get_next_char)();
    e = tokeniser_grid[d];

    if (next_token_begins_syntax_line)
    {   if ((e != WHITESPACE_CODE) && (e != COMMENT_CODE))
        {   new_syntax_line();
            next_token_begins_syntax_line = FALSE;
        }
    }

    circle[circle_position].location = get_current_debug_location();

    switch(e)
    {   case 0: char_error("Illegal character found in source:", d);
            goto StartTokenAgain;

        case WHITESPACE_CODE:
            while (tokeniser_grid[lookahead] == WHITESPACE_CODE)
                (*get_next_char)();
            goto StartTokenAgain;

        case COMMENT_CODE:
            while ((lookahead != '\n') && (lookahead != '\r') && (lookahead != 0))
                (*get_next_char)();
            goto StartTokenAgain;

        case EOF_CODE:
            circle[circle_position].type = EOF_TT;
            lexadds("<end of file>");
            break;

        case DIGIT_CODE:
            radix = 10;
            ReturnNumber:
            n=0;
            do
            {   n = n*radix + character_digit_value[d];
                lexaddc(d);
            } while ((character_digit_value[lookahead] < radix)
                     && (d = (*get_next_char)(), TRUE));

            lexaddc(0);
            circle[circle_position].type = NUMBER_TT;
            circle[circle_position].value = (int32)n;
            break;

            FloatNumber:
            /* When we reach here, d is the sign bit ('+' or '-').
               If we're constructing a 32-bit float, floatend is 0;
               for a 64-bit double, floatend is '>' for high, '<' for low. */
            {   int expo=0; double intv=0, fracv=0;
                int expocount=0, intcount=0, fraccount=0;
                int signbit = (d == '-');
                lexaddc(d);
                while (character_digit_value[lookahead] < 10) {
                    intv = 10.0*intv + character_digit_value[lookahead];
                    intcount++;
                    lexaddc(lookahead);
                    (*get_next_char)();
                }
                if (lookahead == '.') {
                    double fracpow = 1.0;
                    lexaddc(lookahead);
                    (*get_next_char)();
                    while (character_digit_value[lookahead] < 10) {
                        fracpow *= 0.1;
                        fracv = fracv + fracpow*character_digit_value[lookahead];
                        fraccount++;
                        lexaddc(lookahead);
                        (*get_next_char)();
                    }
                }
                if (lookahead == 'e' || lookahead == 'E') {
                    int exposign = 0;
                    lexaddc(lookahead);
                    (*get_next_char)();
                    if (lookahead == '+' || lookahead == '-') {
                        exposign = (lookahead == '-');
                        lexaddc(lookahead);
                        (*get_next_char)();
                    }
                    while (character_digit_value[lookahead] < 10) {
                        expo = 10*expo + character_digit_value[lookahead];
                        expocount++;
                        lexaddc(lookahead);
                        (*get_next_char)();
                    }
                    if (expocount == 0)
                        error("Floating-point literal must have digits after the 'e'");
                    if (exposign) { expo = -expo; }
                }
                if (intcount + fraccount == 0)
                    error("Floating-point literal must have digits");
                if (floatend == '>')
                    n = construct_double(TRUE, signbit, intv, fracv, expo);
                else if (floatend == '<')
                    n = construct_double(FALSE, signbit, intv, fracv, expo);
                else                    
                    n = construct_float(signbit, intv, fracv, expo);
            }
            lexaddc(0);
            circle[circle_position].type = NUMBER_TT;
            circle[circle_position].value = n;
            if (!glulx_mode && dont_enter_into_symbol_table != -2) error("Floating-point literals are not available in Z-code");
            break;

        case RADIX_CODE:
            radix = 16; d = (*get_next_char)();
            if (d == '-' || d == '+') {
                floatend = 0;
                goto FloatNumber;
            }
            if (d == '<' || d == '>') {
                floatend = d;
                d = (*get_next_char)();
                if (d == '-' || d == '+') {
                    goto FloatNumber;
                }
                error("Signed number expected after '$<' or '$>'");
            }
            if (d == '$') { d = (*get_next_char)(); radix = 2; }
            if (character_digit_value[d] >= radix)
            {   if (radix == 2)
                    error("Binary number expected after '$$'");
                else
                    error("Hexadecimal number expected after '$'");
            }
            goto ReturnNumber;

        case QUOTE_CODE:     /* Single-quotes: scan a literal string */
            quoted_size=0;
            do
            {   e = d; d = (*get_next_char)(); lexaddc(d);
                quoted_size++;
                if ((d == '\'') && (e != '@'))
                {   if (quoted_size == 1)
                    {   d = (*get_next_char)(); lexaddc(d);
                        if (d != '\'')
                            error("No text between quotation marks ''");
                    }
                    break;
                }
            } while (d != 0);
            if (d==0) ebf_error("'\''", "end of file");
            lexdelc();
            circle[circle_position].type = SQ_TT;
            break;

        case DQUOTE_CODE:    /* Double-quotes: scan a literal string */
            do
            {   d = (*get_next_char)(); lexaddc(d);
                if (d == '\n')
                {   lex_pos--;
                    while (lexlastc() == ' ') lex_pos--;
                    if (lexlastc() != '^') lexaddc(' ');
                    while ((lookahead != 0) &&
                          (tokeniser_grid[lookahead] == WHITESPACE_CODE))
                    (*get_next_char)();
                }
                else if (d == '\\')
                {   int newline_passed = FALSE;
                    lex_pos--;
                    while ((lookahead != 0) &&
                          (tokeniser_grid[lookahead] == WHITESPACE_CODE))
                        if ((d = (*get_next_char)()) == '\n')
                            newline_passed = TRUE;
                    if (!newline_passed)
                    {   char chb[4];
                        chb[0] = '\"'; chb[1] = lookahead;
                        chb[2] = '\"'; chb[3] = 0;
                        ebf_error("empty rest of line after '\\' in string",
                            chb);
                    }
                }
            }   while ((d != 0) && (d!='\"'));
            if (d==0) ebf_error("'\"'", "end of file");
            lexdelc();
            circle[circle_position].type = DQ_TT;
            break;

        case IDENTIFIER_CODE:    /* Letter or underscore: an identifier */

            lexaddc(d); n=1;
            while (((tokeniser_grid[lookahead] == IDENTIFIER_CODE)
                   || (tokeniser_grid[lookahead] == DIGIT_CODE)))
                n++, lexaddc((*get_next_char)());

            lexaddc(0);

            interpret_identifier(lextexts[lex_index].text, circle_position);
            break;

        default:

            /*  The character is initial to at least one of the separators  */

            for (j=e>>4, k=j+(e&0x0f); j<k; j++)
            {   r = (char *) separators[j];
                if (r[1]==0)
                {   lexaddc(d);
                    lexaddc(0);
                    goto SeparatorMatched;
                }
                else
                if (r[2]==0)
                {   if (*(r+1) == lookahead)
                    {   lexaddc(d);
                        lexaddc((*get_next_char)());
                        lexaddc(0);
                        goto SeparatorMatched;
                    }
                }
                else
                {   if ((*(r+1) == lookahead) && (*(r+2) == lookahead2))
                    {   lexaddc(d);
                        lexaddc((*get_next_char)());
                        lexaddc((*get_next_char)());
                        lexaddc(0);
                        goto SeparatorMatched;
                    }
                }
            }

            /*  The following contingency never in fact arises with the
                current set of separators, but might in future  */

            lexaddc(d); lexaddc(lookahead); lexaddc(lookahead2);
            lexaddc(0);
            error_named("Unrecognised combination in source:",
                lextexts[lex_index].text);
            goto StartTokenAgain;

            SeparatorMatched:

            circle[circle_position].type = SEP_TT;
            circle[circle_position].value = j;
            switch(j)
            {   case SEMICOLON_SEP: break;
                case HASHNDOLLAR_SEP:
                case HASHWDOLLAR_SEP:
                    if (tokeniser_grid[lookahead] == WHITESPACE_CODE)
                    {   error_named("Character expected after",
                            lextexts[lex_index].text);
                        break;
                    }
                    lex_pos--;
                    lexaddc((*get_next_char)());
                    while ((tokeniser_grid[lookahead] == IDENTIFIER_CODE)
                           || (tokeniser_grid[lookahead] == DIGIT_CODE))
                        lexaddc((*get_next_char)());
                    lexaddc(0);
                    break;
                case HASHADOLLAR_SEP:
                case HASHGDOLLAR_SEP:
                case HASHRDOLLAR_SEP:
                case HASHHASH_SEP:
                    if (tokeniser_grid[lookahead] != IDENTIFIER_CODE)
                    {   error_named("Alphabetic character expected after",
                            lextexts[lex_index].text);
                        break;
                    }
                    lex_pos--;
                    while ((tokeniser_grid[lookahead] == IDENTIFIER_CODE)
                           || (tokeniser_grid[lookahead] == DIGIT_CODE))
                        lexaddc((*get_next_char)());
                    lexaddc(0);
                    break;
            }
            break;
    }

    /* We can now assign the text pointer, since the lextext has finished reallocing. */
    circle[circle_position].text = lextexts[lex_index].text;
    i = circle_position;

    ReturnBack:
    /* We've either parsed a new token or selected a put-back one.
       i is the circle-position of the token in question. Time to
       export the token data where higher-level code can find it. */
    token_value = circle[i].value;
    token_type = circle[i].type;
    token_text = circle[i].text;
    if (!returning_a_put_back_token)
    {   set_token_location(circle[i].location);
    }

    if (tokens_trace_level > 0)
    {   if (tokens_trace_level == 1) {
            printf("'%s' ", circle[i].text);
            if (circle[i].type == EOF_TT) printf("\n");
        }
        else
        {   printf("-> "); describe_token(&circle[i]);
            printf(" ");
            if (tokens_trace_level > 2) {
                if (circle[i].newsymbol) printf("newsym ");
                print_context(circle[i].context);
            }
            printf("\n");
        }
    }
}

static char veneer_error_title[64];

extern void restart_lexer(char *lexical_source, char *name)
{   int i;
    circle_position = 0;
    for (i=0; i<CIRCLE_SIZE; i++)
    {   circle[i].type = 0;
        circle[i].value = 0;
        circle[i].newsymbol = FALSE;
        circle[i].text = "(if this is ever visible, there is a bug)";
        circle[i].lextext = -1;
        circle[i].context = 0;
    }

    cur_lextexts = 0;
    /* But we don't touch no_lextexts; those allocated blocks can be reused */
    lex_index = -1;
    lex_pos = -1;
    
    tokens_put_back = 0;
    forerrors_pointer = 0;
    dont_enter_into_symbol_table = FALSE;
    return_sp_as_variable = FALSE;
    next_token_begins_syntax_line = TRUE;

    source_to_analyse = lexical_source;

    if (source_to_analyse == NULL)
    {   get_next_char = get_next_char_from_pipeline;
        if (!pipeline_made) create_char_pipeline();
        forerrors_buff[0] = 0; forerrors_pointer = 0;
    }
    else
    {   get_next_char = get_next_char_from_string;
        source_to_analyse_pointer = 0;
        CurrentLB = &StringLB;
        sprintf(veneer_error_title, "<veneer routine '%s'>", name);
        StringLB.filename = veneer_error_title;

        CurrentLB->source_line = 1;
        CurrentLB->line_start  = 0;
        CurrentLB->chars_read  = 0;
    }
}

/* ========================================================================= */
/*   Data structure management routines                                      */
/* ------------------------------------------------------------------------- */

extern void init_lexer_vars(void)
{
    FileStack = NULL;
    FileStack_max = 0;
    CF = NULL;
    CurrentLB = NULL;

    lextexts = NULL;
    no_lextexts = 0;
    cur_lextexts = 0;
    lex_index = -1;
    lex_pos = -1;

    no_locals = 0;
    local_variable_names_usage = 0;
    
    blank_brief_location.file_index = -1;
    blank_brief_location.line_number = 0;
    blank_brief_location.orig_file_index = 0;
    blank_brief_location.orig_line_number = 0;
}

extern void lexer_begin_prepass(void)
{   total_source_line_count = 0;
    CurrentLB = &NoFileOpen;
    report_errors_at_current_line();
}

extern void lexer_begin_pass(void)
{   no_hash_printed_yet = TRUE;
    hash_printed_since_newline = FALSE;

    pipeline_made = FALSE;

    no_locals = 0;

    restart_lexer(NULL, NULL);
}

extern void lexer_endpass(void)
{   CurrentLB = &MakingOutput;
    report_errors_at_current_line();
}

extern void lexer_allocate_arrays(void)
{
    initialise_memory_list(&FileStack_memlist,
        sizeof(Sourcefile), 4, (void**)&FileStack,
        "source file stack");
    FileStack_max = 0;

    initialise_memory_list(&lextexts_memlist,
        sizeof(lextext), 200, (void**)&lextexts,
        "lexeme texts list");
    cur_lextexts = 0;

    keywords_hash_table = my_calloc(sizeof(int), HASH_TAB_SIZE,
        "keyword hash table");
    keywords_hash_ends_table = my_calloc(sizeof(int), HASH_TAB_SIZE,
        "keyword hash end table");
    keywords_data_table = my_calloc(sizeof(int), 3*MAX_KEYWORDS,
        "keyword hashing linked list");
    
    initialise_memory_list(&local_variable_names_memlist,
        sizeof(char), MAX_LOCAL_VARIABLES*32, NULL,
        "text of local variable names");
    local_variable_name_offsets = my_calloc(sizeof(int), MAX_LOCAL_VARIABLES-1,
        "offsets of local variable names");
    local_variable_hash_table = my_calloc(sizeof(int), HASH_TAB_SIZE,
        "local variable hash table");
    local_variable_hash_codes = my_calloc(sizeof(int), MAX_LOCAL_VARIABLES,
        "local variable hash codes");

    make_tokeniser_grid();
    make_keywords_tables();

    first_token_locations =
        my_malloc(sizeof(debug_locations), "debug locations of recent tokens");
    first_token_locations->location.file_index = 0;
    first_token_locations->location.beginning_byte_index = 0;
    first_token_locations->location.end_byte_index = 0;
    first_token_locations->location.beginning_line_number = 0;
    first_token_locations->location.end_line_number = 0;
    first_token_locations->location.beginning_character_number = 0;
    first_token_locations->location.end_character_number = 0;
    first_token_locations->location.orig_file_index = 0;
    first_token_locations->location.orig_beg_line_number = 0;
    first_token_locations->location.orig_beg_char_number = 0;
    first_token_locations->next = NULL;
    first_token_locations->reference_count = 0;
    last_token_location = first_token_locations;
}

extern void lexer_free_arrays(void)
{   int ix;
    CF = NULL;
    CurrentLB = NULL;

    for (ix=0; ix<FileStack_max; ix++) {
        my_free(&FileStack[ix].buffer, "source file buffer");
    }
    deallocate_memory_list(&FileStack_memlist);

    for (ix=0; ix<no_lextexts; ix++) {
        my_free(&lextexts[ix].text, "one lexeme text");
    }
    deallocate_memory_list(&lextexts_memlist);

    my_free(&keywords_hash_table, "keyword hash table");
    my_free(&keywords_hash_ends_table, "keyword hash end table");
    my_free(&keywords_data_table, "keyword hashing linked list");

    deallocate_memory_list(&local_variable_names_memlist);
    my_free(&local_variable_name_offsets, "offsets of local variable names");
    my_free(&local_variable_hash_table, "local variable hash table");
    my_free(&local_variable_hash_codes, "local variable hash codes");

    cleanup_token_locations(NULL);
}

/* ========================================================================= */

/* ------------------------------------------------------------------------- */
/*   "options" : Compiler options and memory settings                        */
/*                                                                           */
/*   Part of Inform 6.43                                                     */
/*   copyright (c) Graham Nelson 1993 - 2024                                 */
/*                                                                           */
/* ------------------------------------------------------------------------- */

#include "header.h"

/* ### Notes: 
   set_memory_sizes() is default-setting, so that will move to options table.
   Eliminate adjust_memory_sizes(). If we make $LIST smarter, we don't need it.
   select_target() handles a lot of bounds and mod checking. This is called after all option setting (including !%). Rework this to call the master option-grabber (table to vars).

   INDIV_PROP_START is handled inconsistently.
   SERIAL is a six-character string. (And not in $LIST?)
   A couple of Glulx options are rounded up mod 256.
   Same Glulx options are defined as int32.
   NUM_ATTR_BYTES is rounded up 3 mod 4.
   MAX_ABBREVS, MAX_DYNAMIC_STRINGS fight in Z-code.
 ### */

enum optionuse {
    OPTUSE_ALL         = 0,  /* all targets */
    OPTUSE_ZCODE       = 1,  /* Z-code only */
    OPTUSE_GLULX       = 2,  /* Glulx only */
    OPTUSE_OBSOLETE_I5 = 5,  /* obsolete Inform 5 option */
    OPTUSE_OBSOLETE_I6 = 6,  /* obsolete Inform 6 option */
};

enum optionlimit {
    OPTLIM_ANY         = 0,  /* any non-negative value */
    OPTLIM_TOMAX       = 1,  /* zero to N inclusive */
    OPTLIM_MUL256      = 2,  /* any, round up to a multiple of 256 */
    OPTLIM_3MOD4       = 3,  /* any, round up to a multiple of 3 mod 4 */
};

#define MAX_OPT(v) { 0, (v) }  /* zero to N inclusive */
#define MUL256_OPT() { } /* round up to a multiple of 256 */

#define DEFAULTVAL(v) { (v), (v) }
#define DEFAULTVALS(z, g) { (z), (g) }

#define DEFAULT_OPTPREC (0)
#define HEADCOM_OPTPREC (1)
#define CMDLINE_OPTPREC (2)

typedef struct optionlimit_s {
    enum optionlimit limittype;
    int32 maxval;
} optionlimitt;

typedef struct platformval_s {
    int32 z;
    int32 g;
} platformval;

typedef struct optiont_s {
    char *name;
    char *desc;
    enum optionuse use;
    optionlimitt limit;
    platformval val;
    int precedence;
} optiont;

/* Must match the order of alloptions[], which is the order of the $LIST
   in the old options system, which was not very systematic. */
enum optionindex {
    OPT_HASH_TAB_SIZE   = 0,
    OPT_OPTIONS_COUNT   = 1,
};

static optiont alloptions[] = {
    {
        "HASH_TAB_SIZE",
        "\
  HASH_TAB_SIZE is the size of the hash tables used for the heaviest \n\
  symbols banks.\n",
        OPTUSE_ALL,
        { OPTLIM_ANY },
        DEFAULTVAL(512),
    },
    {
        "DICT_WORD_SIZE",
        "\
  DICT_WORD_SIZE is the number of characters in a dictionary word. In \n\
  Z-code this is always 6 (only 4 are used in v3 games). In Glulx it \n\
  can be any number.\n",
        OPTUSE_GLULX,
    },
    {
        "DICT_CHAR_SIZE",
        "\
  DICT_CHAR_SIZE is the byte size of one character in the dictionary. \n\
  (This is only meaningful in Glulx, since Z-code has compressed dictionary \n\
  words.) It can be either 1 (the default) or 4 (to enable full Unicode \n\
  input.)\n",
        OPTUSE_GLULX,
    },
    {
        "GRAMMAR_VERSION",
        "\
  GRAMMAR_VERSION defines the table format for the verb grammar. 2 is \n\
  the Inform standard. 1 is an older version based on Infocom's format. \n\
  The default is 1 in Z-code, 2 in Glulx.\n",
        OPTUSE_ALL,
    },
    {
        "GRAMMAR_META_FLAG",
        "\
  GRAMMAR_META_FLAG indicates actions which have the 'meta' flag by \n\
  ensure that their values are <= #largest_meta_action. This allows \n\
  individual actions to be marked 'meta', rather than relying on dict \n\
  word flags.\n",
        OPTUSE_ALL,
    },
    {
        "NUM_ATTR_BYTES",
        "\
  NUM_ATTR_BYTES is the space used to store attribute flags. Each byte \n\
  stores eight attributes. In Z-code this is always 6 (only 4 are used in \n\
  v3 games). In Glulx it can be any number which is a multiple of four, \n\
  plus three.\n",
        OPTUSE_GLULX,
    },
    {
        "ZCODE_HEADER_EXT_WORDS",
        "\
  ZCODE_HEADER_EXT_WORDS is the number of words in the Z-code header \n\
  extension table (Z-Spec 1.0). The -W switch also sets this. It defaults \n\
  to 3, but can be set higher. (It can be set lower if no Unicode \n\
  translation table is created.)\n",
        OPTUSE_ZCODE,
    },
    {
        "ZCODE_HEADER_FLAGS_3",
        "\
  ZCODE_HEADER_FLAGS_3 is the value to store in the Flags 3 word of the \n\
  header extension table (Z-Spec 1.1).\n",
        OPTUSE_ZCODE,
    },
    {
        "ZCODE_FILE_END_PADDING",
        "\
  ZCODE_FILE_END_PADDING, if set, pads the game file length to a multiple \n\
  of 512 bytes. (Z-code only.)\n",
        OPTUSE_ZCODE,
    },
    {
        "ZCODE_LESS_DICT_DATA",
        "\
  ZCODE_LESS_DICT_DATA, if set, provides each dict word with two data bytes\n\
  rather than three. (Z-code only.)\n",
        OPTUSE_ZCODE,
    },
    {
        "ZCODE_MAX_INLINE_STRING",
        "\
  ZCODE_MAX_INLINE_STRING is the length beyond which string literals cannot\n\
  be inlined in assembly opcodes. (Z-code only.)\n",
        OPTUSE_ZCODE,
    },
    {
        "GLULX_OBJECT_EXT_BYTES",
        "\
  GLULX_OBJECT_EXT_BYTES is an amount of additional space to add to each \n\
  object record. It is initialized to zero bytes, and the game is free to \n\
  use it as desired. (This is only meaningful in Glulx, since Z-code \n\
  specifies the object structure.)\n",
        OPTUSE_GLULX,
    },
    {
        "MAX_ABBREVS",
        "\
  MAX_ABBREVS is the maximum number of declared abbreviations.  It is not \n\
  allowed to exceed 96 in Z-code. (This is not meaningful in Glulx, where \n\
  there is no limit on abbreviations.)\n",
        OPTUSE_ZCODE,
    },
    {
        "MAX_DYNAMIC_STRINGS",
        "\
  MAX_DYNAMIC_STRINGS is the maximum number of string substitution variables\n\
  (\"@00\" or \"@(0)\").  It is not allowed to exceed 96 in Z-code.\n",
        OPTUSE_ALL,
    },
    {
        "INDIV_PROP_START",
        "\
  Properties 1 to INDIV_PROP_START-1 are common properties; individual\n\
  properties are numbered INDIV_PROP_START and up.\n",
        OPTUSE_ALL,
    },
    {
        "MAX_STACK_SIZE",
        "\
  MAX_STACK_SIZE is the maximum size (in bytes) of the interpreter stack \n\
  during gameplay. (Glulx only)\n",
        OPTUSE_GLULX,
    },
    {
        "MEMORY_MAP_EXTENSION",
        "\
  MEMORY_MAP_EXTENSION is the number of bytes (all zeroes) to map into \n\
  memory after the game file. (Glulx only)\n",
        OPTUSE_GLULX,
    },
    {
        "TRANSCRIPT_FORMAT",
        "\
  TRANSCRIPT_FORMAT, if set to 1, adjusts the gametext.txt transcript for \n\
  easier machine processing; each line will be prefixed by its context.\n",
        OPTUSE_ALL,
    },
    {
        "WARN_UNUSED_ROUTINES",
        "\
  WARN_UNUSED_ROUTINES, if set to 2, will display a warning for each \n\
  routine in the game file which is never called. (This includes \n\
  routines called only from uncalled routines, etc.) If set to 1, will warn \n\
  only about functions in game code, not in the system library.\n",
        OPTUSE_ALL,
    },
    {
        "OMIT_UNUSED_ROUTINES",
        "\
  OMIT_UNUSED_ROUTINES, if set to 1, will avoid compiling unused routines \n\
  into the game file.\n",
        OPTUSE_ALL,
    },
    {
        "STRIP_UNREACHABLE_LABELS",
        "\
  STRIP_UNREACHABLE_LABELS, if set to 1, will skip labels in unreachable \n\
  statements. Jumping to a skipped label is an error. If 0, all labels \n\
  will be compiled, at the cost of less optimized code. The default is 1.\n",
        OPTUSE_ALL,
    },
    {
        "OMIT_SYMBOL_TABLE",
        "\
  OMIT_SYMBOL_TABLE, if set to 1, will skip compiling debug symbol names \n\
  into the game file.\n",
        OPTUSE_ALL,
    },
    {
        "DICT_IMPLICIT_SINGULAR",
        "\
  DICT_IMPLICIT_SINGULAR, if set to 1, will cause dict words in noun \n\
  context to have the '//s' flag if the '//p' flag is not set. \n",
        OPTUSE_ALL,
    },
    {
        "DICT_TRUNCATE_FLAG",
        "\
  DICT_TRUNCATE_FLAG, if set to 1, will set bit 6 of a dict word if the \n\
  word is truncated (extends beyond DICT_WORD_SIZE). If 0, bit 6 will be \n\
  set for verbs (legacy behavior). \n",
        OPTUSE_ALL,
    },
    {
        "LONG_DICT_FLAG_BUG",
        "\
  LONG_DICT_FLAG_BUG, if set to 0, will fix the old bug which ignores \n\
  the '//p' flag in long dictionary words. If 1, the buggy behavior is \n\
  retained.\n",
        OPTUSE_ALL,
    },
    {
        "SERIAL",
        "\
  SERIAL, if set, will be used as the six digit serial number written into \n\
  the header of the output file.\n",
        OPTUSE_ALL,
    },
};

/* The default options are set above. All we really need to do here is
   set the precedence field of each entry.
*/
extern void prepare_options(void)
{
    int ix;
    if (OPT_OPTIONS_COUNT != sizeof(alloptions) / sizeof(optiont))
        compiler_error("alloptions array size mismatch");
    for (ix=0; ix < OPT_OPTIONS_COUNT; ix++) {
        alloptions[ix].precedence = DEFAULT_OPTPREC;
    }
}

/* Set an option to a given value.

   We will enforce option limits (minima, maxima, multiple-of-N) before
   setting the value.
   
   The precedence value indicates where the option came from. Higher
   precedence sources (command line) override lower precedence (header
   comments).

   Note that we do not necessarily know whether the target is Z-code
   or Glulx when this is called. The option structure has two values,
   perhaps differing; we will set both.
 */
extern void set_option(char *name, int32 val, int prec)
{
}

/* Apply the options to our compiler variables. At this point we *do*
   know the target platform (glulx_mode is TRUE or FALSE) and all
   options have valid values.
*/
extern void apply_options(void)
{
}

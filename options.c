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

#define CURRENT_OPT (0)
#define OBSOLETE_OPT_5 (5)
#define OBSOLETE_OPT_6 (6)

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

/* Must match the order of alloptions[] */
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
};

extern void prepare_options(void)
{
    int ix;
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

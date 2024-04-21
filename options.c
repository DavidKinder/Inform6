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

#define CURRENT_OPT (0)
#define OBSOLETE_OPT_5 (5)
#define OBSOLETE_OPT_6 (6)

#define DEFAULTVAL(v) { (v), (v), }
#define DEFAULTVALS(z, g) { (z), (g), }

#define DEFAULT_OPTPREC (0)
#define HEADCOM_OPTPREC (1)
#define CMDLINE_OPTPREC (2)

typedef struct platformval_s {
    int z;
    int g;
} platformval;

typedef struct optiont_s {
    char *name;
    char *desc;
    int obsolete;
    int minval;
    int maxval;
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
        CURRENT_OPT,
        0, -1,
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

/* Set an option to a given value. The option value is a string; most
   options are numeric but not all.

   We will enforce option limits (minima, maxima, multiple-of-N) before
   setting the value.
   
   The precedence value indicates where the option came from. Higher
   precedence sources (command line) override lower precedence (header
   comments).

   Note that we do not necessarily know whether the target is Z-code
   or Glulx when this is called. The option structure has two values,
   perhaps differing; we will set both.
 */
extern void set_option(char *name, char *str, int prec)
{
}

/* Apply the options to our compiler variables. At this point we *do*
   know the target platform (glulx_mode is TRUE or FALSE) and all
   options have valid values.
*/
extern void apply_options(void)
{
}

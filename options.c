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
    { NULL },
};

extern void init_options(void)
{
    int ix;
    for (ix=0; alloptions[ix].name; ix++) {
        alloptions[ix].precedence = DEFAULT_OPTPREC;
        puts(alloptions[ix].desc);
    }
}

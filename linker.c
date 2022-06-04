/* ------------------------------------------------------------------------- */
/*   "linker" : For compiling and linking modules                            */
/*                                                                           */
/*   Part of Inform 6.40                                                     */
/*   copyright (c) Graham Nelson 1993 - 2022                                 */
/*                                                                           */
/* ------------------------------------------------------------------------- */

#include "header.h"

uchar *link_data_holding_area;            /* Allocated to link_data_ha_size  */
static memory_list link_data_holding_area_memlist;
int32 link_data_ha_size;

uchar *link_data_area;
static memory_list link_data_area_memlist;
                                          /*  Start, current top, size of    */
int32 link_data_size;                     /*  link data table being written  */
                                          /*  (holding import/export names)  */

/* ------------------------------------------------------------------------- */
/*   Import/export records                                                   */
/* ------------------------------------------------------------------------- */

/* ========================================================================= */
/*   Linking in external modules: this code is run when the external         */
/*   program hits a Link directive.                                          */
/* ------------------------------------------------------------------------- */
/*   This map is between global variable numbers in the module and in the    */
/*   external program: variables_map[n] will be the external global variable */
/*   no for module global variable no n.  (The entries [0] to [15] are not   */
/*   used.)                                                                  */
/* ------------------------------------------------------------------------- */

/* ------------------------------------------------------------------------- */
/*   These are offsets within the module:                                    */
/* ------------------------------------------------------------------------- */


/* ------------------------------------------------------------------------- */
/*   Reading and writing bytes/words in the module (as loaded in), indexing  */
/*   via "marker addresses".                                                 */
/* ------------------------------------------------------------------------- */

/* ------------------------------------------------------------------------- */
/*   The main routine: linking in a module with the given filename.          */
/* ------------------------------------------------------------------------- */

char current_module_filename[PATHLEN];


/* ========================================================================= */
/*   Writing imports, exports and markers to the link data table during      */
/*   module compilation                                                      */
/* ------------------------------------------------------------------------- */
/*   Writing to the link data table                                          */
/* ------------------------------------------------------------------------- */

/* ------------------------------------------------------------------------- */
/*   Exports and imports                                                     */
/* ------------------------------------------------------------------------- */

/* ------------------------------------------------------------------------- */
/*   Marking for later importation                                           */
/* ------------------------------------------------------------------------- */

/* ========================================================================= */
/*   Data structure management routines                                      */
/* ------------------------------------------------------------------------- */

extern void init_linker_vars(void)
{   link_data_size = 0;
    link_data_area = NULL;
    link_data_ha_size = 0;
    link_data_holding_area = NULL;
}

extern void linker_begin_pass(void)
{   link_data_ha_size = 0;
}

extern void linker_endpass(void)
{
}

extern void linker_allocate_arrays(void)
{
    int initlinksize = 0;
    initialise_memory_list(&link_data_holding_area_memlist,
        sizeof(uchar), initlinksize, (void**)&link_data_holding_area,
        "link data holding area");
    initialise_memory_list(&link_data_area_memlist,
        sizeof(uchar), 128, (void**)&link_data_area,
        "link data area");
}

extern void linker_free_arrays(void)
{
    deallocate_memory_list(&link_data_holding_area_memlist);
    deallocate_memory_list(&link_data_area_memlist);
}

/* ========================================================================= */

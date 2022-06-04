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
/*   Marker values                                                           */
/* ------------------------------------------------------------------------- */

extern char *describe_mv(int mval)
{   switch(mval)
    {   case NULL_MV:       return("null");

        /*  Marker values used in ordinary story file backpatching  */

        case DWORD_MV:      return("dictionary word");
        case STRING_MV:     return("string literal");
        case INCON_MV:      return("system constant");
        case IROUTINE_MV:   return("routine");
        case VROUTINE_MV:   return("veneer routine");
        case ARRAY_MV:      return("internal array");
        case NO_OBJS_MV:    return("the number of objects");
        case INHERIT_MV:    return("inherited common p value");
        case INDIVPT_MV:    return("indiv prop table address");
        case INHERIT_INDIV_MV: return("inherited indiv p value");
        case MAIN_MV:       return("ref to Main");
        case SYMBOL_MV:     return("ref to symbol value");

        /*  Additional marker values used in module backpatching  */

        case VARIABLE_MV:   return("global variable");
        case IDENT_MV:      return("prop identifier number");
        case ACTION_MV:     return("action");
        case OBJECT_MV:     return("internal object");

        /*  Record types in the import/export table (not really marker
            values at all)  */

        case EXPORT_MV:     return("Export   ");
        case EXPORTSF_MV:   return("Export sf");
        case EXPORTAC_MV:   return("Export ##");
        case IMPORT_MV:     return("Import   ");
    }
    return("** No such MV **");
}

/* ------------------------------------------------------------------------- */
/*   Import/export records                                                   */
/* ------------------------------------------------------------------------- */

typedef struct importexport_s
{   int module_value;
    int32 symbol_number;
    char symbol_type;
    int backpatch;
    int32 symbol_value;
    char *symbol_name;
} ImportExport;

/* ========================================================================= */
/*   Linking in external modules: this code is run when the external         */
/*   program hits a Link directive.                                          */
/* ------------------------------------------------------------------------- */
/*   This map is between global variable numbers in the module and in the    */
/*   external program: variables_map[n] will be the external global variable */
/*   no for module global variable no n.  (The entries [0] to [15] are not   */
/*   used.)                                                                  */
/* ------------------------------------------------------------------------- */

int32 module_map[16];

ImportExport IE;

/* ------------------------------------------------------------------------- */
/*   These are offsets within the module:                                    */
/* ------------------------------------------------------------------------- */

int32 *xref_table; int xref_top;
int32 *property_identifier_map;
int *accession_numbers_map;
int32 routine_replace[64],
      routine_replace_with[64]; int no_rr;

/* ------------------------------------------------------------------------- */
/*   Reading and writing bytes/words in the module (as loaded in), indexing  */
/*   via "marker addresses".                                                 */
/* ------------------------------------------------------------------------- */

int m_read_pos;

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

int mv_vref=LOWEST_SYSTEM_VAR_NUMBER-1;

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

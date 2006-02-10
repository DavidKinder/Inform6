/* ------------------------------------------------------------------------- */
/*   "memory" : Memory management and ICL memory setting commands            */
/*              (For "memoryerror", see "errors.c")                          */
/*                                                                           */
/*   Part of Inform 6.31                                                     */
/*   copyright (c) Graham Nelson 1993 - 2006                                 */
/*                                                                           */
/* ------------------------------------------------------------------------- */

#include "header.h"

int32 malloced_bytes=0;                /* Total amount of memory allocated   */

#ifdef PC_QUICKC
extern void *my_malloc(int32 size, char *whatfor)
{   char _huge *c;
    if (memout_switch)
        printf("Allocating %ld bytes for %s\n",size,whatfor);
    if (size==0) return(NULL);
    c=(char _huge *)halloc(size,1); malloced_bytes+=size;
    if (c==0) memory_out_error(size, 1, whatfor);
    return(c);
}
extern void *my_calloc(int32 size, int32 howmany, char *whatfor)
{   void _huge *c;
    if (memout_switch)
        printf("Allocating %d bytes: array (%ld entries size %ld) for %s\n",
            size*howmany,howmany,size,whatfor);
    if ((size*howmany) == 0) return(NULL);
    c=(void _huge *)halloc(howmany*size,1); malloced_bytes+=size*howmany;
    if (c==0) memory_out_error(size, howmany, whatfor);
    return(c);
}
#else
extern void *my_malloc(int32 size, char *whatfor)
{   char *c;
    if (size==0) return(NULL);
    c=malloc((size_t) size); malloced_bytes+=size;
    if (c==0) memory_out_error(size, 1, whatfor);
    if (memout_switch)
        printf("Allocating %ld bytes for %s at (%08lx)\n",
            (long int) size,whatfor,(long int) c);
    return(c);
}
extern void *my_calloc(int32 size, int32 howmany, char *whatfor)
{   void *c;
    if (size*howmany==0) return(NULL);
    c=calloc(howmany,(size_t) size); malloced_bytes+=size*howmany;
    if (c==0) memory_out_error(size, howmany, whatfor);
    if (memout_switch)
        printf("Allocating %ld bytes: array (%ld entries size %ld) \
for %s at (%08lx)\n",
            ((long int)size) * ((long int)howmany),
            (long int)howmany,(long int)size,whatfor,
            (long int) c);
    return(c);
}
#endif

extern void my_free(void *pointer, char *whatitwas)
{
    if (*(int **)pointer != NULL)
    {   if (memout_switch)
            printf("Freeing memory for %s at (%08lx)\n",
                whatitwas, (long int) (*(int **)pointer));
#ifdef PC_QUICKC
        hfree(*(int **)pointer);
#else
        free(*(int **)pointer);
#endif
        *(int **)pointer = NULL;
    }
}

/* ------------------------------------------------------------------------- */
/*   Extensible blocks of memory, providing a kind of RAM disc as an         */
/*   alternative to the temporary files option                               */
/* ------------------------------------------------------------------------- */

static char chunk_name_buffer[60];
static char *chunk_name(memory_block *MB, int no)
{   char *p = "(unknown)";
    if (MB == &static_strings_area) p = "static strings area";
    if (MB == &zcode_area)          p = "Z-code area";
    if (MB == &link_data_area)      p = "link data area";
    if (MB == &zcode_backpatch_table) p = "Z-code backpatch table";
    if (MB == &zmachine_backpatch_table) p = "Z-machine backpatch table";
    sprintf(chunk_name_buffer, "%s chunk %d", p, no);
    return(chunk_name_buffer);
}

extern void initialise_memory_block(memory_block *MB)
{   int i;
    MB->chunks = 0;
    for (i=0; i<72; i++) MB->chunk[i] = NULL;
    MB->extent_of_last = 0;
    MB->write_pos = 0;
}

extern void deallocate_memory_block(memory_block *MB)
{   int i;
    for (i=0; i<72; i++)
        if (MB->chunk[i] != NULL)
            my_free(&(MB->chunk[i]), chunk_name(MB, i));
    MB->chunks = 0;
    MB->extent_of_last = 0;
}

extern int read_byte_from_memory_block(memory_block *MB, int32 index)
{   uchar *p;
    p = MB->chunk[index/ALLOC_CHUNK_SIZE];
    if (p == NULL)
    {   compiler_error_named("memory: read from unwritten byte in",
            chunk_name(MB, index/ALLOC_CHUNK_SIZE));
        return 0;
    }
    return p[index % ALLOC_CHUNK_SIZE];
}

extern void write_byte_to_memory_block(memory_block *MB, int32 index, int value)
{   uchar *p; int ch = index/ALLOC_CHUNK_SIZE;
    if (ch < 0)
    {   compiler_error_named("memory: negative index to", chunk_name(MB, 0));
        return;
    }
    if (ch >= 72) fatalerror("One of the memory blocks has exceeded 640K");

    if (MB->chunk[ch] == NULL)
    {   int i;
        MB->chunk[ch] = my_malloc(ALLOC_CHUNK_SIZE, chunk_name(MB, ch));
        p = MB->chunk[ch];
        for (i=0; i<ALLOC_CHUNK_SIZE; i++) p[i] = 255;
    }

    p = MB->chunk[ch];
    p[index % ALLOC_CHUNK_SIZE] = value;
}

/* ------------------------------------------------------------------------- */
/*   Where the memory settings are declared as variables                     */
/* ------------------------------------------------------------------------- */

int MAX_QTEXT_SIZE;
int MAX_SYMBOLS;
int SYMBOLS_CHUNK_SIZE;
int HASH_TAB_SIZE;
int MAX_OBJECTS;
int MAX_ARRAYS;
int MAX_ACTIONS;
int MAX_ADJECTIVES;
int MAX_DICT_ENTRIES;
int MAX_STATIC_DATA;
int MAX_PROP_TABLE_SIZE;
int MAX_ABBREVS;
int MAX_EXPRESSION_NODES;
int MAX_VERBS;
int MAX_VERBSPACE;
int MAX_LABELS;
int MAX_LINESPACE;
int32 MAX_STATIC_STRINGS;
int32 MAX_ZCODE_SIZE;
int MAX_LOW_STRINGS;
int32 MAX_TRANSCRIPT_SIZE;
int MAX_CLASSES;
int MAX_CLASS_TABLE_SIZE;
int32 MAX_LINK_DATA_SIZE;
int MAX_INCLUSION_DEPTH;
int MAX_SOURCE_FILES;
int32 MAX_INDIV_PROP_TABLE_SIZE;
int32 MAX_OBJ_PROP_TABLE_SIZE;
int MAX_OBJ_PROP_COUNT;
int MAX_LOCAL_VARIABLES;
int MAX_GLOBAL_VARIABLES;
int DICT_WORD_SIZE;
int NUM_ATTR_BYTES;
int32 MAX_NUM_STATIC_STRINGS;

/* The way memory sizes are set causes great nuisance for those parameters
   which have different defaults under Z-code and Glulx. We have to get
   the defaults right whether the user sets "-G $HUGE" or "$HUGE -G". 
   And an explicit value set by the user should override both defaults. */
static int32 MAX_ZCODE_SIZE_z, MAX_ZCODE_SIZE_g;
static int MAX_PROP_TABLE_SIZE_z, MAX_PROP_TABLE_SIZE_g;
static int MAX_GLOBAL_VARIABLES_z, MAX_GLOBAL_VARIABLES_g;
static int MAX_LOCAL_VARIABLES_z, MAX_LOCAL_VARIABLES_g;
static int DICT_WORD_SIZE_z, DICT_WORD_SIZE_g;
static int NUM_ATTR_BYTES_z, NUM_ATTR_BYTES_g;

/* ------------------------------------------------------------------------- */
/*   Memory control from the command line                                    */
/* ------------------------------------------------------------------------- */

static void list_memory_sizes(void)
{   printf("+--------------------------------------+\n");
    printf("|  %25s = %-7s |\n","Memory setting","Value");
    printf("+--------------------------------------+\n");
    printf("|  %25s = %-7d |\n","MAX_ABBREVS",MAX_ABBREVS);
    printf("|  %25s = %-7d |\n","MAX_ACTIONS",MAX_ACTIONS);
    printf("|  %25s = %-7d |\n","MAX_ADJECTIVES",MAX_ADJECTIVES);
    printf("|  %25s = %-7d |\n","NUM_ATTR_BYTES",NUM_ATTR_BYTES);
    printf("|  %25s = %-7d |\n","MAX_CLASSES",MAX_CLASSES);
    printf("|  %25s = %-7d |\n","MAX_CLASS_TABLE_SIZE",MAX_CLASS_TABLE_SIZE);
    printf("|  %25s = %-7d |\n","MAX_DICT_ENTRIES",MAX_DICT_ENTRIES);
    printf("|  %25s = %-7d |\n","DICT_WORD_SIZE",DICT_WORD_SIZE);
    printf("|  %25s = %-7d |\n","MAX_EXPRESSION_NODES",MAX_EXPRESSION_NODES);
    printf("|  %25s = %-7d |\n","MAX_GLOBAL_VARIABLES",MAX_GLOBAL_VARIABLES);
    printf("|  %25s = %-7d |\n","HASH_TAB_SIZE",HASH_TAB_SIZE);
    printf("|  %25s = %-7d |\n","MAX_INCLUSION_DEPTH",MAX_INCLUSION_DEPTH);
    printf("|  %25s = %-7d |\n","MAX_INDIV_PROP_TABLE_SIZE",
        MAX_INDIV_PROP_TABLE_SIZE);
    printf("|  %25s = %-7d |\n","MAX_LABELS",MAX_LABELS);
    printf("|  %25s = %-7d |\n","MAX_LINESPACE",MAX_LINESPACE);
    printf("|  %25s = %-7d |\n","MAX_LINK_DATA_SIZE",MAX_LINK_DATA_SIZE);
    printf("|  %25s = %-7d |\n","MAX_LOCAL_VARIABLES",MAX_LOCAL_VARIABLES);
    printf("|  %25s = %-7d |\n","MAX_LOW_STRINGS",MAX_LOW_STRINGS);
    printf("|  %25s = %-7d |\n","MAX_NUM_STATIC_STRINGS",MAX_NUM_STATIC_STRINGS);
    printf("|  %25s = %-7d |\n","MAX_OBJECTS",MAX_OBJECTS);
    printf("|  %25s = %-7d |\n","MAX_OBJ_PROP_COUNT",
        MAX_OBJ_PROP_COUNT);
    printf("|  %25s = %-7d |\n","MAX_OBJ_PROP_TABLE_SIZE",
        MAX_OBJ_PROP_TABLE_SIZE);
    printf("|  %25s = %-7d |\n","MAX_PROP_TABLE_SIZE",MAX_PROP_TABLE_SIZE);
    printf("|  %25s = %-7d |\n","MAX_QTEXT_SIZE",MAX_QTEXT_SIZE);
    printf("|  %25s = %-7d |\n","MAX_SOURCE_FILES",MAX_SOURCE_FILES);
    printf("|  %25s = %-7d |\n","MAX_SYMBOLS",MAX_SYMBOLS);
    printf("|  %25s = %-7d |\n","MAX_STATIC_DATA",MAX_STATIC_DATA);
    printf("|  %25s = %-7ld |\n","MAX_STATIC_STRINGS",
           (long int) MAX_STATIC_STRINGS);
    printf("|  %25s = %-7d |\n","SYMBOLS_CHUNK_SIZE",SYMBOLS_CHUNK_SIZE);
    printf("|  %25s = %-7ld |\n","MAX_TRANSCRIPT_SIZE",
           (long int) MAX_TRANSCRIPT_SIZE);
    printf("|  %25s = %-7d |\n","MAX_VERBS",MAX_VERBS);
    printf("|  %25s = %-7d |\n","MAX_VERBSPACE",MAX_VERBSPACE);
    printf("|  %25s = %-7ld |\n","MAX_ZCODE_SIZE",
           (long int) MAX_ZCODE_SIZE);
    printf("+--------------------------------------+\n");
}

extern void set_memory_sizes(int size_flag)
{
    if (size_flag == HUGE_SIZE)
    {
        MAX_QTEXT_SIZE  = 4000;
        MAX_SYMBOLS     = 10000;

        SYMBOLS_CHUNK_SIZE = 5000;
        HASH_TAB_SIZE      = 512;

        MAX_OBJECTS = 640;

        MAX_ACTIONS      = 200;
        MAX_ADJECTIVES   = 50;
        MAX_DICT_ENTRIES = 2000;
        MAX_STATIC_DATA  = 10000;

        MAX_PROP_TABLE_SIZE_z = 30000;
        MAX_PROP_TABLE_SIZE_g = 60000;

        MAX_ABBREVS = 64;

        MAX_EXPRESSION_NODES = 100;
        MAX_VERBS = 200;
        MAX_VERBSPACE = 4096;
        MAX_LABELS = 1000;
        MAX_LINESPACE = 16000;

        MAX_STATIC_STRINGS = 8000;
        MAX_ZCODE_SIZE_z = 20000;
        MAX_ZCODE_SIZE_g = 40000;
        MAX_LINK_DATA_SIZE = 2000;

        MAX_LOW_STRINGS = 2048;

        MAX_TRANSCRIPT_SIZE = 200000;
        MAX_NUM_STATIC_STRINGS = 20000;

        MAX_CLASSES = 64;
        MAX_CLASS_TABLE_SIZE = 1000;

        MAX_OBJ_PROP_COUNT = 128;
        MAX_OBJ_PROP_TABLE_SIZE = 4096;

        MAX_INDIV_PROP_TABLE_SIZE = 15000;
        MAX_ARRAYS = 128;

        MAX_GLOBAL_VARIABLES_z = 240;
        MAX_GLOBAL_VARIABLES_g = 512;
    }
    if (size_flag == LARGE_SIZE)
    {
        MAX_QTEXT_SIZE  = 4000;
        MAX_SYMBOLS     = 6400;

        SYMBOLS_CHUNK_SIZE = 5000;
        HASH_TAB_SIZE      = 512;

        MAX_OBJECTS = 512;

        MAX_ACTIONS      = 200;
        MAX_ADJECTIVES   = 50;
        MAX_DICT_ENTRIES = 1300;
        MAX_STATIC_DATA  = 10000;

        MAX_PROP_TABLE_SIZE_z = 15000;
        MAX_PROP_TABLE_SIZE_g = 30000;

        MAX_ABBREVS = 64;

        MAX_EXPRESSION_NODES = 100;
        MAX_VERBS = 140;
        MAX_VERBSPACE = 4096;
        MAX_LINESPACE = 10000;

        MAX_LABELS = 1000;
        MAX_STATIC_STRINGS = 8000;
        MAX_ZCODE_SIZE_z = 20000;
        MAX_ZCODE_SIZE_g = 40000;
        MAX_LINK_DATA_SIZE = 2000;

        MAX_LOW_STRINGS = 2048;

        MAX_TRANSCRIPT_SIZE = 200000;
        MAX_NUM_STATIC_STRINGS = 20000;

        MAX_CLASSES = 64;
        MAX_CLASS_TABLE_SIZE = 1000;

        MAX_OBJ_PROP_COUNT = 64;
        MAX_OBJ_PROP_TABLE_SIZE = 2048;

        MAX_INDIV_PROP_TABLE_SIZE = 10000;
        MAX_ARRAYS = 128;

        MAX_GLOBAL_VARIABLES_z = 240;
        MAX_GLOBAL_VARIABLES_g = 512;
    }
    if (size_flag == SMALL_SIZE)
    {
        MAX_QTEXT_SIZE  = 4000;
        MAX_SYMBOLS     = 3000;

        SYMBOLS_CHUNK_SIZE = 2500;
        HASH_TAB_SIZE      = 512;

        MAX_OBJECTS = 300;

        MAX_ACTIONS      = 200;
        MAX_ADJECTIVES   = 50;
        MAX_DICT_ENTRIES = 700;
        MAX_STATIC_DATA  = 10000;

        MAX_PROP_TABLE_SIZE_z = 8000;
        MAX_PROP_TABLE_SIZE_g = 16000;

        MAX_ABBREVS = 64;

        MAX_EXPRESSION_NODES = 40;
        MAX_VERBS = 110;
        MAX_VERBSPACE = 2048;
        MAX_LINESPACE = 10000;
        MAX_LABELS = 1000;

        MAX_STATIC_STRINGS = 8000;
        MAX_ZCODE_SIZE_z = 10000;
        MAX_ZCODE_SIZE_g = 20000;
        MAX_LINK_DATA_SIZE = 1000;

        MAX_LOW_STRINGS = 1024;

        MAX_TRANSCRIPT_SIZE = 100000;
        MAX_NUM_STATIC_STRINGS = 10000;

        MAX_CLASSES = 32;
        MAX_CLASS_TABLE_SIZE = 800;

        MAX_OBJ_PROP_COUNT = 64;
        MAX_OBJ_PROP_TABLE_SIZE = 1024;

        MAX_INDIV_PROP_TABLE_SIZE = 5000;
        MAX_ARRAYS = 64;

        MAX_GLOBAL_VARIABLES_z = 240;
        MAX_GLOBAL_VARIABLES_g = 256;
    }

    /* Regardless of size_flag... */
    MAX_SOURCE_FILES = 256;
    MAX_INCLUSION_DEPTH = 5;
    MAX_LOCAL_VARIABLES_z = 16;
    MAX_LOCAL_VARIABLES_g = 32;
    DICT_WORD_SIZE_z = 6;
    DICT_WORD_SIZE_g = 9;
    NUM_ATTR_BYTES_z = 6;
    NUM_ATTR_BYTES_g = 7;

    adjust_memory_sizes();
}

extern void adjust_memory_sizes()
{
  if (!glulx_mode) {
    MAX_ZCODE_SIZE = MAX_ZCODE_SIZE_z;
    MAX_PROP_TABLE_SIZE = MAX_PROP_TABLE_SIZE_z;
    MAX_GLOBAL_VARIABLES = MAX_GLOBAL_VARIABLES_z;
    MAX_LOCAL_VARIABLES = MAX_LOCAL_VARIABLES_z;
    DICT_WORD_SIZE = DICT_WORD_SIZE_z;
    NUM_ATTR_BYTES = NUM_ATTR_BYTES_z;
  }
  else {
    MAX_ZCODE_SIZE = MAX_ZCODE_SIZE_g;
    MAX_PROP_TABLE_SIZE = MAX_PROP_TABLE_SIZE_g;
    MAX_GLOBAL_VARIABLES = MAX_GLOBAL_VARIABLES_g;
    MAX_LOCAL_VARIABLES = MAX_LOCAL_VARIABLES_g;
    DICT_WORD_SIZE = DICT_WORD_SIZE_g;
    NUM_ATTR_BYTES = NUM_ATTR_BYTES_g;
  }
}

static void explain_parameter(char *command)
{   printf("\n");
    if (strcmp(command,"MAX_QTEXT_SIZE")==0)
    {   printf(
"  MAX_QTEXT_SIZE is the maximum length of a quoted string.  Increasing\n\
   by 1 costs 5 bytes (for lexical analysis memory).  Inform automatically\n\
   ensures that MAX_STATIC_STRINGS is at least twice the size of this.");
        return;
    }
    if (strcmp(command,"MAX_SYMBOLS")==0)
    {   printf(
"  MAX_SYMBOLS is the maximum number of symbols - names of variables, \n\
  objects, routines, the many internal Inform-generated names and so on.\n");
        return;
    }
    if (strcmp(command,"SYMBOLS_CHUNK_SIZE")==0)
    {   printf(
"  The symbols names are stored in memory which is allocated in chunks \n\
  of size SYMBOLS_CHUNK_SIZE.\n");
        return;
    }
    if (strcmp(command,"HASH_TAB_SIZE")==0)
    {   printf(
"  HASH_TAB_SIZE is the size of the hash tables used for the heaviest \n\
  symbols banks.\n");
        return;
    }
    if (strcmp(command,"MAX_OBJECTS")==0)
    {   printf(
"  MAX_OBJECTS is the maximum number of objects.  (If compiling a version-3 \n\
  game, 255 is an absolute maximum in any event.)\n");
        return;
    }
    if (strcmp(command,"MAX_ACTIONS")==0)
    {   printf(
"  MAX_ACTIONS is the maximum number of actions - that is, routines such as \n\
  TakeSub which are referenced in the grammar table.\n");
        return;
    }
    if (strcmp(command,"MAX_ADJECTIVES")==0)
    {   printf(
"  MAX_ADJECTIVES is the maximum number of different \"adjectives\" in the \n\
  grammar table.  Adjectives are misleadingly named: they are words such as \n\
  \"in\", \"under\" and the like.\n");
        return;
    }
    if (strcmp(command,"MAX_DICT_ENTRIES")==0)
    {   printf(
"  MAX_DICT_ENTRIES is the maximum number of words which can be entered \n\
  into the game's dictionary.  It costs 29 bytes to increase this by one.\n");
        return;
    }
    if (strcmp(command,"DICT_WORD_SIZE")==0)
    {   printf(
"  DICT_WORD_SIZE is the number of characters in a dictionary word. In \n\
  Z-code this is always 6 (only 4 are used in v3 games). In Glulx it \n\
  can be any number.\n");
        return;
    }
    if (strcmp(command,"NUM_ATTR_BYTES")==0)
    {   printf(
"  NUM_ATTR_BYTES is the space used to store attribute flags. Each byte \n\
  stores eight attribytes. In Z-code this is always 6 (only 4 are used in \n\
  v3 games). In Glulx it can be any number which is a multiple of four, \n\
  plus three.\n");
        return;
    }
    if (strcmp(command,"MAX_STATIC_DATA")==0)
    {   printf(
"  MAX_STATIC_DATA is the size of an array of integers holding initial \n\
  values for arrays and strings stored as ASCII inside the Z-machine.  It \n\
  should be at least 1024 but seldom needs much more.\n");
        return;
    }
    if (strcmp(command,"MAX_PROP_TABLE_SIZE")==0)
    {   printf(
"  MAX_PROP_TABLE_SIZE is the number of bytes allocated to hold the \n\
  properties table.\n");
        return;
    }
    if (strcmp(command,"MAX_ABBREVS")==0)
    {   printf(
"  MAX_ABBREVS is the maximum number of declared abbreviations.  It is not \n\
  allowed to exceed 64.\n");
        return;
    }
    if (strcmp(command,"MAX_ARRAYS")==0)
    {   printf(
"  MAX_ARRAYS is the maximum number of declared arrays.\n");
        return;
    }
    if (strcmp(command,"MAX_EXPRESSION_NODES")==0)
    {   printf(
"  MAX_EXPRESSION_NODES is the maximum number of nodes in the expression \n\
  evaluator's storage for parse trees.  In effect, it measures how \n\
  complicated algebraic expressions are allowed to be.  Increasing it by \n\
  one costs about 80 bytes.\n");
        return;
    }
    if (strcmp(command,"MAX_VERBS")==0)
    {   printf(
"  MAX_VERBS is the maximum number of verbs (such as \"take\") which can be \n\
  defined, each with its own grammar.  To increase it by one costs about\n\
  128 bytes.  A full game will contain at least 100.\n");
        return;
    }
    if (strcmp(command,"MAX_VERBSPACE")==0)
    {   printf(
"  MAX_VERBSPACE is the size of workspace used to store verb words, so may\n\
  need increasing in games with many synonyms: unlikely to exceed 4K.\n");
        return;
    }
    if (strcmp(command,"MAX_LABELS")==0)
    {   printf(
"  MAX_LABELS is the maximum number of label points in any one routine.\n\
  (If the -k debugging information switch is set, MAX_LABELS is raised to\n\
  a minimum level of 2000, as about twice the normal number of label points\n\
  are needed to generate tables of how source code corresponds to positions\n\
  in compiled code.)");
        return;
    }
    if (strcmp(command,"MAX_LINESPACE")==0)
    {   printf(
"  MAX_LINESPACE is the size of workspace used to store grammar lines, so \n\
  may need increasing in games with complex or extensive grammars.\n");
        return;
    }
    if (strcmp(command,"MAX_STATIC_STRINGS")==0)
    {
        printf(
"  MAX_STATIC_STRINGS is the size in bytes of a buffer to hold compiled\n\
  strings before they're written into longer-term storage.  2000 bytes is \n\
  plenty, allowing string constants of up to about 3000 characters long.\n\
  Inform automatically ensures that this is at least twice the size of\n\
  MAX_QTEXT_SIZE, to be on the safe side.");
        return;

    }
    if (strcmp(command,"MAX_ZCODE_SIZE")==0)
    {
        printf(
"  MAX_ZCODE_SIZE is the size in bytes of a buffer to hold compiled \n\
  code for a single routine.  (It applies to both Z-code and Glulx, \n\
  despite the name.)  As a guide, the longest library routine is \n\
  about 6500 bytes long in Z-code; about twice that in Glulx.");
        return;
    }
    if (strcmp(command,"MAX_LINK_DATA_SIZE")==0)
    {
        printf(
"  MAX_LINK_DATA_SIZE is the size in bytes of a buffer to hold module \n\
  link data before it's written into longer-term storage.  2000 bytes \n\
  is plenty.");
        return;
    }
    if (strcmp(command,"MAX_LOW_STRINGS")==0)
    {   printf(
"  MAX_LOW_STRINGS is the size in bytes of a buffer to hold all the \n\
  compiled \"low strings\" which are to be written above the synonyms table \n\
  in the Z-machine.  1024 is plenty.\n");
        return;
    }
    if (strcmp(command,"MAX_TRANSCRIPT_SIZE")==0)
    {   printf(
"  MAX_TRANSCRIPT_SIZE is only allocated for the abbreviations optimisation \n\
  switch, and has the size in bytes of a buffer to hold the entire text of\n\
  the game being compiled: it has to be enormous, say 100000 to 200000.\n");
        return;
    }
    if (strcmp(command,"MAX_CLASSES")==0)
    {   printf(
"  MAX_CLASSES maximum number of object classes which can be defined.  This\n\
  is cheap to increase.\n");
        return;
    }
    if (strcmp(command,"MAX_CLASS_TABLE_SIZE")==0)
    {   printf(
"  MAX_CLASS_TABLE_SIZE is the number of bytes allocated to hold the table \n\
  of properties to inherit from each class.\n");
        return;
    }
    if (strcmp(command,"MAX_INCLUSION_DEPTH")==0)
    {   printf(
"  MAX_INCLUSION_DEPTH is the number of nested includes permitted.\n");
        return;
    }
    if (strcmp(command,"MAX_SOURCE_FILES")==0)
    {   printf(
"  MAX_SOURCE_FILES is the number of source files that can be read in the \n\
  compilation.\n");
        return;
    }
    if (strcmp(command,"MAX_INDIV_PROP_TABLE_SIZE")==0)
    {   printf(
"  MAX_INDIV_PROP_TABLE_SIZE is the number of bytes allocated to hold the \n\
  table of ..variable values.\n");
        return;
    }
    if (strcmp(command,"MAX_OBJ_PROP_COUNT")==0)
    {   printf(
"  MAX_OBJ_PROP_COUNT is the maximum number of properties a single object \n\
  can have. (Glulx only)\n");
        return;
    }
    if (strcmp(command,"MAX_OBJ_PROP_TABLE_SIZE")==0)
    {   printf(
"  MAX_OBJ_PROP_TABLE_SIZE is the number of words allocated to hold a \n\
  single object's properties. (Glulx only)\n");
        return;
    }
    if (strcmp(command,"MAX_LOCAL_VARIABLES")==0)
    {   printf(
"  MAX_LOCAL_VARIABLES is the number of local variables (including \n\
  arguments) allowed in a procedure. (Glulx only)\n");
        return;
    }
    if (strcmp(command,"MAX_GLOBAL_VARIABLES")==0)
    {   printf(
"  MAX_GLOBAL_VARIABLES is the number of global variables allowed in the \n\
  program. (Glulx only)\n");
        return;
    }
    if (strcmp(command,"MAX_NUM_STATIC_STRINGS")==0)
    {
        printf(
"  MAX_NUM_STATIC_STRINGS is the maximum number of compiled strings \n\
  allowed in the program. (Glulx only)\n");
        return;

    }

    printf("No such memory setting as \"%s\"\n",command);

    return;
}

extern void memory_command(char *command)
{   int i, k, flag=0; int32 j;

    for (k=0; command[k]!=0; k++)
        if (islower(command[k])) command[k]=toupper(command[k]);

    if (command[0]=='?') { explain_parameter(command+1); return; }

    if (strcmp(command, "HUGE")==0) { set_memory_sizes(HUGE_SIZE); return; }
    if (strcmp(command, "LARGE")==0) { set_memory_sizes(LARGE_SIZE); return; }
    if (strcmp(command, "SMALL")==0) { set_memory_sizes(SMALL_SIZE); return; }
    if (strcmp(command, "LIST")==0)  { list_memory_sizes(); return; }
    for (i=0; command[i]!=0; i++)
    {   if (command[i]=='=')
        {   command[i]=0;
            j=(int32) atoi(command+i+1);
            if ((j==0) && (command[i+1]!='0'))
            {   printf("Bad numerical setting in $ command \"%s=%s\"\n",
                    command,command+i+1);
                return;
            }
            if (strcmp(command,"BUFFER_LENGTH")==0)
                flag=2;
            if (strcmp(command,"MAX_QTEXT_SIZE")==0)
            {   MAX_QTEXT_SIZE=j, flag=1;
                if (2*MAX_QTEXT_SIZE > MAX_STATIC_STRINGS)
                    MAX_STATIC_STRINGS = 2*MAX_QTEXT_SIZE;
            }
            if (strcmp(command,"MAX_SYMBOLS")==0)
                MAX_SYMBOLS=j, flag=1;
            if (strcmp(command,"MAX_BANK_SIZE")==0)
                flag=2;
            if (strcmp(command,"SYMBOLS_CHUNK_SIZE")==0)
                SYMBOLS_CHUNK_SIZE=j, flag=1;
            if (strcmp(command,"BANK_CHUNK_SIZE")==0)
                flag=2;
            if (strcmp(command,"HASH_TAB_SIZE")==0)
                HASH_TAB_SIZE=j, flag=1;
            if (strcmp(command,"MAX_OBJECTS")==0)
                MAX_OBJECTS=j, flag=1;
            if (strcmp(command,"MAX_ACTIONS")==0)
                MAX_ACTIONS=j, flag=1;
            if (strcmp(command,"MAX_ADJECTIVES")==0)
                MAX_ADJECTIVES=j, flag=1;
            if (strcmp(command,"MAX_DICT_ENTRIES")==0)
                MAX_DICT_ENTRIES=j, flag=1;
            if (strcmp(command,"DICT_WORD_SIZE")==0) 
            {   DICT_WORD_SIZE=j, flag=1;
                DICT_WORD_SIZE_g=DICT_WORD_SIZE_z=j;
            }
            if (strcmp(command,"NUM_ATTR_BYTES")==0) 
            {   NUM_ATTR_BYTES=j, flag=1;
                NUM_ATTR_BYTES_g=NUM_ATTR_BYTES_z=j;
            }
            if (strcmp(command,"MAX_STATIC_DATA")==0)
                MAX_STATIC_DATA=j, flag=1;
            if (strcmp(command,"MAX_OLDEPTH")==0)
                flag=2;
            if (strcmp(command,"MAX_ROUTINES")==0)
                flag=2;
            if (strcmp(command,"MAX_GCONSTANTS")==0)
                flag=2;
            if (strcmp(command,"MAX_PROP_TABLE_SIZE")==0)
            {   MAX_PROP_TABLE_SIZE=j, flag=1;
                MAX_PROP_TABLE_SIZE_g=MAX_PROP_TABLE_SIZE_z=j;
            }
            if (strcmp(command,"MAX_FORWARD_REFS")==0)
                flag=2;
            if (strcmp(command,"STACK_SIZE")==0)
                flag=2;
            if (strcmp(command,"STACK_LONG_SLOTS")==0)
                flag=2;
            if (strcmp(command,"STACK_SHORT_LENGTH")==0)
                flag=2;
            if (strcmp(command,"MAX_ABBREVS")==0)
                MAX_ABBREVS=j, flag=1;
            if (strcmp(command,"MAX_ARRAYS")==0)
                MAX_ARRAYS=j, flag=1;
            if (strcmp(command,"MAX_EXPRESSION_NODES")==0)
                MAX_EXPRESSION_NODES=j, flag=1;
            if (strcmp(command,"MAX_VERBS")==0)
                MAX_VERBS=j, flag=1;
            if (strcmp(command,"MAX_VERBSPACE")==0)
                MAX_VERBSPACE=j, flag=1;
            if (strcmp(command,"MAX_LABELS")==0)
                MAX_LABELS=j, flag=1;
            if (strcmp(command,"MAX_LINESPACE")==0)
                MAX_LINESPACE=j, flag=1;
            if (strcmp(command,"MAX_NUM_STATIC_STRINGS")==0)
                MAX_NUM_STATIC_STRINGS=j, flag=1;
            if (strcmp(command,"MAX_STATIC_STRINGS")==0)
            {   MAX_STATIC_STRINGS=j, flag=1;
                if (2*MAX_QTEXT_SIZE > MAX_STATIC_STRINGS)
                    MAX_STATIC_STRINGS = 2*MAX_QTEXT_SIZE;
            }
            if (strcmp(command,"MAX_ZCODE_SIZE")==0)
            {   MAX_ZCODE_SIZE=j, flag=1;
                MAX_ZCODE_SIZE_g=MAX_ZCODE_SIZE_z=j;
            }
            if (strcmp(command,"MAX_LINK_DATA_SIZE")==0)
                MAX_LINK_DATA_SIZE=j, flag=1;
            if (strcmp(command,"MAX_LOW_STRINGS")==0)
                MAX_LOW_STRINGS=j, flag=1;
            if (strcmp(command,"MAX_TRANSCRIPT_SIZE")==0)
                MAX_TRANSCRIPT_SIZE=j, flag=1;
            if (strcmp(command,"MAX_CLASSES")==0)
                MAX_CLASSES=j, flag=1;
            if (strcmp(command,"MAX_CLASS_TABLE_SIZE")==0)
                MAX_CLASS_TABLE_SIZE=j, flag=1;
            if (strcmp(command,"MAX_INCLUSION_DEPTH")==0)
                MAX_INCLUSION_DEPTH=j, flag=1;
            if (strcmp(command,"MAX_SOURCE_FILES")==0)
                MAX_SOURCE_FILES=j, flag=1;
            if (strcmp(command,"MAX_INDIV_PROP_TABLE_SIZE")==0)
                MAX_INDIV_PROP_TABLE_SIZE=j, flag=1;
            if (strcmp(command,"MAX_OBJ_PROP_TABLE_SIZE")==0)
                MAX_OBJ_PROP_TABLE_SIZE=j, flag=1;
            if (strcmp(command,"MAX_OBJ_PROP_COUNT")==0)
                MAX_OBJ_PROP_COUNT=j, flag=1;
            if (strcmp(command,"MAX_LOCAL_VARIABLES")==0)
            {   MAX_LOCAL_VARIABLES=j, flag=1;
                MAX_LOCAL_VARIABLES_g=MAX_LOCAL_VARIABLES_z=j;
            }
            if (strcmp(command,"MAX_GLOBAL_VARIABLES")==0)
            {   MAX_GLOBAL_VARIABLES=j, flag=1;
                MAX_GLOBAL_VARIABLES_g=MAX_GLOBAL_VARIABLES_z=j;
            }

            if (flag==0)
                printf("No such memory setting as \"%s\"\n", command);
            if (flag==2)
            printf("The Inform 5 memory setting \"%s\" has been withdrawn.\n\
It should be safe to omit it (putting nothing in its place).\n", command);
            return;
        }
    }
    printf("No such memory $ command as \"%s\"\n",command);
}

extern void print_memory_usage(void)
{
    printf("Properties table used %d\n",
        properties_table_size);
    printf("Allocated a total of %ld bytes of memory\n",
        (long int) malloced_bytes);
}

/* ========================================================================= */
/*   Data structure management routines                                      */
/* ------------------------------------------------------------------------- */

extern void init_memory_vars(void)
{   malloced_bytes = 0;
}

extern void memory_begin_pass(void) { }

extern void memory_allocate_arrays(void) { }

extern void memory_free_arrays(void) { }

/* ========================================================================= */

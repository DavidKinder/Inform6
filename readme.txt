This is version 6.32N of the Inform compiler,
copyright (c) Graham Nelson 1993 - 2011
Full release notes and instructions are available at
http://www.inform-fiction.org/
and
http://www.ifarchive.org/indexes/if-archiveXinfocomXcompilersXinform6.html


This is a minor update to Inform 6.32. The only changes between 6.32N and
6.32 are the application of the following patches:

Change the Glulx Unsigned__Compare() veneer routine to a much more efficient
implementation (Andrew Plotkin)

Improve the array bounds checking for the $MAX_PROP_TABLE_SIZE,
$MAX_INDIV_PROP_TABLE_SIZE, $MAX_OBJ_PROP_TABLE_SIZE, $SYMBOLS_CHUNK_SIZE,
$MAX_ZCODE_SIZE, $MAX_QTEXT_SIZE and $MAX_STATIC_DATA settings
(Andrew Plotkin)

Fix the error message when too many global variables are declared
(Andrew Plotkin)

Tidy the output printed when the statistics (-s) switch is used
(David Kinder)


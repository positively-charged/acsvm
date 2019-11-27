#include <stdio.h>
#include <setjmp.h>
#include <stdarg.h>

#include "common/misc.h"
#include "common/list.h"
#include "common/str.h"
#include "vm.h"
#include "debug.h"

void dgb_show_waiting_scripts( struct vm* vm ) {
   struct list_iter i;
   list_iterate( &vm->waiting_scripts, &i );
   while ( ! list_end( &i ) ) {
      struct script* script = list_data( &i );
      printf( "script %d\n", script->number );
      list_next( &i );
   }
}

/**
 * Prints the contents of an `options` struct.
 */
void dbg_show_options( struct options* options ) {
   printf( "object_file: %s\n", options->object_file ?
      options->object_file : "NULL" );
   printf( "libraries:" );
   if ( list_size( &options->libraries ) > 0 ) {
      printf( "\n" );
      struct list_iter i;
      list_iterate( &options->libraries, &i );
      while ( ! list_end( &i ) ) {
         const char* path = list_data( &i );
         printf( " - %s\n", path );
         list_next( &i );
      }
   }
   else {
      printf( " None\n" );
   }
}

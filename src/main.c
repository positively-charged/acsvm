#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <setjmp.h>

#include "common/misc.h"
#include "common/mem.h"
#include "common/str.h"
#include "common/list.h"
#include "vm.h"
#include "debug.h"

static void init_options( struct options* options );
static bool read_options( struct options* options, char* argv[] );
static void print_usage( char* path );

i32 main( i32 argc, char* argv[] ) {
   mem_init();
   i32 result = EXIT_FAILURE;
   struct options options;
   init_options( &options );
   if ( ! read_options( &options, argv ) ) {
      print_usage( argv[ 0 ] );
      goto deinit_memory;
   }

#if 0
   
   FILE* fh = fopen( options.object_file, "rb" );
   if ( ! fh ) {
      printf( "error: failed to open file: %s\n", options.object_file );
      goto finish;
   }
   fseek( fh, 0, SEEK_END );
   size_t size = ftell( fh );
   rewind( fh );
   char* data = malloc( sizeof( char ) * size );
   fread( data, sizeof( char ), size, fh );
   fclose( fh );
#endif
   vm_run( &options );
   deinit_memory:
   mem_free_all();
   finish:
   return result;
}

static void init_options( struct options* options ) {
   options->object_file = NULL;
   list_init( &options->libraries );
}

static bool read_options( struct options* options, char* argv[] ) {
   char** args = argv + 1;

   // Read options.
   while ( *args && args[ 0 ][ 0 ] == '-' ) {
      switch ( args[ 0 ][ 1 ] ) {
      case 'l':
         ++args;
         if ( *args == NULL ) {
            printf( "fatal error: "
               "missing library path argument for -l option" );
            return false;
         }
         list_append( &options->libraries, *args );
         ++args;
         break;
      default:
         return false;
      }
   }

   // Read object file.
   if ( *args ) {
      options->object_file = *args;
   }
   else {
      printf( "fatal error: missing object file argument\n" );
      return false;
   }
   return true;
}

static void print_usage( char* path ) {
   printf(
      "Usage: %s [options] <object-file>\n"
      "Parameters:\n"
      "  <object-file>: path to file to run.\n"
      "Options:\n"
      "  -l <path>     Load a library\n"
      "",
      path );
}

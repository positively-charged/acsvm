#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <setjmp.h>

#include "common.h"
#include "common/mem.h"
#include "common/str.h"
#include "common/list.h"
#include "vm.h"

static void init_options( struct options* options );
static void read_options( struct options* options, char* argv[] );
static void print_usage( char* path );

i32 main( i32 argc, char* argv[] ) {
   mem_init();
   i32 result = EXIT_FAILURE;
   struct options options;
   init_options( &options );
   read_options( &options, argv );
   if ( ! options.object_file ) {
      print_usage( argv[ 0 ] );
      goto deinit_memory;
   }

   struct file_request request;
   vm_init_file_request( &request );
   vm_load_file( &request, options.object_file );
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
   vm_run( request.data, request.size );
   deinit_data:
   free( request.data );
   deinit_memory:
   mem_free_all();
   finish:
   return result;
}

static void init_options( struct options* options ) {
   options->object_file = NULL;
}

static void read_options( struct options* options, char* argv[] ) {
   char** args = argv + 1;
   if ( *args ) {
      options->object_file = *args;
   }
}

static void print_usage( char* path ) {
   printf(
      "Usage: %s [object-file]\n"
      "",
      path );
}

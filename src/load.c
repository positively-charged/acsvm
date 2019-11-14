/*
 * This module is responsible for reading the contents of files.
 **/

#include <stdio.h>
#include <setjmp.h>
#include <string.h>

#include "common.h"
#include "common/mem.h"
#include "common/str.h"
#include "common/list.h"
#include "vm.h"

void vm_init_file_request( struct file_request* request ) {
   request->err = FILEREQUESTERR_NONE;
   request->data = NULL;
   request->size = 0;
}

void vm_load_file( struct file_request* request, const char* path ) {
   FILE* fh = fopen( path, "rb" );
   if ( ! fh ) {
      request->err = FILEREQUESTERR_OPEN;
      return;
   }
   fseek( fh, 0, SEEK_END );
   size_t size = ftell( fh );
   rewind( fh );
   u8* data = malloc( sizeof( char ) * size );
   fread( data, sizeof( char ), size, fh );
   request->data = data;
   request->size = size;
   fclose( fh );
}

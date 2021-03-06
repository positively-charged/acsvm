#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "common/misc.h"
#include "common/mem.h"
#include "common/str.h"

static void adjust_buffer( struct str* string, int length );

void str_init( struct str* str ) {
   str->length = 0;
   str->buffer_length = 0;
   str->value = NULL;
}

void str_deinit( struct str* str ) {
   if ( str->value ) {
      mem_free( str->value );
   }
}

void str_copy( struct str* str, const char* value, int length ) {
   adjust_buffer( str, length );
   memcpy( str->value, value, length );
   str->value[ length ] = 0;
   str->length = length;
}

// If necessary, increases the buffer size enough to fit a string of the
// specified length and the terminating null character. To select the size, an
// initial size is doubled until the size is appropriate.
static void adjust_buffer( struct str* string, int length ) {
   int required_length = length + 1;
   if ( string->buffer_length < required_length ) {
      int buffer_length = 1;
      while ( buffer_length < required_length ) {
         buffer_length <<= 1;
      }
      str_grow( string, buffer_length );
   }
}

void str_grow( struct str* string, int length ) {
   string->value = mem_realloc( string->value, length );
   string->buffer_length = length;
}

void str_append( struct str* str, const char* value ) {
   int length = strlen( value );
   int new_length = str->length + length;
   adjust_buffer( str, new_length );
   memcpy( str->value + str->length, value, length );
   str->value[ new_length ] = 0;
   str->length = new_length;
}

void str_append_sub( struct str* str, const char* cstr, int length ) {
   adjust_buffer( str, str->buffer_length + length );
   memcpy( str->value + str->length, cstr, length );
   str->length += length;
   str->value[ str->length ] = '\0';
}

void str_append_number( struct str* str, int number ) {
   char buffer[ 11 ];
   sprintf( buffer, "%d", number );
   str_append( str, buffer );
}

void str_append_format( struct str* str, const char* format, ... ) {
   va_list args;
   va_start( args, format );
   str_append_format_va( str, format, &args );
   va_end( args );
}

void str_append_format_va( struct str* str, const char* format,
   va_list* args ) {
   va_list args_copy;
   va_copy( args_copy, *args );
   int length = vsnprintf( NULL, 0, format, *args );
   if ( length >= 0 ) {
      if ( str->length + length >= str->buffer_length ) {
         str_grow( str, str->length + length + 1 );
      }
      vsnprintf( str->value + str->length, str->buffer_length - str->length,
         format, args_copy );
      str->length += length;
   }
   va_end( args_copy );
}

void str_clear( struct str* str ) {
   str->length = 0;
   if ( str->value ) {
      str->value[ 0 ] = 0;
   }
}

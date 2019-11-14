#ifndef SRC_COMMON_STR_H
#define SRC_COMMON_STR_H

struct str {
   char* value;
   int length;
   int buffer_length;
};

void str_init( struct str* );
void str_deinit( struct str* );
void str_copy( struct str*, const char* value, int length );
void str_grow( struct str*, int length );
void str_append( struct str*, const char* cstr );
void str_append_sub( struct str*, const char* cstr, int length );
void str_append_number( struct str*, int number );
void str_append_format( struct str* str, const char* format, ... );
void str_append_format_va( struct str* str, const char* format,
   va_list* args );
void str_clear( struct str* );

#endif

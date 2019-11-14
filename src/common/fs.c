#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "common.h"
#include "str.h"
#include "fs.h"

#if OS_WINDOWS

#include <windows.h>

bool c_read_fileid( struct fileid* fileid, const char* path ) {
   HANDLE fh = CreateFile( path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0,
      NULL );
   if ( fh != INVALID_HANDLE_VALUE ) {
      BY_HANDLE_FILE_INFORMATION detail;
      if ( GetFileInformationByHandle( fh, &detail ) ) {
         fileid->id_high = detail.nFileIndexHigh;
         fileid->id_low = detail.nFileIndexLow;
         CloseHandle( fh );
         return true;
      }
      else {
         CloseHandle( fh );
         return false;
      }
   }
   else {
      return false;
   }
}

bool c_same_fileid( struct fileid* a, struct fileid* b ) {
   return ( a->id_high == b->id_high && a->id_low == b->id_low );
}

#else

#include <sys/stat.h>

bool c_read_fileid( struct fileid* fileid, const char* path ) {
   struct stat buff;
   if ( stat( path, &buff ) == -1 ) {
      return false;
   }
   fileid->device = buff.st_dev;
   fileid->number = buff.st_ino;
   return true;
}

bool c_same_fileid( struct fileid* a, struct fileid* b ) {
   return ( a->device == b->device && a->number == b->number );
}

#endif

// Miscellaneous
// ==========================================================================

int alignpad( int size, int align_size ) {
   int i = size % align_size;
   if ( i ) {
      return align_size - i;
   }
   else {
      return 0;
   }
}

#if OS_WINDOWS

bool c_read_full_path( const char* path, struct str* str ) {
   const int max_path = MAX_PATH + 1;
   if ( str->buffer_length < max_path ) { 
      str_grow( str, max_path );
   }
   str->length = GetFullPathName( path, max_path, str->value, NULL );
   if ( GetFileAttributes( str->value ) != INVALID_FILE_ATTRIBUTES ) {
      int i = 0;
      while ( str->value[ i ] ) {
         if ( str->value[ i ] == '\\' ) {
            str->value[ i ] = '/';
         }
         ++i;
      }
      return true;
   }
   else {
      return false;
   }
}

void fs_strip_trailing_pathsep( struct str* path ) {
   while ( path->length - 1 > 0 &&
      path->value[ path->length - 1 ] == '\\' ) {
      path->value[ path->length - 1 ] = '\0';
      --path->length;
   }
}

const char* fs_get_tempdir( void ) {
   static bool got_path = false;
   static CHAR path[ MAX_PATH + 1 ];
   if ( ! got_path ) {
      DWORD length = GetTempPathA( sizeof( path ), path );
      if ( length - 1 > 0 && path[ length - 1 ] == '\\' ) {
         path[ length - 1 ] = '\0';
      }
      got_path = true;
   }
   return path;
}

bool fs_create_dir( const char* path, struct fs_result* result ) {
   return ( CreateDirectory( path, NULL ) != 0 );
}

void fs_init_query( struct fs_query* query, const char* path ) {
   query->path = path;
   query->err = 0;
   query->attrs_obtained = false;
}

WIN32_FILE_ATTRIBUTE_DATA* get_file_attrs( struct fs_query* query ) {
   // Request file statistics once.
   if ( ! query->attrs_obtained ) {
      bool result = GetFileAttributesEx( query->path, GetFileExInfoStandard,
         &query->attrs );
      if ( ! result ) {
         query->err = GetLastError();
         return NULL;
      }
      query->attrs_obtained = true;
   }
   return &query->attrs;
}

bool fs_exists( struct fs_query* query ) {
   return ( get_file_attrs( query ) != NULL );
}

bool fs_is_dir( struct fs_query* query ) {
   WIN32_FILE_ATTRIBUTE_DATA* attrs = get_file_attrs( query );
   return ( attrs && ( attrs->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) );
}

#define UNIX_EPOCH 116444736000000000LL

bool fs_get_mtime( struct fs_query* query, struct fs_timestamp* timestamp ) {
   WIN32_FILE_ATTRIBUTE_DATA* attrs = get_file_attrs( query );
   if ( attrs ) {
      ULARGE_INTEGER mtime = { { attrs->ftLastWriteTime.dwLowDateTime,
         attrs->ftLastWriteTime.dwHighDateTime } };
      timestamp->value = ( mtime.QuadPart - UNIX_EPOCH ) / 10000000LL;
      return true;
   }
   else {
      return false;
   }
}

bool fs_delete_file( const char* path ) {
   return ( DeleteFileA( path ) == TRUE );
}

#else

#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#ifdef __APPLE__
#include <limits.h> // PATH_MAX on OS X is defined in limits.h
#elif defined __linux__
#include <linux/limits.h> // And on Linux it is in linux/limits.h
#endif

bool c_read_full_path( const char* path, struct str* str ) {
   str_grow( str, PATH_MAX + 1 );
   if ( realpath( path, str->value ) ) {
      str->length = strlen( str->value );
      return true;
   }
   else {
      return false;
   }
}

void fs_strip_trailing_pathsep( struct str* path ) {
   while ( path->length - 1 > 0 &&
      path->value[ path->length - 1 ] == '/' ) {
      path->value[ path->length - 1 ] = '\0';
      --path->length;
   }
}

void fs_init_query( struct fs_query* query, const char* path ) {
   query->path = path;
   query->err = 0;
   query->stat_obtained = false;
}

struct stat* get_query_stat( struct fs_query* query ) {
   // Request file statistics once.
   if ( ! query->stat_obtained ) {
      if ( stat( query->path, &query->stat ) != 0 ) {
         query->err = errno;
         return NULL;
      }
      query->stat_obtained = true;
   }
   return &query->stat;
}

bool fs_exists( struct fs_query* query ) {
   return ( get_query_stat( query ) != NULL );
}

bool fs_is_dir( struct fs_query* query ) {
   struct stat* stat = get_query_stat( query );
   return ( stat && S_ISDIR( stat->st_mode ) );
}

bool fs_get_mtime( struct fs_query* query, struct fs_timestamp* timestamp ) {
   struct stat* stat = get_query_stat( query );
   if ( ! stat ) {
      return false;
   }
   timestamp->value = stat->st_mtime;
   return true;
}

bool fs_create_dir( const char* path, struct fs_result* result ) {
   static const mode_t mode =
      S_IRUSR | S_IWUSR | S_IXUSR |
      S_IRGRP | S_IXGRP |
      S_IROTH | S_IXOTH;
   if ( mkdir( path, mode ) == 0 ) {
      result->err = 0;
      return true;
   }
   else {
      result->err = errno;
      return false;
   }
}

const char* fs_get_tempdir( void ) {
   return "/tmp";
}

bool fs_delete_file( const char* path ) {
   return ( unlink( path ) == 0 );
}

#endif

void c_extract_dirname( struct str* path ) {
   while ( true ) {
      if ( path->length == 0 ) {
         break;
      }
      --path->length;
      char ch = path->value[ path->length ];
      path->value[ path->length ] = 0;
      if ( ch == '/' || ch == '\\' ) {
         break;
      }
   }
}

void fs_get_file_contents( const char* path, struct file_contents* contents ) {
   FILE* fh = fopen( path, "r" );
   if ( ! fh ) {
      contents->obtained = false;
      contents->err = errno;
      return;
   }
   fseek( fh, 0, SEEK_END );
   size_t size = ftell( fh );
   fseek( fh, 0, SEEK_SET );
   contents->data = mem_alloc( size );
   fread( contents->data, size, 1, fh );
   fclose( fh );
   contents->obtained = true;
   contents->err = 0;
}

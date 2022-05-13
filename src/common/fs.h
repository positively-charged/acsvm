#ifndef SRC_COMMON_FS_H
#define SRC_COMMON_FS_H

/**
 * File system related functionality
 */

#if OS_WINDOWS

#include <windows.h>
#include <time.h>

// NOTE: Volume information is not included. Maybe add it later.
struct fileid {
   int id_high;
   int id_low;
};

#define NEWLINE_CHAR "\r\n"
#define OS_PATHSEP "\\"

struct fs_query {
   WIN32_FILE_ATTRIBUTE_DATA attrs;
   const char* path;
   DWORD err;
   bool attrs_obtained;
};

struct fs_timestamp {
   time_t value;
};

#define strcasecmp _stricmp

#else

#include <sys/types.h>
#include <sys/stat.h>

struct fileid {
   dev_t device;
   ino_t number;
};

#define NEWLINE_CHAR "\n"
#define OS_PATHSEP "/"

struct fs_query {
   const char* path;
   struct stat stat;
   int err;
   bool stat_obtained;
};

struct fs_timestamp {
   time_t value;
};

#endif

struct file_contents {
   char* data;
   int err;
   bool obtained;
};

struct fs_result {
   int err;
};

struct str;

bool c_read_fileid( struct fileid*, const char* path );
bool c_same_fileid( struct fileid*, struct fileid* );
bool c_read_full_path( const char* path, struct str* );
void c_extract_dirname( struct str* );

int alignpad( int size, int align_size );

void fs_init_query( struct fs_query* query, const char* path );
bool fs_exists( struct fs_query* query );
bool fs_is_dir( struct fs_query* query );
bool fs_get_mtime( struct fs_query* query, struct fs_timestamp* timestamp );
bool fs_create_dir( const char* path, struct fs_result* result );
const char* fs_get_tempdir( void );
void fs_get_file_contents( const char* path, struct file_contents* contents );
void fs_strip_trailing_pathsep( struct str* path );
bool fs_delete_file( const char* path );

#endif

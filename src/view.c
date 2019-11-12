/*
 * This module is responsible for browsing of an ACS object file.
 **/

#include <string.h>
#include <setjmp.h>
#include <ctype.h>

#include "common.h"
#include "vm.h"

struct chunk {
   char name[ 5 ];
   const char* data; 
   int size;
   enum {
      CHUNK_UNKNOWN,
      CHUNK_ARAY,
      CHUNK_AINI,
      CHUNK_AIMP,
      CHUNK_ASTR,
      CHUNK_MSTR,
      CHUNK_LOAD,
      CHUNK_FUNC,
      CHUNK_FNAM,
      CHUNK_MINI,
      CHUNK_MIMP,
      CHUNK_MEXP,
      CHUNK_SPTR,
      CHUNK_SFLG,
      CHUNK_SVCT,
      CHUNK_STRL,
      CHUNK_STRE,
      CHUNK_JUMP,
      CHUNK_ALIB,
      CHUNK_SARY,
      CHUNK_FARY,
      CHUNK_ATAG,
      CHUNK_SNAM,
   } type;
};

static void init_chunk( struct chunk* chunk, const char* data );
static bool find_chunk( struct object* object, struct chunk* chunk, int type );
static int get_chunk_type( const char* name );
static void load_sptr( struct vm* vm, struct object* object,
   struct chunk* chunk );
static void load_strl( struct vm* vm, struct object* object,
   struct chunk* chunk );
static void load_aray( struct vm* vm, struct object* object,
   struct chunk* chunk );
static void load_aini( struct vm* vm, struct object* object,
   struct chunk* chunk );

void vm_init_object( struct object* object, const char* data, int size ) {
   struct header {
      char id[ 4 ];
      int offset;
   };
   struct header header;
   memcpy( &header, data, sizeof( header ) );
   object->data = data;
   object->size = size;
   object->format = FORMAT_UNKNOWN;
   object->chunk_offset = header.offset;
   object->chunk_end = size;
   object->dummy_offset = header.offset;
   object->indirect_format = false;
   object->small_code = false;
   if ( memcmp( header.id, "ACSE", 4 ) == 0 ) {
      object->format = FORMAT_BIG_E;
   }
   else if ( memcmp( header.id, "ACSe", 4 ) == 0 ) {
      object->format = FORMAT_LITTLE_E;
   }
   else if ( memcmp( header.id, "ACS\0", 4 ) == 0 ) {
      const char* format = data + header.offset - sizeof( int );
      if ( memcmp( format, "ACSE", 4 ) == 0 ||
         memcmp( format, "ACSe", 4 ) == 0 ) {
         object->format = format[ 3 ] == 'E' ? FORMAT_BIG_E : FORMAT_LITTLE_E;
         memcpy( &object->chunk_offset, format - sizeof( int ),
            sizeof( int ) );
         object->indirect_format = true;
         object->chunk_end = header.offset - ( sizeof( int ) * 2 );
      }
      else {
         object->format = FORMAT_ZERO;
      }
   }
   if ( object->format == FORMAT_LITTLE_E ) {
      object->small_code = true;
   }
}

void vm_list_chunks( struct vm* vm, struct object* object ) {
   const char* data = object->data + object->chunk_offset;
   while ( data < object->data + object->size ) {
      struct chunk chunk;
      init_chunk( &chunk, data );
      if ( chunk.type == CHUNK_SPTR ) {
         load_sptr( vm, object, &chunk );
      }
      else if ( chunk.type == CHUNK_STRL ) {
         load_strl( vm, object, &chunk );
      }
      else if ( chunk.type == CHUNK_ARAY ) {
         load_aray( vm, object, &chunk );
      }
      data += sizeof( int ) * 2 + chunk.size;
   }
   data = object->data + object->chunk_offset;
   while ( data < object->data + object->size ) {
      struct chunk chunk;
      init_chunk( &chunk, data );
      if ( chunk.type == CHUNK_AINI ) {
         load_aini( vm, object, &chunk );
      }
      data += sizeof( int ) * 2 + chunk.size;
   }
}

static void init_chunk( struct chunk* chunk, const char* data ) {
   memcpy( chunk->name, data, 4 );
   chunk->name[ 4 ] = 0;
   data += sizeof( int );
   memcpy( &chunk->size, data, sizeof( int ) );
   data += sizeof( int );
   chunk->data = data;
   chunk->type = get_chunk_type( chunk->name );
}

static bool find_chunk( struct object* object, struct chunk* chunk,
   int type ) {
   const char* data = object->data + object->chunk_offset;
   while ( data < object->data + object->size ) {
      init_chunk( chunk, data );
      if ( chunk->type == type ) {
         return true;
      }
      data += sizeof( int ) * 2 + chunk->size;
   }
   return false;
}

static int get_chunk_type( const char* name ) {
   char buff[ 5 ];
   memcpy( buff, name, 4 );
   for ( int i = 0; i < 4; ++i ) {
      buff[ i ] = toupper( buff[ i ] );
   }
   buff[ 4 ] = 0;
   static const struct {
      const char* name;
      int type;
   } supported[] = {
      { "ARAY", CHUNK_ARAY },
      { "AINI", CHUNK_AINI },
      { "AIMP", CHUNK_AIMP },
      { "ASTR", CHUNK_ASTR },
      { "MSTR", CHUNK_MSTR },
      { "LOAD", CHUNK_LOAD },
      { "FUNC", CHUNK_FUNC },
      { "FNAM", CHUNK_FNAM },
      { "MINI", CHUNK_MINI },
      { "MIMP", CHUNK_MIMP },
      { "MEXP", CHUNK_MEXP },
      { "SPTR", CHUNK_SPTR },
      { "SFLG", CHUNK_SFLG },
      { "SVCT", CHUNK_SVCT },
      { "STRL", CHUNK_STRL },
      { "STRE", CHUNK_STRE },
      { "JUMP", CHUNK_JUMP },
      { "ALIB", CHUNK_ALIB },
      { "SARY", CHUNK_SARY },
      { "FARY", CHUNK_FARY },
      { "ATAG", CHUNK_ATAG },
      { "SNAM", CHUNK_SNAM },
      { "", CHUNK_UNKNOWN }
   };
   int i = 0;
   while ( supported[ i ].type != CHUNK_UNKNOWN &&
      strcmp( buff, supported[ i ].name ) != 0 ) {
      ++i;
   }
   return supported[ i ].type;
}

static void load_sptr( struct vm* vm, struct object* object,
   struct chunk* chunk ) {
   int size = 0;
   while ( size < chunk->size ) {
      if ( object->indirect_format ) {
         struct {
            short number;
            char type;
            char num_param;
            int offset;
         } entry;
         memcpy( &entry, chunk->data + size, sizeof( entry ) );
         size += sizeof( entry );

         struct script* script = mem_alloc( sizeof( *script ) );
         script->next = NULL;
         script->next_waiting = NULL;
         script->waiting = NULL;
         script->waiting_tail = NULL;
         script->number = entry.number;
         script->offset = entry.offset;
         script->delay_amount = 0;
         script->state = SCRIPTSTATE_TERMINATED;
         memset( script->vars, 0, sizeof( script->vars ) );
         list_append( &vm->scripts, script );
/*
         number = ( int ) entry.number;
         type = ( int ) entry.type;
         num_param = ( int ) entry.num_param;
         offset = entry.offset;
         if ( size < chunk->size ) {
            memcpy( &entry, chunk->data + size, sizeof( entry ) );
            end_offset = entry.offset;
         }
*/
      }
      else {
      }
   }
}

static void load_strl( struct vm* vm, struct object* object,
   struct chunk* chunk ) {
   const char* data = chunk->data;
   data += sizeof( int );
   int count = 0;
   memcpy( &count, data, sizeof( int ) );
   data += sizeof( int );
   data += sizeof( int );
   for ( int i = 0; i < count; ++i ) {
      struct str* string = mem_alloc( sizeof( *string ) );
      str_init( string );
      int offset = 0;
      memcpy( &offset, data, sizeof( int ) );
      data += sizeof( int );
      const char* ch = chunk->data + offset;
      int k = 0;
      while ( true ) {
         char decoded = *ch;
         if ( chunk->type == CHUNK_STRE ) {
            decoded = decoded ^ ( offset * 157135 + k / 2 );
            ++k;
         }
         if ( ! decoded ) {
            break;
         }
         char text[ 2 ] = { decoded };
         str_append( string, text );
         ++ch;
      }
      list_append( &vm->strings, string );
   }
}

static void load_aray( struct vm* vm, struct object* object,
   struct chunk* chunk ) {
   struct {
      int index;
      int size;
   } entry;
   int count = chunk->size / sizeof( entry );
   vm->arrays = mem_alloc( sizeof( *vm->arrays ) * count );
   for ( int i = 0; i < count; ++i ) {
      memcpy( &entry, chunk->data + i * sizeof( entry ), sizeof( entry ) );
      vm->arrays[ entry.index ].size = entry.size;
      vm->arrays[ entry.index ].array = true;
      vm->arrays[ entry.index ].elements = mem_alloc(
         sizeof( *vm->arrays[ entry.index ].elements ) * entry.size );
      memset( vm->arrays[ entry.index ].elements, 0,
         sizeof( *vm->arrays[ entry.index ].elements ) * entry.size );
   }
}

static void load_aini( struct vm* vm, struct object* object,
   struct chunk* chunk ) {
   int index = 0;
   memcpy( &index, chunk->data, sizeof( index ) );
   memcpy( vm->arrays[ index ].elements, chunk->data + sizeof( index ),
      chunk->size - sizeof( int ) );
}

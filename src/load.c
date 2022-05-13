/**
 * This file is responsible for loading modules.
 */

#include <stdio.h>
#include <setjmp.h>
#include <string.h>
#include <ctype.h>

#include "common/misc.h"
#include "common/mem.h"
#include "common/str.h"
#include "common/list.h"
#include "vm.h"
#include "debug.h"

enum { ORIGINAL_SCRIPT_VAR_LIMIT = 20 };

struct chunk {
   char name[ 5 ];
   const u8* data;
   i32 size;
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

struct func_entry {
   u8 num_param;
   u8 size;
   u8 has_return;
   u8 padding;
   i32 offset;
};

static void load_module( struct vm* vm, const char* name, const char* path );
static struct module* alloc_module( void );
static void read_chunks( struct vm* vm, struct object* object );
static void init_chunk( struct chunk* chunk, const u8* data );
static bool find_chunk( struct object* object, struct chunk* chunk, int type );
static int get_chunk_type( const char* name );
static void load_sptr( struct vm* vm, struct object* object,
   struct chunk* chunk );
static void set_script_type( struct script* script, i32 type );
static void load_strl( struct vm* vm, struct object* object,
   struct chunk* chunk );
static void load_aray( struct vm* vm, struct object* object,
   struct chunk* chunk );
static void load_mini( struct vm* vm, struct object* object,
   struct chunk* chunk );
static void load_aini( struct vm* vm, struct object* object,
   struct chunk* chunk );
static void load_func( struct vm* vm, struct object* object,
   struct chunk* chunk );
static void init_func( struct func* func );
static void load_sary_fary( struct vm* vm, struct object* object,
   struct chunk* chunk );
static struct script* get_script( struct vm* vm, i32 number );
static struct script* get_script_in_module( struct vm* vm,
   struct module* module, i32 number );
static void load_sflg( struct vm* vm, struct object* object,
   struct chunk* chunk );
static void load_svct( struct vm* vm, struct object* object,
   struct chunk* chunk );
static void load_snam( struct vm* vm, struct object* object,
   struct chunk* chunk );
static const char* read_chunk_string( struct vm* vm, struct chunk* chunk,
   i32 offset );
static void load_load( struct vm* vm, struct object* object,
   struct chunk* chunk );
static void load_mexp( struct vm* vm, struct object* object,
   struct chunk* chunk );
static void load_mimp( struct vm* vm, struct object* object,
   struct chunk* chunk );
static void load_aimp( struct vm* vm, struct object* object,
   struct chunk* chunk );
static void load_fnam( struct vm* vm, struct object* object,
   struct chunk* chunk );
static void link_modules( struct vm* vm );
static void do_imports( struct vm* vm, struct module* module );
static struct module* find_module( struct vm* vm, const char* name );
static void link_vars( struct vm* vm, struct module* module );
static struct var* find_var_in_module( struct module* module,
   const char* name );
static void link_funcs( struct vm* vm, struct module* module );
static struct func* find_func_in_module( struct module* module,
   const char* name );

/**
 * Loads all the modules that will be executed. This includes the main module
 * and libraries.
 */
void vm_load_modules( struct vm* vm ) {
   //read_modules( vm );
   //link_modules( vm );
   struct list_iter i;
   list_iterate( &vm->options->modules, &i );
   while ( ! list_end( &i ) ) {
      struct module_arg* arg = list_data( &i );
      load_module( vm, arg->name, arg->path );
      list_next( &i );
   }
   link_modules( vm );
   //load_libs( vm );
   //vm_load_module( vm, "", vm->options->object_file );
}

static void load_module( struct vm* vm, const char* name, const char* path ) {
   struct module* module = alloc_module();
   strncpy( module->name, name, sizeof( module->name ) );

   struct file_request request;
   vm_init_file_request( &request );
   vm_load_file( &request, path );

   vm_init_object( &module->object, request.data, request.size );
   module->object.module = module;

   //vm->object = object;
   switch ( module->object.format ) {
   case FORMAT_ZERO:
   case FORMAT_BIG_E:
   case FORMAT_LITTLE_E:
      break;
   default:
      printf( "error: unsupported format\n" );
      return;
   }
   read_chunks( vm, &module->object );

   list_append( &vm->modules, module );
}

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
   u8* data = mem_alloc( sizeof( char ) * size );
   fread( data, sizeof( char ), size, fh );
   request->data = data;
   request->size = size;
   fclose( fh );
}

static struct module* alloc_module( void ) {
   struct module* module = mem_alloc( sizeof( *module ) );
   module->name[ 0 ] = '\0';
   list_init( &module->imports );
   list_init( &module->scripts );
   list_init( &module->strings );
   list_init( &module->waiting_scripts );
   for ( isize i = 0; i < ARRAY_SIZE( module->vars ); ++i ) {
      module->vars[ i ].name = "";
      module->vars[ i ].elements = &module->vars[ i ].value;
      module->vars[ i ].value = 0;
      module->vars[ i ].size = 0;
      module->vars[ i ].array = false;
      module->vars[ i ].imported = false;
      module->map_vars[ i ] = &module->vars[ i ];
   }
   module->func_table.entries = NULL;
   module->func_table.size = 0;
   return module;
}

void vm_init_object( struct object* object, const u8* data, int size ) {
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
      const u8* format = data + header.offset - sizeof( int );
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

static void read_chunks( struct vm* vm, struct object* object ) {
   const u8* data = object->data + object->chunk_offset;
   // Load independent chunks.
   // Load scripts and function chunks first before reading the chunks that
   // depend on scripts and functions.
   while ( data < object->data + object->size ) {
      struct chunk chunk;
      init_chunk( &chunk, data );
      switch ( chunk.type ) {
      case CHUNK_SPTR:
         load_sptr( vm, object, &chunk );
         break;
      case CHUNK_STRL:
         load_strl( vm, object, &chunk );
         break;
      case CHUNK_ARAY:
         load_aray( vm, object, &chunk );
         break;
      case CHUNK_FUNC:
         load_func( vm, object, &chunk );
         break;
      case CHUNK_LOAD:
         load_load( vm, object, &chunk );
         break;
      default:
         break;
      }
      data += sizeof( int ) * 2 + chunk.size;
   }

   // Load dependent chunks.
   data = object->data + object->chunk_offset;
   while ( data < object->data + object->size ) {
      struct chunk chunk;
      init_chunk( &chunk, data );
      switch ( chunk.type ) {
      case CHUNK_MINI:
         load_mini( vm, object, &chunk );
         break;
      case CHUNK_AINI:
         load_aini( vm, object, &chunk );
         break;
      case CHUNK_SARY:
      case CHUNK_FARY:
         load_sary_fary( vm, object, &chunk );
         break;
      case CHUNK_SFLG:
         load_sflg( vm, object, &chunk );
         break;
      case CHUNK_SVCT:
         load_svct( vm, object, &chunk );
         break;
      case CHUNK_SNAM:
         load_snam( vm, object, &chunk );
         break;
      case CHUNK_MEXP:
         load_mexp( vm, object, &chunk );
         break;
      case CHUNK_MIMP:
         load_mimp( vm, object, &chunk );
         break;
      case CHUNK_AIMP:
         load_aimp( vm, object, &chunk );
         break;
      case CHUNK_FNAM:
         load_fnam( vm, object, &chunk );
         break;
      default:
         break;
      }
      data += sizeof( int ) * 2 + chunk.size;
   }
}

static void init_chunk( struct chunk* chunk, const u8* data ) {
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
   const u8* data = object->data + object->chunk_offset;
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
         script->name = null;
         script->number = entry.number;
         set_script_type( script, entry.type );
         script->flags = 0;
         script->start = entry.offset;
         script->arrays = NULL;
         script->num_vars = ORIGINAL_SCRIPT_VAR_LIMIT;
         script->num_arrays = 0;
         script->total_array_size = 0;
         list_append( &vm->scripts, script );
         list_append( &object->module->scripts, script );
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

static void set_script_type( struct script* script, i32 type ) {
   switch ( type ) {
   case SCRIPTTYPE_CLOSED:
   case SCRIPTTYPE_OPEN:
   case SCRIPTTYPE_RESPAWN:
   case SCRIPTTYPE_DEATH:
   case SCRIPTTYPE_ENTER:
   case SCRIPTTYPE_PICKUP:
   case SCRIPTTYPE_BLUERETURN:
   case SCRIPTTYPE_REDRETURN:
   case SCRIPTTYPE_WHITERETURN:
   case SCRIPTTYPE_LIGHTNING:
   case SCRIPTTYPE_UNLOADING:
   case SCRIPTTYPE_DISCONNECT:
   case SCRIPTTYPE_RETURN:
   case SCRIPTTYPE_EVENT:
   case SCRIPTTYPE_KILL:
   case SCRIPTTYPE_REOPEN:
      script->type = type;
      break;
   default:
      script->type = SCRIPTTYPE_UNKNOWN;
   }
}

static void load_strl( struct vm* vm, struct object* object,
   struct chunk* chunk ) {
   const u8* data = chunk->data;
   data += sizeof( int );
   int count = 0;
   memcpy( &count, data, sizeof( int ) );
   data += sizeof( int );
   data += sizeof( int );
   for ( int i = 0; i < count; ++i ) {
      struct str* string = mem_alloc( sizeof( *string ) );
      str_init( string );
      str_append( string, "" );
      int offset = 0;
      memcpy( &offset, data, sizeof( int ) );
      data += sizeof( int );
      const u8* ch = chunk->data + offset;
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
      list_append( &object->module->strings, string );
   }
}

static void load_aray( struct vm* vm, struct object* object,
   struct chunk* chunk ) {
   struct {
      i32 index;
      i32 size;
   } entry;
   i32 count = chunk->size / sizeof( entry );
   for ( i32 i = 0; i < count; ++i ) {
      memcpy( &entry, chunk->data + i * sizeof( entry ), sizeof( entry ) );
      object->module->vars[ entry.index ].size = entry.size;
      object->module->vars[ entry.index ].array = true;
      object->module->vars[ entry.index ].elements = mem_alloc(
         sizeof( *object->module->vars[ entry.index ].elements ) * entry.size );
      memset( object->module->vars[ entry.index ].elements, 0,
         sizeof( *object->module->vars[ entry.index ].elements ) * entry.size );
   }
}

static void load_mini( struct vm* vm, struct object* object,
   struct chunk* chunk ) {
   const u8* data = chunk->data;
   i32 data_left = chunk->size;
   i32 first_var = 0;
   //if ( data_left < sizeof( first_var ) ) {
   //   warn_expect_chunk_data( viewer, data_left, sizeof( first_var ),
   //      "index of first variable" );
   //   return;
   //}
   memcpy( &first_var, data, sizeof( first_var ) );
   data += sizeof( first_var );
   data_left -= sizeof( first_var );
   i32 initz = 0;
   i32 total_initz = data_left / sizeof( initz );
   //printf( "first-var=%d total-initializers=%d\n", first_var, total_initz );
   for ( i32 i = 0; i < total_initz; ++i ) {
      memcpy( &initz, data, sizeof( initz ) );
      data += sizeof( initz );
      data_left -= sizeof( initz );
      i32* var = vm_get_map_var( vm, object->module, first_var + i );
      var[ 0 ] = initz;
      //printf( "index=%d value=%d\n", first_var + i, initz );
   }
   if ( data_left > 0 ) {
      //warn_unused_chunk_data( viewer, data_left );
   }
}

static void load_aini( struct vm* vm, struct object* object,
   struct chunk* chunk ) {
   int index = 0;
   memcpy( &index, chunk->data, sizeof( index ) );
   // TODO: Make sure the index refers to an array.
   memcpy( object->module->vars[ index ].elements, chunk->data + sizeof( index ),
      chunk->size - sizeof( int ) );
}

static void load_func( struct vm* vm, struct object* object,
   struct chunk* chunk ) {
   struct func_entry entry;
   isize total_funcs = chunk->size / sizeof( entry );
   struct func* entries = mem_alloc( sizeof( entries[ 0 ] ) * total_funcs );
   int data_left = chunk->size % sizeof( entry );
   if ( data_left > 0 ) {
      v_diag( vm, DIAG_WARN,
         "chunk %s %d byte%s of data%s, which is too small to "
         "form a function entry (%d bytes)", ( total_funcs == 0 ) ? "has" :
         "will have", data_left, ( data_left == 1 ) ? "" : "s",
         ( total_funcs== 0 ) ? "" : " left at the end", sizeof( entry ) );
   }
   for ( int i = 0; i < total_funcs; ++i ) {
      init_func( &entries[ i ] );
      memcpy( &entry, chunk->data + ( i * sizeof( entry ) ), sizeof( entry ) );
      entries[ i ].module = object->module;
      entries[ i ].name = "";
      entries[ i ].params = entry.num_param;
      entries[ i ].local_size = entry.size;
      entries[ i ].start = entry.offset;
      //printf( "index=%d params=%d local-size=%d has-return=%d offset=%d\n", i,
      //   entry.num_param, entry.size, entry.has_return, entry.offset );
/*
      if ( ! offset_in_object_file( object, entry.offset ) ) {
         diag( viewer, DIAG_WARN,
            "offset (%d) points outside the object file, so the function code "
            "will not be shown", entry.offset );  
         continue;
      }
*/
      if ( entry.offset == 0 ) {
         entries[ i ].imported = true;
         //printf( "(imported)\n" );
      }
      else {
         //show_pcode( viewer, object, entry.offset,
         //   calc_code_size( viewer, object, entry.offset ) );
      }
   }

   object->module->func_table.entries = entries;
   object->module->func_table.size = total_funcs;
}

static void init_func( struct func* func ) {
   func->local_size = 0;
   func->arrays = NULL;
   func->num_arrays = 0;
   func->total_array_size = 0;
   func->imported = false;
}

static void load_sary_fary( struct vm* vm, struct object* object,
   struct chunk* chunk ) {
   const u8* data = chunk->data;
   i16 index = 0;
   //expect_chunk_data( viewer, chunk, data, sizeof( index ) );
   memcpy( &index, data, sizeof( index ) );
   data += sizeof( index );
   i32 size = 0; // Size of a script array.
   i32 total_size = 0; // Size of all script arrays combined.
   i32 total_arrays = ( chunk->size - sizeof( index ) ) / sizeof( size );
   struct script_array* arrays = mem_alloc( sizeof( arrays[ 0 ] ) *
      total_arrays );
   //printf( "%s=%d total-script-arrays=%d\n",
    //  ( chunk->type == CHUNK_FARY ) ? "function" : "script",
   //   index, total_arrays );
   for ( i32 i = 0; i < total_arrays; ++i ) {
      //expect_chunk_data( viewer, chunk, data, sizeof( size ) );
      memcpy( &size, data, sizeof( size ) );
      data += sizeof( size );
      arrays[ i ].start = total_size;
      arrays[ i ].size = size;
      total_size += size;
   }
   if ( chunk->type == CHUNK_FARY ) {
      struct func* func = vm_find_func( vm, object->module, index );
      func->arrays = arrays;
      func->num_arrays = total_arrays;
      func->total_array_size = total_size;
   }
   else {
      struct script* script = get_script( vm, index );
      script->arrays = arrays;
      script->num_arrays = total_arrays;
      script->total_array_size = total_size;
   }
}

static struct script* get_script( struct vm* vm, i32 number ) {
   struct list_iter i;
   list_iterate( &vm->scripts, &i );
   while ( ! list_end( &i ) ) {
      struct script* script = list_data( &i );
      if ( script->number == number ) {
         return script;
      }
      list_next( &i );
   }
   v_diag( vm, DIAG_FATALERR,
      "invalid script requested (number of script is %d) ", number );
   v_bail( vm );
   return NULL;
}

static struct script* get_script_in_module( struct vm* vm,
   struct module* module, i32 number ) {
   struct list_iter i;
   list_iterate( &module->scripts, &i );
   while ( ! list_end( &i ) ) {
      struct script* script = list_data( &i );
      if ( script->number == number ) {
         return script;
      }
      list_next( &i );
   }
   v_diag( vm, DIAG_FATALERR,
      "invalid script requested (number of script is %d) ", number );
   v_bail( vm );
   return NULL;
}

static void load_sflg( struct vm* vm, struct object* object,
   struct chunk* chunk ) {
   i32 pos = 0;
   while ( pos < chunk->size ) {
      struct {
         i16 number;
         u16 flags;
      } entry;
      //expect_chunk_data( viewer, chunk, chunk->data + pos, sizeof( entry ) );
      memcpy( &entry, chunk->data + pos, sizeof( entry ) );
      pos += sizeof( entry );
      struct script* script = get_script( vm, entry.number );
      //printf( "script=%hd ", entry.number );
      u16 flags = entry.flags;
      //printf( "flags=" );
      // Net flag.
      if ( flags & FLAG_NET ) {
         script->flags |= FLAG_NET;
         flags &= ~FLAG_NET;
         //printf( "net(0x%x)", FLAG_NET );
         if ( flags != 0 ) {
            //printf( "|" );
         }
      }
      // Clientside flag.
      if ( flags & FLAG_CLIENTSIDE ) {
         script->flags |= FLAG_CLIENTSIDE;
         flags &= ~FLAG_CLIENTSIDE;
         //printf( "clientside(0x%x)", FLAG_CLIENTSIDE );
         if ( flags != 0 ) {
           // printf( "|" );
         }
      }
      // Unknown flags.
      if ( flags != 0 ) {
         printf( "unknown(0x%x)", flags );
      }
      //printf( "\n" );
   }
}

static void load_svct( struct vm* vm, struct object* object,
   struct chunk* chunk ) {
   i32 pos = 0;
   while ( pos < chunk->size ) {
      struct {
         i16 number;
         i16 size;
      } entry;
      //expect_chunk_data( viewer, chunk, chunk->data + pos, sizeof( entry ) ); 
      memcpy( &entry, chunk->data + pos, sizeof( entry ) );
      pos += sizeof( entry );
      struct script* script = get_script( vm, entry.number );
      script->num_vars = entry.size;
      //printf( "script=%hd new-size=%hd\n", entry.number, entry.size );
   }
}

static void load_snam( struct vm* vm, struct object* object,
   struct chunk* chunk ) {
   const u8* data = chunk->data;
   i32 total_names = 0;
   // expect_chunk_data( viewer, chunk, data, sizeof( total_names ) );
   memcpy( &total_names, data, sizeof( total_names ) );
   data += sizeof( total_names );
   // printf( "total-named-scripts=%d\n", total_names );
   for ( i32 i = 0; i < total_names; ++i ) {
      i32 offset = 0;
      // expect_chunk_data( viewer, chunk, data, sizeof( offset ) );
      memcpy( &offset, data, sizeof( offset ) );
      data += sizeof( offset );
      enum { INITIAL_NAMEDSCRIPT_NUMBER = -1 };
      // expect_chunk_offset_in_chunk( viewer, chunk, offset );
      //printf( "script-number=%d script-name=\"%s\"\n",
      //   INITIAL_NAMEDSCRIPT_NUMBER - i,
      i32 number = INITIAL_NAMEDSCRIPT_NUMBER - i;
      struct script* script = get_script_in_module( vm, object->module,
         number );
      script->name = read_chunk_string( vm, chunk, offset );
   }
}

static const char* read_chunk_string( struct vm* vm, struct chunk* chunk,
   i32 offset ) {
   // Make sure the string is NUL-terminated.
   if ( ! memchr( chunk->data + offset, '\0', chunk->size - offset ) ) {
      v_diag( vm, DIAG_FATALERR,
         "a string at offset %d in %s chunk is not NUL-terminated", offset,
         chunk->name );
      v_bail( vm );
   }
   return ( const char* ) ( chunk->data + offset );
}

static void load_load( struct vm* vm, struct object* object,
   struct chunk* chunk ) {
   const u8* data = chunk->data;
   i32 data_left = chunk->size;
   while ( data_left > 0 ) {
      const u8* data_nul = memchr( data, '\0', data_left );
      if ( data_nul ) {
         // Ignore empty strings. Empty strings are used as padding at the end
         // of the chunk for data alignment purposes.
         if ( data != data_nul ) {
            //printf( "imported-module=%s\n", ( const char* ) data );
            struct import* import = mem_alloc( sizeof( *import ) );
            import->module_name = ( const char* ) data;
            import->module = null;
            list_append( &object->module->imports, import );
         }

         // Plus one for NUL character.
         i32 data_size = ( data_nul - data ) + 1;
         //if ( data_size > 1 ) {
            
         //}

         data += data_size;
         data_left -= data_size;
      }
      else {
         v_diag( vm, DIAG_FATALERR,
            "library name at offset %d is not NUL-terminated",
            data - chunk->data );
         v_bail( vm );
      }
   }
}

static void load_mexp( struct vm* vm, struct object* object,
   struct chunk* chunk ) {
   const u8* data = chunk->data;
   i32 total_names = 0;
   //expect_chunk_data( viewer, chunk, data, sizeof( total_names ) );
   memcpy( &total_names, data, sizeof( total_names ) );
   data += sizeof( total_names );
   //printf( "table-size=%d\n", total_names );
   for ( i32 i = 0; i < total_names; ++i ) {
      i32 offset = 0;
      //expect_chunk_data( viewer, chunk, data, sizeof( offset ) );
      memcpy( &offset, data, sizeof( offset ) );
      data += sizeof( offset );
      //expect_chunk_offset_in_chunk( viewer, chunk, offset );
      const char* name = read_chunk_string( vm, chunk, offset );
      object->module->vars[ i ].name = name;
/*
      if ( viewer->attr_format ) {
         printf( "index=%d offset=%d name=%s\n", i, offset,
            read_chunk_string( viewer, chunk, offset ) );
      }
      else {
         printf( "[%d] offset=%d %s\n", i, offset,
            read_chunk_string( viewer, chunk, offset ) );
      }
*/
   }
}

static void load_mimp( struct vm* vm, struct object* object,
   struct chunk* chunk ) {
   const u8* data = chunk->data;
   i32 data_left = chunk->size;
   while ( data_left > 0 ) {
      i32 index = 0;
      if ( data_left < sizeof( index ) ) {
         //warn_expect_chunk_data( viewer, data_left, sizeof( index ),
         //   "a variable index" );
         return;
      }
      memcpy( &index, data, sizeof( index ) );
      data += sizeof( index );
      data_left -= sizeof( index );
      const u8* data_nul = memchr( data, '\0', data_left );
      if ( ! data_nul ) {
         //diag( viewer, DIAG_WARN,
         //   "missing a NUL-terminated string containing the variable name for "
         //   "variable with index %d", index );
         return;
      }
      //printf( "index=%d name=%s\n", index, ( const char* ) data );
      const char* name = ( const char* ) data;
      object->module->vars[ index ].name = name;
      object->module->vars[ index ].imported = true;
      i32 data_size = ( data_nul - data ) + 1; // Plus one for NUL character.
      data += data_size;
      data_left -= data_size;
   }
}

static void load_aimp( struct vm* vm, struct object* object,
   struct chunk* chunk ) {
   const u8* data = chunk->data;
   u32 total_arrays = 0;
   //expect_chunk_data( viewer, chunk, data, sizeof( total_arrays ) );
   memcpy( &total_arrays, data, sizeof( total_arrays ) );
   data += sizeof( total_arrays );
   //printf( "total-imported-arrays=%d\n", total_arrays );
   i32 i = 0;
   while ( i < total_arrays ) {
      u32 index = 0;
      //expect_chunk_data( viewer, chunk, data, sizeof( index ) );
      memcpy( &index, data, sizeof( index ) );
      data += sizeof( index );
      u32 size = 0;
      //expect_chunk_data( viewer, chunk, data, sizeof( size ) );
      memcpy( &size, data, sizeof( size ) );
      data += sizeof( size );
      const char* name = read_chunk_string( vm, chunk,
         ( i32 ) ( data - chunk->data ) );
      object->module->vars[ index ].name = name;
      object->module->vars[ index ].imported = true;
      object->module->vars[ index ].array = true;
/*
      if ( viewer->attr_format ) {
         printf( "index=%u name=%s size=%u\n", index, string, size );
      }
      else {
         printf( "index=%u %s[%u]\n", index, string, size );
      }*/
      data += strlen( name ) + 1; // Plus one for NUL character.
      ++i;
   }
}

static void load_fnam( struct vm* vm, struct object* object,
   struct chunk* chunk ) {
   const u8* data = chunk->data;
   i32 data_left = chunk->size;
   i32 total_names = 0;
   if ( data_left < sizeof( total_names ) ) {
      //warn_expect_chunk_data( viewer, data_left, sizeof( total_names ),
      //   "total-names field" );
      return;
   }
   memcpy( &total_names, data, sizeof( total_names ) );
   data += sizeof( total_names );
   data_left -= sizeof( total_names );
   //printf( "total-names=%d\n", total_names );
   for ( i32 i = 0; i < total_names; ++i ) {
      i32 offset = 0;
      if ( data_left < sizeof( offset ) ) {
         //warn_expect_chunk_data( viewer, data_left, sizeof( offset ),
         //   "a name offset" );
         return;
      }
      memcpy( &offset, data, sizeof( offset ) );
      data += sizeof( offset );
      data_left -= sizeof( offset );
      //if ( ! chunk_offset_in_chunk( chunk, offset ) ) {
      //   diag( viewer, DIAG_WARN,
       //     "for function %d, name offset (%d) points outside the boundaries "
       //     "of the chunk", i, offset );
       //  continue;
     // }
      if ( ! memchr( chunk->data + offset, '\0', chunk->size - offset ) ) {
       //  diag( viewer, DIAG_WARN,
       //     "string at offset %d, containing the name for function %d, is not "
       //     "NUL-terminated", offset, i );
         continue;
      }
      
      const char* name = ( const char* ) ( chunk->data + offset );
      object->module->func_table.entries[ i ].name = name;
      //printf( "function=%d offset=%d name=%s\n", i, offset,
      //   ( const char* ) ( chunk->data + offset ) );
   }
}

static void link_modules( struct vm* vm ) {
   struct list_iter i;
   list_iterate( &vm->modules, &i );
   while ( ! list_end( &i ) ) {
      struct module* module = list_data( &i );
      do_imports( vm, module );
      link_vars( vm, module );
      link_funcs( vm, module );
      list_next( &i );
   }
}

static void do_imports( struct vm* vm, struct module* module ) {
   struct list_iter i;
   list_iterate( &module->imports, &i );
   while ( ! list_end( &i ) ) {
      struct import* import = list_data( &i );
      struct module* imported_module = find_module( vm, import->module_name );
      if ( ! imported_module ) {
         v_diag( vm, DIAG_FATALERR,
            "module `%s` importing an unknown module (`%s`)",
            module->name, import->module_name );
         v_bail( vm );
      }
      import->module = imported_module;
      list_next( &i );
   }
}

static struct module* find_module( struct vm* vm, const char* name ) {
   struct list_iter i;
   list_iterate( &vm->modules, &i );
   while ( ! list_end( &i ) ) {
      struct module* module = list_data( &i );
      if ( strcmp( name, module->name ) == 0 ) {
         return module;
      }
      list_next( &i );
   }
   return null;
}

static void link_vars( struct vm* vm, struct module* module ) {
   for ( isize i = 0; i < ARRAY_SIZE( module->vars ); ++i ) {
      if ( module->vars[ i ].imported ) {
         struct var* var = null;
         struct list_iter k;
         list_iterate( &module->imports, &k );
         while ( ! list_end( &k ) && var == null ) {
            struct import* import = list_data( &k );
            var = find_var_in_module( import->module, module->vars[ i ].name );
            list_next( &k );
         }
         if ( var == null ) {
            v_diag( vm, DIAG_FATALERR,
               "failed to import `%s` variable", module->vars[ i ].name );
            v_bail( vm );
         }
         module->map_vars[ i ] = var;
      }
   }
}

static struct var* find_var_in_module( struct module* module,
   const char* name ) {
   for ( isize i = 0; i < ARRAY_SIZE( module->vars ); ++i ) {
      if ( strcmp( module->vars[ i ].name, name ) == 0 ) {
         return &module->vars[ i ];
      }
   }
   return null;
}

static void link_funcs( struct vm* vm, struct module* module ) {
   module->func_table.linked_entries = mem_alloc(
      sizeof( module->func_table.linked_entries[ 0 ] ) *
      module->func_table.size );
   for ( isize i = 0; i < module->func_table.size; ++i ) {
      if ( module->func_table.entries[ i ].imported ) {
         struct func* func = null;
         struct list_iter k;
         list_iterate( &module->imports, &k );
         while ( ! list_end( &k ) && func == null ) {
            struct import* import = list_data( &k );
            func = find_func_in_module( import->module,
               module->func_table.entries[ i ].name );
            list_next( &k );
         }
         if ( func == null ) {
            v_diag( vm, DIAG_FATALERR,
               "failed to import `%s` function",
               module->func_table.entries[ i ].name );
            v_bail( vm );
         }
         module->func_table.linked_entries[ i ] = func;
      }
      else {
         module->func_table.linked_entries[ i ] =
            &module->func_table.entries[ i ];
      }
   }
}

static struct func* find_func_in_module( struct module* module,
   const char* name ) {
   for ( isize i = 0; i < module->func_table.size; ++i ) {
      if ( strcmp( module->func_table.entries[ i ].name, name ) == 0 ) {
         return &module->func_table.entries[ i ];
      }
   }
   return null;
}

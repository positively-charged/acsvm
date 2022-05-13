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

void dbg_dump_stack( struct vm* vm, struct turn* turn ) {
   i32 elements = turn->stack - turn->stack_start;
   v_diag( vm, DIAG_DBG, "stack size: %d", elements );
   for ( isize i = 0; i < elements; ++i ) {
      v_diag( vm, DIAG_DBG, "  [%d] = %d", i, turn->stack_start[ i ] );
   }
}

void dbg_dump_funcs( struct vm* vm, struct turn* turn ) {
   dbg_dump_func_table( vm, &turn->module->func_table );
}

void dbg_dump_func_table( struct vm* vm, struct func_table* table ) {
   v_diag( vm, DIAG_DBG, "total-functions=%d", table->size );
   for ( i32 i = 0; i < table->size; ++i ) {
      v_diag( vm, DIAG_DBG,
         "index=%d params=%d local-size=%d has-return=%d start=%d", i,
         0,
         table->entries[ i ].local_size,
         0,
         table->entries[ i ].start );
      if ( table->entries[ i ].start == 0 ) {
         v_diag( vm, DIAG_DBG, "(imported)" );
      }
      dbg_dump_script_arrays( vm, table->entries[ i ].arrays,
         table->entries[ i ].num_arrays );
   }
}

void dbg_dump_script_arrays( struct vm* vm, struct script_array* arrays,
   i32 total ) {
   v_diag( vm, DIAG_DBG, "total-local-arrays=%d", total );
   for ( i32 i = 0; i < total; ++i ) {
      v_diag( vm, DIAG_DBG,
         "  array-index=%d array-size=%d", i, arrays[ i ].size );
   }
}

void dbg_dump_scripts( struct vm* vm ) {
   v_diag( vm, DIAG_DBG, "total-scripts=%d", list_size( &vm->scripts ) );
   struct list_iter i;
   list_iterate( &vm->scripts, &i );
   while ( ! list_end( &i ) ) {
      struct script* script = list_data( &i );
      dbg_dump_script( vm, script );
      list_next( &i );
   }
}

void dbg_dump_script( struct vm* vm, struct script* script ) {
   v_diag( vm, DIAG_DBG | DIAG_MULTI_PART,
      "script=%d type=%d", script->number, script->type );

   v_diag_more( vm, " flags=" );
   if ( script->flags != 0 ) {
      bool added_flag = false;
      if ( ( script->flags & FLAG_NET ) != 0 ) {
         v_diag_more( vm, "net" );
         added_flag = true;
      }
      if ( ( script->flags & FLAG_CLIENTSIDE ) != 0 ) {
         if ( added_flag ) {
            v_diag_more( vm, "|" );
         }
         v_diag_more( vm, "clientside" );
         added_flag = true;
      }
   }
   else {
      v_diag_more( vm, "none" );
   }

   v_diag_more( vm, "\n" );

   dbg_dump_script_arrays( vm, script->arrays, script->num_arrays );
}

void dbg_dump_local_vars( struct vm* vm, struct turn* turn ) {
   v_diag( vm, DIAG_DBG,
      "total-local-vars=%d", turn->script->script->num_vars );
   for ( isize i = 0; i < turn->script->script->num_vars; ++i ) {
      v_diag( vm, DIAG_DBG, "  [%d] = %d", i, turn->script->vars[ i ] );
   }
}

void dbg_dump_module_strings( struct vm* vm, struct module* module ) {
   v_diag( vm, DIAG_DBG, "total-strings=%d", list_size( &module->strings ) );
   i32 index = 0;
   struct list_iter i;
   list_iterate( &module->strings, &i );
   while ( ! list_end( &i ) ) {
      struct str* string = list_data( &i );
      v_diag( vm, DIAG_DBG, "  [%d] \"%s\"", index, string->value );
      ++index;
      list_next( &i );
   }
}


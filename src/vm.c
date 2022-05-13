#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <setjmp.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>

#include "common/misc.h"
#include "common/mem.h"
#include "common/str.h"
#include "common/list.h"
#include "vm.h"
#include "pcode.h"
#include "debug.h"

static void init_vm( struct vm* vm, struct options* options );
static void create_master_str_table( struct vm* vm );
static isize count_initial_strings( struct vm* vm );
static void run( struct vm* vm );
static void start_open_scripts( struct vm* vm );
static void start_script( struct vm* vm, struct module* module,
   struct script* script );
static struct instance* create_instance( struct script* script );
static bool scripts_waiting( struct vm* vm );
static void next_tic( struct vm* vm );
static void run_module( struct vm* vm, struct module* module );
static bool script_ready( struct vm* vm, struct module* module );
static void run_delayed_script( struct vm* machine, struct module* module,
   struct instance* script );
static struct instance* deq_script( struct module* module );
static void enq_script( struct module* module, struct instance* script );
static bool run_sooner( struct instance* a, struct instance* b );
static void add_suspended_script( struct vm* vm, struct instance* script );
static void init_turn( struct turn* turn, struct module* module,
   struct instance* script );
static void run_script( struct vm* vm, struct turn* turn );
static bool script_finished( struct turn* turn );

void vm_run( struct options* options ) {
   struct vm vm;
   init_vm( &vm, options );
   jmp_buf bail;
   if ( setjmp( bail ) == 0 ) {
      vm.bail = &bail;
      vm_load_modules( &vm );
      create_master_str_table( &vm );
      run( &vm );
   }

   //free( request.data );
}

void init_vm( struct vm* vm, struct options* options ) {
   vm->options = options;
//   vm->object = NULL;
   list_init( &vm->modules );
   list_init( &vm->scripts );
   list_init( &vm->waiting_scripts );
   list_init( &vm->suspended_scripts );
   str_init( &vm->msg );
   srand( time( NULL ) );
   for ( isize i = 0; i < ARRAY_SIZE( vm->world_arrays ); ++i ) {
      vector_init( &vm->world_arrays[ i ], sizeof( i32 ) );
   }
   for ( isize i = 0; i < ARRAY_SIZE( vm->global_arrays ); ++i ) {
      vector_init( &vm->global_arrays[ i ], sizeof( i32 ) );
   }
   vm->tics = 0;
   vm->num_active_scripts = 0;
   vm->call_stack = NULL;
   str_init( &vm->temp_str );
   vector_init( &vm->strings, sizeof( struct indexed_string* ) );
}

static void create_master_str_table( struct vm* vm ) {
   isize total_initial_strings = count_initial_strings( vm );
   //printf( "%ld\n", total_initial_strings );
}

static isize count_initial_strings( struct vm* vm ) {
   isize count = 0;
   struct list_iter i;
   list_iterate( &vm->modules, &i );
   while ( ! list_end( &i ) ) {
      struct module* module = list_data( &i );
      count += list_size( &module->strings );
      list_next( &i );
   }
   return count;
}

struct indexed_string* vm_add_string( struct vm* vm, const char* value ) {
   for ( isize i = 0; i < vm->strings.capacity; ++i ) {
      struct vector_result result = vector_get( &vm->strings, i );
      struct indexed_string* string = result.element;
      if ( strcmp( string->value->value, value ) == 0 ) {
         return string;
      }
   }
   struct indexed_string* string = vector_append( &vm->strings );
   return string;
} 

void run( struct vm* vm  ) {
   start_open_scripts( vm );
   while ( scripts_waiting( vm ) ) {
      struct list_iter i;
      list_iterate( &vm->modules, &i );
      while ( ! list_end( &i ) ) {
         struct module* module = list_data( &i );
         run_module( vm, module );
         list_next( &i );
      }
      next_tic( vm );
   }
}

/**
 * Queues for execution every OPEN script from every module.
 */
static void start_open_scripts( struct vm* vm ) {
   struct list_iter i;
   list_iterate( &vm->modules, &i );
   while ( ! list_end( &i ) ) {
      struct module* module = list_data( &i );
      struct list_iter k;
      list_iterate( &module->scripts, &k );
      while ( ! list_end( &k ) ) {
         struct script* script = list_data( &k );
         if ( script->type == SCRIPTTYPE_OPEN ) {
            start_script( vm, module, script );
         }
         list_next( &k );
      }
      list_next( &i );
   }
}

/* Queues a script for execution and notifies the user that the script started
 * running.
 */
static void start_script( struct vm* vm, struct module* module,
   struct script* script ) {
   struct instance* instance = create_instance( script );
   enq_script( module, instance );
   v_diag( vm, DIAG_DBG, "starting script %s",
      vm_present_script( vm, script ) );
   ++vm->num_active_scripts;
}

static struct instance* create_instance( struct script* script ) {
   struct instance* instance = mem_alloc( sizeof( *instance ) );
   instance->script = script;
   instance->next = NULL;
   instance->next_waiting = NULL;
   instance->waiting = NULL;
   instance->waiting_tail = NULL;
   instance->delay_amount = 0;
   instance->state = SCRIPTSTATE_TERMINATED;
   instance->vars = mem_alloc( sizeof( instance->vars[ 0 ] ) *
      script->num_vars );
   for ( isize i = 0; i < script->num_vars; ++i ) {
      instance->vars[ i ] = 0;
   }
   instance->resume_time = 0;
   instance->ip = script->start;
   instance->arrays = vm_alloc_local_array_space( script->total_array_size );
   return instance;
}

i32* vm_alloc_local_array_space( isize count ) {
   i32* elements = mem_alloc( sizeof( elements[ 0 ] ) * count );
   for ( isize i = 0; i < count; ++i ) {
      elements[ i ] = 0;
   }
   return elements;
}

/**
 * Tells whether there are any scripts waiting to run.
 */
static bool scripts_waiting( struct vm* vm ) {
   struct list_iter i;
   list_iterate( &vm->modules, &i );
   while ( ! list_end( &i ) ) {
      struct module* module = list_data( &i );
      if ( list_size( &module->waiting_scripts ) > 0 ) {
         return true;
      }
      list_next( &i );
   }
   return false;
}

/**
 * Moves the virtual machine into the next tic.
 */
static void next_tic( struct vm* vm ) {
   if ( vm->num_active_scripts > 0 ) {
      usleep( 1000000 );
      ++vm->tics;
   }
/*
   now = timer();
   sleep( next_iterval - now );
   ++tics;
*/
}

static void run_module( struct vm* vm, struct module* module ) {
   while ( script_ready( vm, module ) ) {
      struct instance* script = deq_script( module );
      run_delayed_script( vm, module, script );
   }
}

static bool script_ready( struct vm* vm, struct module* module ) {
   struct instance* instance = list_head( &module->waiting_scripts );
   return ( instance && instance->resume_time <= vm->tics );
}

static void run_delayed_script( struct vm* machine, struct module* module,
   struct instance* script ) {
   struct turn turn;
   init_turn( &turn, module, script );
   run_script( machine, &turn );
   switch ( script->state ) {
   case SCRIPTSTATE_WAITING:
      break;
   case SCRIPTSTATE_TERMINATED:
      {
/*
         //printf( "script %d terminated\n", script->number );
         struct script* waiting_script = script->waiting;
         while ( waiting_script ) {
            enq_script( machine, waiting_script );
            waiting_script = waiting_script->next_waiting;
         }
*/
         // free_script( machine, script );
         v_diag( machine, DIAG_DBG,
            "script %s finished running",
            vm_present_script( machine, script->script ) );
         --machine->num_active_scripts;
      }
      break;
   case SCRIPTSTATE_SUSPENDED:
      add_suspended_script( machine, script );
      break;
   case SCRIPTSTATE_DELAYED:
printf( "delayed until %ld\n", script->resume_time );
      enq_script( module, script );
      break;
   case SCRIPTSTATE_RUNNING:
      // A script should not still be running. This means the maximum tic limit
      // has been reached by the script. Terminate it.
      break;
   default:
      UNREACHABLE();
   }
}

/**
 * Removes the top-most script from the script list.
 */
struct instance* deq_script( struct module* module ) {
   return list_shift( &module->waiting_scripts );
}

/**
 * Inserts a script into the script priority queue. The scripts in the queue
 * are sorted based on how soon they need to run: a script that needs to run
 * sooner will appear closer to the front of the queue.
 */
static void enq_script( struct module* module, struct instance* script ) {
   struct list_iter i;
   list_iterate( &module->waiting_scripts, &i );
   while ( ! list_end( &i ) && run_sooner( list_data( &i ), script ) ) {
      list_next( &i );
   }
   list_insert_before( &module->waiting_scripts, &i, script );
}

/**
 * Tells whether script A should run sooner than script B.
 */
static bool run_sooner( struct instance* a, struct instance* b ) {
   return ( a->delay_amount <= b->delay_amount );
}

struct instance* vm_get_active_script( struct vm* vm, int number ) {
   struct list_iter i;
   list_iterate( &vm->waiting_scripts, &i );
   while ( ! list_end( &i ) ) {
      struct instance* script = list_data( &i );
      if ( script->script->number == number ) {
         return script;
      }
      list_next( &i );
   }
   return NULL;
}

static void add_suspended_script( struct vm* vm, struct instance* script ) {
   list_append( &vm->suspended_scripts, script );
}

struct script* vm_remove_suspended_script( struct vm* vm, i32 script_number ) {
   struct list_iter i;
   list_iterate( &vm->suspended_scripts, &i );
   while ( ! list_end( &i ) ) {
      struct script* script = list_data( &i );
      if ( script->number == script_number ) {
         list_remove( &vm->suspended_scripts, &i );
         return script;
      }
      list_next( &i );
   }
   return NULL;
}

static void init_turn( struct turn* turn, struct module* module,
   struct instance* script ) {
   turn->module = module;
   turn->script = script;
   turn->opcode = PCD_NOP;
   turn->ip = NULL;
   turn->finished = false;
   turn->stack = NULL;
}

static void run_script( struct vm* vm, struct turn* turn ) {
   int stack_buffer[ 1000 ];
   int* stack = stack_buffer;
   int* stack_end = stack + 1000;
   turn->ip = turn->module->object.data + turn->script->ip;
   turn->stack_start = stack;
   turn->stack = turn->stack_start;
   turn->script->state = SCRIPTSTATE_RUNNING;
   while ( ! script_finished( turn ) ) {
      vm_run_instruction( vm, turn );
   }
}

static bool script_finished( struct turn* turn ) {
   switch ( turn->script->state ) {
   case SCRIPTSTATE_SUSPENDED:
   case SCRIPTSTATE_TERMINATED:
   case SCRIPTSTATE_DELAYED:
      return true;
   default:
      return false;
   }
}

void v_diag( struct vm* machine, int flags, ... ) {
   if ( ( flags & DIAG_DBG ) != 0 && ! machine->options->verbose ) {
      return;
   }

   va_list args;
   va_start( args, flags );
   if ( ( flags & DIAG_DBG ) != 0 ) {
      printf( "[dbg] " );
   }
   if ( flags & DIAG_INTERNAL ) {
      printf( "internal " );
   }
   if ( flags & DIAG_ERR ) {
      printf( "error: " );
   }
   else if ( flags & DIAG_FATALERR ) {
      printf( "fatal error: " );
   }
   else if ( flags & DIAG_WARN ) {
      printf( "warning: " );
   }
   const char* format = va_arg( args, const char* );
   vprintf( format, args );
   if ( ( flags & DIAG_MULTI_PART ) == 0 ) {
      printf( "\n" );
   }
   va_end( args );
}

void v_diag_more( struct vm* machine, ... ) {
   va_list args;
   va_start( args, machine );
   const char* format = va_arg( args, const char* );
   vprintf( format, args );
   va_end( args );
}

void v_bail( struct vm* machine ) {
   longjmp( *machine->bail, 1 );
}

i32* vm_get_map_var( struct vm* vm, struct module* module, i32 index ) {
   if ( index >= 0 && index < ARRAY_SIZE( module->vars ) ) {
      return &module->vars[ index ].value;
   }
   else {
      v_diag( vm, DIAG_FATALERR,
         "attempting to access an invalid map variable" );
      v_bail( vm );
      return NULL;
   }
}

struct script* vm_find_script_by_number( struct vm* vm, i32 number ) {
   struct list_iter i;
   list_iterate( &vm->scripts, &i );
   while ( ! list_end( &i ) ) {
      struct script* script = list_data( &i );
      if ( script->number == number ) {
         return script;
      }
      list_next( &i );
   }
   return NULL;
}

const char* vm_present_script( struct vm* vm, struct script* script ) {
   str_clear( &vm->temp_str );
   if ( script->name != null ) {
      str_append( &vm->temp_str, "\"" );
      str_append( &vm->temp_str, script->name );
      str_append( &vm->temp_str, "\"" );
   }
   else {
      str_append_format( &vm->temp_str, "%d", script->number );
   }
   return vm->temp_str.value;
}

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

static void init_vm( struct vm* vm );
static void run( struct vm* vm );
static void run_delayed_script( struct vm* machine );
static struct script* deq_script( struct vm* machine );
static void enq_script( struct vm* machine, struct script* script );
static struct script* get_active_script( struct vm* machine, int number );
static void add_suspended_script( struct vm* vm, struct script* script );
static struct script* remove_suspended_script( struct vm* vm,
   i32 script_number );
static void run_script( struct vm* vm, struct script* script );
static void add_waiting_script( struct script* script,
   struct script* waiting_script );
static void execute_line_special( struct vm* vm, i32 special, i32 arg1,
   i32 arg2 );
static void execute_acs_execute( struct vm* vm, i32 map, i32 script_number );

void vm_run( const u8* data, size_t size ) {
   struct vm vm;
   init_vm( &vm );
   struct object object;
   vm_init_object( &object, data, size );
   vm.object = &object;
   switch ( object.format ) {
   case FORMAT_ZERO:
   case FORMAT_BIG_E:
   case FORMAT_LITTLE_E:
      break;
   default:
      printf( "error: unsupported format\n" );
      return;
   }
   vm_list_chunks( &vm, &object );
   jmp_buf bail;
   if ( setjmp( bail ) == 0 ) {
      vm.bail = &bail;
      run( &vm );
   }
}

void init_vm( struct vm* vm ) {
   vm->object = NULL;
   vm->script_head = NULL;
   list_init( &vm->scripts );
   list_init( &vm->suspended_scripts );
   list_init( &vm->strings );
   vm->arrays = NULL;
   str_init( &vm->msg );
   memset( vm->vars, 0, sizeof( vm->vars ) );
   srand( time( NULL ) );
}

void run( struct vm* machine  ) {
   struct list_iter i;
   list_iterate( &machine->scripts, &i );
   while ( ! list_end( &i ) ) {
      struct script* script = list_data( &i );
      enq_script( machine, script );
      list_next( &i );
   }

/*
   struct script* script = machine->script_head;
   while ( machine->script_head ) {
      struct script* script = deq_script( machine );
      printf( "%d %d\n", script->number, script->delay_amount );
   }
*/


   while ( machine->script_head ) {
      run_delayed_script( machine );
   }
}

void run_delayed_script( struct vm* machine ) {
   struct script* script = deq_script( machine );
   // Wait for the earliest script to run.
   if ( script->delay_amount > 0 ) {
      int delay_amount = script->delay_amount;
      usleep( delay_amount * 1000 );
      struct script* active_script = script;
      while ( active_script ) {
         active_script->delay_amount -= delay_amount;
         active_script = active_script->next;
      }
   }
   run_script( machine, script );
   switch ( script->state ) {
   case SCRIPTSTATE_WAITING:
      break;
   case SCRIPTSTATE_TERMINATED:
      {
         //printf( "script %d terminated\n", script->number );
         struct script* waiting_script = script->waiting;
         while ( waiting_script ) {
            enq_script( machine, waiting_script );
            waiting_script = waiting_script->next_waiting;
         }
         // free_script( machine, script );
      }
      break;
   case SCRIPTSTATE_SUSPENDED:
      add_suspended_script( machine, script );
      break;
   case SCRIPTSTATE_DELAYED:
      enq_script( machine, script );
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
struct script* deq_script( struct vm* machine ) {
   struct script* script = machine->script_head;
   machine->script_head = script->next;
   script->next = NULL;
   return script;
}

/**
 * Inserts a script into the script priority queue. The scripts in the queue
 * are sorted based on how soon they need to run: a script that needs to run
 * the soonest will appear in the front of the queue.
 */
void enq_script( struct vm* machine, struct script* script ) {
   struct script* prev_script = NULL;
   struct script* curr_script = machine->script_head;
   while ( curr_script && curr_script->delay_amount <= script->delay_amount ) {
      prev_script = curr_script;
      curr_script = curr_script->next;
   }
   if ( prev_script ) {
      script->next = prev_script->next;
      prev_script->next = script;
   }
   else {
      script->next = machine->script_head;
      machine->script_head = script;
   }
}

struct script* get_active_script( struct vm* machine, int number ) {
   struct script* script = machine->script_head;
   while ( script ) {
      if ( script->number == number ) {
         return script;
      }
      struct script* waiting_script = script->waiting;
      while ( waiting_script ) {
         if ( waiting_script->number == number ) {
            return waiting_script;
         }
         waiting_script = waiting_script->next;
      }
      script = script->next;
   }
   return NULL;
}

static void add_suspended_script( struct vm* vm, struct script* script ) {
   list_append( &vm->suspended_scripts, script );
}

static struct script* remove_suspended_script( struct vm* vm,
   i32 script_number ) {
   //list_iter_t i;
   
   
   return NULL;
}

void run_script( struct vm* vm, struct script* script ) {
   int stack_buffer[ 1000 ];
   int* stack = stack_buffer;
   int* stack_end = stack + 1000;
   const u8* data = vm->object->data + script->offset;
   int opc;
   script->state = SCRIPTSTATE_RUNNING;
   execute_pcode:
   // Decode opcode.
   opc = PCD_NOP;
   if ( vm->object->small_code ) {
      unsigned char ch = 0;
      memcpy( &ch, data, 1 );
      opc = ( int ) ch;
      ++data;
      if ( opc >= 240 ) {
         memcpy( &ch, data, 1 );
         opc += ( int ) ch;
         ++data;
      }
   }
   else {
   }
   // Execute instruction.
   switch ( opc ) {
   case PCD_NOP:
      // Do nothing.
      break;
   case PCD_TERMINATE:
      script->state = SCRIPTSTATE_TERMINATED;
      return;
   case PCD_SUSPEND:
      script->state = SCRIPTSTATE_SUSPENDED;
      return;
   case PCD_PUSHNUMBER:
      memcpy( stack, data, sizeof( *stack ) );
      ++stack;
      data += sizeof( *stack );
      break;
   case PCD_ADD:
      stack[ -2 ] =
         stack[ -2 ] +
         stack[ -1 ];
      --stack;
      break;
   case PCD_SUBTRACT:
      stack[ -2 ] =
         stack[ -2 ] -
         stack[ -1 ];
      --stack;
      break;
   case PCD_MULTIPLY:
      stack[ -2 ] =
         stack[ -2 ] *
         stack[ -1 ];
      --stack;
      break;
   case PCD_DIVIDE:
      if ( stack[ -1 ] == 0 ) {
         divzero_err:
         v_diag( vm, DIAG_ERR,
            "division by zero in script %d", script->number );
         v_bail( vm );
      }
      stack[ -2 ] =
         stack[ -2 ] /
         stack[ -1 ];
      --stack;
      break;
   case PCD_MODULUS:
      if ( stack[ -1 ] == 0 ) {
         modzero_err:
         v_diag( vm, DIAG_ERR,
            "modulo by zero in script %d", script->number );
         v_bail( vm );
      }
      stack[ -2 ] =
         stack[ -2 ] %
         stack[ -1 ];
      --stack;
      break;
   case PCD_EQ:
      stack[ -2 ] =
         stack[ -2 ] ==
         stack[ -1 ];
      --stack;
      break;
   case PCD_NE:
      stack[ -2 ] =
         stack[ -2 ] !=
         stack[ -1 ];
      --stack;
      break;
   case PCD_LT:
      stack[ -2 ] =
         stack[ -2 ] <
         stack[ -1 ];
      --stack;
      break;
   case PCD_GT:
      stack[ -2 ] =
         stack[ -2 ] >
         stack[ -1 ];
      --stack;
      break;
   case PCD_LE:
      stack[ -2 ] =
         stack[ -2 ] <=
         stack[ -1 ];
      --stack;
      break;
   case PCD_GE:
      stack[ -2 ] =
         stack[ -2 ] >=
         stack[ -1 ];
      --stack;
      break;
   case PCD_ASSIGNSCRIPTVAR:
      script->vars[ ( int ) *data ] = stack[ -1 ];
      --stack;
      ++data;
      break;
   case PCD_ASSIGNMAPVAR:
      vm->vars[ ( int ) *data ] = stack[ -1 ];
      --stack;
      ++data;
      break;
   case PCD_PUSHSCRIPTVAR: {
      int index = *data;
      *stack = script->vars[ index ];
      ++stack;
      ++data;
      break;
   }
   case PCD_PUSHMAPVAR:
      *stack = vm->vars[ ( int ) *data ];
      ++stack;
      ++data;
      break;
   case PCD_ADDSCRIPTVAR:
      script->vars[ ( int ) *data ] += stack[ -1 ];
      --stack;
      ++data;
      break;
   case PCD_ADDMAPVAR:
      vm->vars[ ( int ) *data ] += stack[ -1 ];
      --stack;
      ++data;
      break;
   case PCD_SUBSCRIPTVAR:
      script->vars[ ( int ) *data ] -= stack[ -1 ];
      --stack;
      ++data;
      break;
   case PCD_SUBMAPVAR:
      vm->vars[ ( int ) *data ] -= stack[ -1 ];
      --stack;
      ++data;
      break;
   case PCD_MULSCRIPTVAR:
      script->vars[ ( int ) *data ] *= stack[ -1 ];
      --stack;
      ++data;
      break;
   case PCD_MULMAPVAR:
      vm->vars[ ( int ) *data ] *= stack[ -1 ];
      --stack;
      ++data;
      break;
   case PCD_DIVSCRIPTVAR:
      if ( stack[ -1 ] == 0 ) {
         goto divzero_err;
      }
      script->vars[ ( int ) *data ] /= stack[ -1 ];
      --stack;
      ++data;
      break;
   case PCD_DIVMAPVAR:
      if ( stack[ -1 ] == 0 ) {
         goto divzero_err;
      }
      vm->vars[ ( int ) *data ] /= stack[ -1 ];
      --stack;
      ++data;
      break;
   case PCD_MODSCRIPTVAR:
      if ( stack[ -1 ] == 0 ) {
         goto modzero_err;
      }
      script->vars[ ( int ) *data ] %= stack[ -1 ];
      --stack;
      ++data;
      break;
   case PCD_MODMAPVAR:
      if ( stack[ -1 ] == 0 ) {
         goto modzero_err;
      }
      vm->vars[ ( int ) *data ] %= stack[ -1 ];
      --stack;
      ++data;
      break;
   case PCD_INCSCRIPTVAR:
      ++script->vars[ ( int ) *data ];
      ++data;
      break;
   case PCD_INCMAPVAR:
      ++vm->vars[ ( int ) *data ];
      ++data;
      break;
   case PCD_DECSCRIPTVAR:
      --script->vars[ ( int ) *data ];
      ++data;
      break;
   case PCD_DECMAPVAR:
      --vm->vars[ ( int ) *data ];
      ++data;
      break;
   case PCD_GOTO:
      {
         int offset;
         memcpy( &offset, data, sizeof( offset ) );
         data = vm->object->data + offset;
      }
      break;
   case PCD_IFGOTO:
      {
         int offset;
         memcpy( &offset, data, sizeof( offset ) );
         --stack;
         if ( *stack ) {
            data = vm->object->data + offset;
         }
         else {
            data += sizeof( offset );
         }
      }
      break;
   case PCD_DROP:
      --stack;
      break;
   case PCD_DELAY:
      --stack;
      if ( *stack > 0 ) {
         script->delay_amount = *stack;
         script->state = SCRIPTSTATE_DELAYED;
         script->offset = data - vm->object->data;
         return;
      }
      break;
   case PCD_DELAYDIRECT:
      {
         int delay_amount;
         memcpy( &delay_amount, data, sizeof( delay_amount ) );
         data += sizeof( delay_amount );
         if ( delay_amount > 0 ) {
            script->delay_amount = delay_amount;
            script->state = SCRIPTSTATE_DELAYED;
            script->offset = data - vm->object->data;
            return;
         }
      }
      break;
   case PCD_RANDOM:
      stack[ -2 ] = stack[ -2 ] + rand() % ( stack[ -1 ] - stack[ -2 ] + 1 );
      --stack;
      break;
   case PCD_RANDOMDIRECT:
      {
         struct {
            int min;
            int max;
         } args;
         memcpy( &args, data, sizeof( args ) );
         data += sizeof( args );
         *stack = args.min + rand() % ( args.max - args.min + 1 );
         ++stack;
      }
      break;
   case PCD_RESTART:
      data = vm->object->data + script->offset;
      break;
   case PCD_ANDLOGICAL:
      stack[ -2 ] =
         stack[ -2 ] &&
         stack[ -1 ];
      --stack;
      break;
   case PCD_ORLOGICAL:
      stack[ -2 ] =
         stack[ -2 ] ||
         stack[ -1 ];
      --stack;
      break;
   case PCD_ANDBITWISE:
      stack[ -2 ] =
         stack[ -2 ] &
         stack[ -1 ];
      --stack;
      break;
   case PCD_ORBITWISE:
      stack[ -2 ] =
         stack[ -2 ] |
         stack[ -1 ];
      --stack;
      break;
   case PCD_EORBITWISE:
      stack[ -2 ] =
         stack[ -2 ] ^
         stack[ -1 ];
      --stack;
      break;
   case PCD_NEGATELOGICAL:
      stack[ -1 ] = ( ! stack[ -1 ] );
      break;
   case PCD_LSHIFT:
      stack[ -2 ] =
         stack[ -2 ] <<
         stack[ -1 ];
      --stack;
      break;
   case PCD_RSHIFT:
      stack[ -2 ] =
         stack[ -2 ] >>
         stack[ -1 ];
      --stack;
      break;
   case PCD_UNARYMINUS:
      stack[ -1 ] = ( - stack[ -1 ] );
      break;
   case PCD_IFNOTGOTO:
      {
         int offset;
         memcpy( &offset, data, sizeof( offset ) );
         --stack;
         if ( ! *stack ) {
            data = vm->object->data + offset;
         }
         else {
            data += sizeof( offset );
         }
      }
      break;
   case PCD_SCRIPTWAIT:
   case PCD_SCRIPTWAITDIRECT:
      {
         int number;
         if ( opc == PCD_SCRIPTWAITDIRECT ) {
            memcpy( &number, data, sizeof( number ) );
            data += sizeof( number );
         }
         else {
            number = stack[ -1 ];
            --stack;
         }
         struct script* target_script = get_active_script( vm, number );
         if ( target_script ) {
            add_waiting_script( target_script, script );
            script->state = SCRIPTSTATE_WAITING;
            script->offset = data - vm->object->data;
            return;
         }
      }
      break;
   case PCD_CASEGOTO:
      {
         struct {
            int value;
            int offset;
         } args;
         memcpy( &args, data, sizeof( args ) );
         if ( stack[ -1 ] == args.value ) {
            data = vm->object->data + args.offset;
         }
         else {
            data += sizeof( args );
         }
         --stack;
      }
      break;
   case PCD_BEGINPRINT:
      str_clear( &vm->msg );
      break;
   case PCD_ENDPRINT:
      printf( "%s", vm->msg.value );
      break;
   case PCD_PRINTSTRING:
      {
         struct list_iter i;
         list_iterate( &vm->strings, &i );
         int index = 0;
         while ( ! list_end( &i ) ) {
            if ( index == stack[ -1 ] ) {
               struct str* string = list_data( &i );
               str_append( &vm->msg, string->value );
               break;
            }
            ++index;
            list_next( &i );
         }
         --stack;
      }
      break;
   case PCD_PRINTNUMBER:
      {
         char text[ 11 ];
         sprintf( text, "%d", stack[ -1 ] );
         str_append( &vm->msg, text );
         --stack;
      }
      break;
   case PCD_PRINTCHARACTER:
      {
         char text[] = { ( char ) stack[ -1 ], '\0' };
         str_append( &vm->msg, text );
         --stack;
      }
      break;
   case PCD_PUSHBYTE:
      *stack = *data;
      ++stack;
      ++data;
      break;
   case PCD_PUSHMAPARRAY: {
      int index = stack[ -1 ];
      if ( index >= 0 && index < vm->arrays[ ( int ) *data ].size ) {
         stack[ -1 ] = vm->arrays[ ( int ) *data ].elements[ stack[ -1 ] ];
      }
      else {
         stack[ -1 ] = 0;
      }
      ++data;
      break;
   }
   case PCD_LSPEC1DIRECTB:
      execute_line_special( vm, data[ 0 ], data[ 1 ], 0 );
      data += sizeof( data[ 0 ] ) * 2; // Increment past the two arguments.
      break;
   case PCD_LSPEC1:
   case PCD_LSPEC2:
   case PCD_LSPEC3:
   case PCD_LSPEC4:
   case PCD_LSPEC5:
   case PCD_LSPEC1DIRECT:
   case PCD_LSPEC2DIRECT:
   case PCD_LSPEC3DIRECT:
   case PCD_LSPEC4DIRECT:
   case PCD_LSPEC5DIRECT:
   case PCD_THINGCOUNT:
   case PCD_THINGCOUNTDIRECT:
   case PCD_TAGWAIT:
   case PCD_TAGWAITDIRECT:
   case PCD_POLYWAIT:
   case PCD_POLYWAITDIRECT:
   case PCD_CHANGEFLOOR:
   case PCD_CHANGEFLOORDIRECT:
   case PCD_CHANGECEILING:
   case PCD_CHANGECEILINGDIRECT:
   case PCD_LINESIDE:
   case PCD_CLEARLINESPECIAL:
   case PCD_PLAYERCOUNT:
   case PCD_GAMETYPE:
   case PCD_GAMESKILL:
   case PCD_TIMER:
   case PCD_SECTORSOUND:
   case PCD_AMBIENTSOUND:
   case PCD_SOUNDSEQUENCE:
   case PCD_SETLINETEXTURE:
   case PCD_SETLINEBLOCKING:
   case PCD_SETLINESPECIAL:
   case PCD_THINGSOUND:
   case PCD_ENDPRINTBOLD:
   case PCD_ACTIVATORSOUND:
   case PCD_LOCALAMBIENTSOUND:
   case PCD_SETLINEMONSTERBLOCKING:
   case PCD_PLAYERBLUESKULL:
   case PCD_PLAYERREDSKULL:
   case PCD_PLAYERYELLOWSKULL:
   case PCD_PLAYERMASTERSKULL:
   case PCD_PLAYERBLUECARD:
   case PCD_PLAYERREDCARD:
   case PCD_PLAYERYELLOWCARD:
   case PCD_PLAYERMASTERCARD:
   case PCD_PLAYERBLACKSKULL:
   case PCD_PLAYERSILVERSKULL:
   case PCD_PLAYERGOLDSKULL:
   case PCD_PLAYERBLACKCARD:
   case PCD_PLAYERSILVERCARD:
   case PCD_ISMULTIPLAYER:
   case PCD_PLAYERTEAM:
   case PCD_PLAYERHEALTH:
   case PCD_PLAYERARMORPOINTS:
   case PCD_PLAYERFRAGS:
   case PCD_PLAYEREXPERT:
   case PCD_BLUETEAMCOUNT:
   case PCD_REDTEAMCOUNT:
   case PCD_BLUETEAMSCORE:
   case PCD_REDTEAMSCORE:
   case PCD_ISONEFLAGCTF:
   case PCD_GETINVASIONWAVE:
   case PCD_GETINVASIONSTATE:
   case PCD_PRINTNAME:
   case PCD_MUSICCHANGE:
   case PCD_CONSOLECOMMANDDIRECT:
   case PCD_CONSOLECOMMAND:
   case PCD_SINGLEPLAYER:
   case PCD_FIXEDMUL:
   case PCD_FIXEDDIV:
   case PCD_SETGRAVITY:
   case PCD_SETGRAVITYDIRECT:
   case PCD_SETAIRCONTROL:
   case PCD_SETAIRCONTROLDIRECT:
   case PCD_CLEARINVENTORY:
   case PCD_GIVEINVENTORY:
   case PCD_GIVEINVENTORYDIRECT:
   case PCD_TAKEINVENTORY:
   case PCD_TAKEINVENTORYDIRECT:
   case PCD_CHECKINVENTORY:
   case PCD_CHECKINVENTORYDIRECT:
   case PCD_SPAWN:
   case PCD_SPAWNDIRECT:
   case PCD_SPAWNSPOT:
   case PCD_SPAWNSPOTDIRECT:
   case PCD_SETMUSIC:
   case PCD_SETMUSICDIRECT:
   case PCD_LOCALSETMUSIC:
   case PCD_LOCALSETMUSICDIRECT:
      printf( "error: instruction not supported\n" );
      v_bail( vm );
   case PCD_ASSIGNWORLDVAR:
   case PCD_PUSHWORLDVAR:
   case PCD_ADDWORLDVAR:
   case PCD_SUBWORLDVAR:
   case PCD_MULWORLDVAR:
   case PCD_DIVWORLDVAR:
   case PCD_MODWORLDVAR:
   case PCD_INCWORLDVAR:
   case PCD_DECWORLDVAR:
      v_diag( vm, DIAG_FATALERR,
         "world variables not supported" );
      v_bail( vm );
   default:
      v_diag( vm, DIAG_FATALERR,
         "unknown instruction (opcode is %d)", opc );
      v_bail( vm );
   }
   // Keep executing instructions.
   goto execute_pcode;
}

void add_waiting_script( struct script* script,
   struct script* waiting_script ) {
   if ( script->waiting ) {
      script->waiting_tail->next_waiting = waiting_script;
   }
   else {
      script->waiting = waiting_script;
   }
   script->waiting_tail = waiting_script;
}

static void execute_line_special( struct vm* vm, i32 special, i32 arg1,
   i32 arg2 ) {
   switch ( special ) {
   case LSPEC_ACSEXECUTE:
      execute_acs_execute( vm, arg1, arg2 );
      break;
   default:
      UNREACHABLE();
   }
}

static void execute_acs_execute( struct vm* vm, i32 map, i32 script_number ) {
   // Resume a suspended script.
   struct script* script = remove_suspended_script( vm, script_number );
   if ( script != NULL ) {
      
   }
}

void v_diag( struct vm* machine, int flags, ... ) {
   va_list args;
   va_start( args, flags );
   if ( flags & DIAG_ERR ) {
      printf( "error: " );
   }
   if ( flags & DIAG_FATALERR ) {
      printf( "fatal error: " );
   }
   else if ( flags & DIAG_WARN ) {
      printf( "warning: " );
   }
   const char* format = va_arg( args, const char* );
   vprintf( format, args );
   printf( "\n" );
   va_end( args );
}

void v_bail( struct vm* machine ) {
   longjmp( *machine->bail, 1 );
}

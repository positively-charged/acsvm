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

struct turn {
   struct script* script;
   i32 opcode;
   const u8* ip; // Instruction pointer.
   bool finished;
   i32* stack;
};

static void init_vm( struct vm* vm );
static void run( struct vm* vm );
static bool scripts_waiting( struct vm* vm );
static void run_delayed_script( struct vm* machine );
static struct script* deq_script( struct vm* vm );
static void enq_script( struct vm* vm, struct script* script );
static bool run_sooner( struct script* a, struct script* b );
static struct script* get_active_script( struct vm* vm, int number );
static void add_suspended_script( struct vm* vm, struct script* script );
static struct script* remove_suspended_script( struct vm* vm,
   i32 script_number );
static void init_turn( struct turn* turn, struct script* script );
static void run_script( struct vm* vm, struct turn* turn );
static void run_instruction( struct vm* vm, struct turn* turn );
static void decode_opcode( struct vm* vm, struct turn* turn );
static void push( struct turn* turn, i32 value );
static i32 pop( struct turn* turn );
static void add_waiting_script( struct script* script,
   struct script* waiting_script );
static void execute_line_special( struct vm* vm, i32 special, i32 arg1,
   i32 arg2, i32 arg3 );
static void execute_acs_execute( struct vm* vm, i32 script_number, i32 map,
   i32 arg1 );

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
   list_init( &vm->scripts );
   list_init( &vm->waiting_scripts );
   list_init( &vm->suspended_scripts );
   list_init( &vm->strings );
   vm->arrays = NULL;
   str_init( &vm->msg );
   memset( vm->vars, 0, sizeof( vm->vars ) );
   srand( time( NULL ) );
}

void run( struct vm* vm  ) {
   // OPEN scripts run first.
   struct list_iter i;
   list_iterate( &vm->scripts, &i );
   while ( ! list_end( &i ) ) {
      struct script* script = list_data( &i );
      if ( script->type == SCRIPTTYPE_OPEN ) {
         enq_script( vm, script );
      }
      list_next( &i );
   }
   // Run scripts.
   while ( scripts_waiting( vm ) ) {
      run_delayed_script( vm );
   }
}

/**
 * Tells whether there are any scripts waiting to run.
 */
static bool scripts_waiting( struct vm* vm ) {
   return ( list_size( &vm->waiting_scripts ) );
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
   struct turn turn;
   init_turn( &turn, script );
   run_script( machine, &turn );
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
struct script* deq_script( struct vm* vm ) {
   return list_shift( &vm->waiting_scripts );
}

/**
 * Inserts a script into the script priority queue. The scripts in the queue
 * are sorted based on how soon they need to run: a script that needs to run
 * sooner will appear closer to the front of the queue.
 */
static void enq_script( struct vm* vm, struct script* script ) {
   struct list_iter i;
   list_iterate( &vm->waiting_scripts, &i );
   while ( ! list_end( &i ) && run_sooner( list_data( &i ), script ) ) {
      list_next( &i );
   }
   list_insert_before( &vm->waiting_scripts, &i, script );
}

/**
 * Tells whether script A should run sooner than script B.
 */
static bool run_sooner( struct script* a, struct script* b ) {
   return ( a->delay_amount <= b->delay_amount );
}

struct script* get_active_script( struct vm* vm, int number ) {
   struct list_iter i;
   list_iterate( &vm->waiting_scripts, &i );
   while ( ! list_end( &i ) ) {
      struct script* script = list_data( &i );
      if ( script->number == number ) {
         return script;
      }
      list_next( &i );
   }
   return NULL;
}

static void add_suspended_script( struct vm* vm, struct script* script ) {
   list_append( &vm->suspended_scripts, script );
}

static struct script* remove_suspended_script( struct vm* vm,
   i32 script_number ) {
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

static void init_turn( struct turn* turn, struct script* script ) {
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
   turn->ip = vm->object->data + turn->script->offset;
   turn->stack = stack;
   turn->script->state = SCRIPTSTATE_RUNNING;
   while ( ! turn->finished ) {
      run_instruction( vm, turn );
   }
}

/**
 * Executes a single instruction of a script.
 */
static void run_instruction( struct vm* vm, struct turn* turn ) {
   decode_opcode( vm, turn );
   switch ( turn->opcode ) {
   case PCD_NOP:
      // Do nothing.
      break;
   case PCD_TERMINATE:
      turn->script->state = SCRIPTSTATE_TERMINATED;
      turn->finished = true;
      break;
   case PCD_SUSPEND:
      turn->script->state = SCRIPTSTATE_SUSPENDED;
      turn->script->offset = turn->ip - vm->object->data;
      turn->finished = true;
      break;
   case PCD_PUSHNUMBER:
      {
         i32 value;
         memcpy( &value, turn->ip, sizeof( value ) );
         push( turn, value );
         turn->ip += sizeof( *turn->stack );
      }
      break;
   case PCD_ADD:
      {
         i32 r = pop( turn );
         i32 l = pop( turn );
         push( turn, l + r );
      }
      break;
   case PCD_SUBTRACT:
      {
         i32 r = pop( turn );
         i32 l = pop( turn );
         push( turn, l - r );
      }
      break;
   case PCD_MULTIPLY:
      {
         i32 r = pop( turn );
         i32 l = pop( turn );
         push( turn, l * r );
      }
      break;
   case PCD_DIVIDE:
      {
         i32 r = pop( turn );
         i32 l = pop( turn );
         if ( r == 0 ) {
            divzero_err:
            v_diag( vm, DIAG_ERR,
               "division by zero in script %d", turn->script->number );
            v_bail( vm );
         }
         push( turn, l / r );
      }
      break;
   case PCD_MODULUS:
      {
         i32 r = pop( turn );
         i32 l = pop( turn );
         if ( r == 0 ) {
            modzero_err:
            v_diag( vm, DIAG_ERR,
               "modulo by zero in script %d", turn->script->number );
            v_bail( vm );
         }
         push( turn, l % r );
      }
      break;
   case PCD_EQ:
      {
         i32 r = pop( turn );
         i32 l = pop( turn );
         push( turn, l == r );
      }
      break;
   case PCD_NE:
      {
         i32 r = pop( turn );
         i32 l = pop( turn );
         push( turn, l != r );
      }
      break;
   case PCD_LT:
      {
         i32 r = pop( turn );
         i32 l = pop( turn );
         push( turn, l < r );
      }
      break;
   case PCD_GT:
      {
         i32 r = pop( turn );
         i32 l = pop( turn );
         push( turn, l > r );
      }
      break;
   case PCD_LE:
      {
         i32 r = pop( turn );
         i32 l = pop( turn );
         push( turn, l <= r );
      }
      break;
   case PCD_GE:
      {
         i32 r = pop( turn );
         i32 l = pop( turn );
         push( turn, l >= r );
      }
      break;
   case PCD_ASSIGNSCRIPTVAR:
      turn->script->vars[ ( int ) *turn->ip ] = pop( turn );
      ++turn->ip;
      break;
   case PCD_ASSIGNMAPVAR:
      vm->vars[ ( int ) *turn->ip ] = pop( turn );
      ++turn->ip;
      break;
   case PCD_PUSHSCRIPTVAR: {
      int index = *turn->ip;
      push( turn, turn->script->vars[ index ] );
      ++turn->ip;
      break;
   }
   case PCD_PUSHMAPVAR:
      push( turn, vm->vars[ ( int ) *turn->ip ] );
      ++turn->ip;
      break;
   case PCD_ADDSCRIPTVAR:
      turn->script->vars[ ( int ) *turn->ip ] += pop( turn );
      ++turn->ip;
      break;
   case PCD_ADDMAPVAR:
      vm->vars[ ( int ) *turn->ip ] += pop( turn );
      ++turn->ip;
      break;
   case PCD_SUBSCRIPTVAR:
      turn->script->vars[ ( int ) *turn->ip ] -= pop( turn );
      ++turn->ip;
      break;
   case PCD_SUBMAPVAR:
      vm->vars[ ( int ) *turn->ip ] -= pop( turn );
      ++turn->ip;
      break;
   case PCD_MULSCRIPTVAR:
      turn->script->vars[ ( int ) *turn->ip ] *= pop( turn );
      ++turn->ip;
      break;
   case PCD_MULMAPVAR:
      vm->vars[ ( int ) *turn->ip ] *= pop( turn );
      ++turn->ip;
      break;
   case PCD_DIVSCRIPTVAR:
      {
         i32 r = pop( turn );
         if ( r == 0 ) {
            goto divzero_err;
         }
         turn->script->vars[ ( int ) *turn->ip ] /= r;
         ++turn->ip;
      }
      break;
   case PCD_DIVMAPVAR:
      {
         i32 r = pop( turn );
         if ( r == 0 ) {
            goto divzero_err;
         }
         vm->vars[ ( int ) *turn->ip ] /= r;
         ++turn->ip;
      }
      break;
   case PCD_MODSCRIPTVAR:
      {
         i32 r = pop( turn );
         if ( r == 0 ) {
            goto modzero_err;
         }
         turn->script->vars[ ( int ) *turn->ip ] %= r;
         ++turn->ip;
      }
      break;
   case PCD_MODMAPVAR:
      {
         i32 r = pop( turn );
         if ( r == 0 ) {
            goto modzero_err;
         }
         vm->vars[ ( int ) *turn->ip ] %= r;
         ++turn->ip;
      }
      break;
   case PCD_INCSCRIPTVAR:
      ++turn->script->vars[ ( int ) *turn->ip ];
      ++turn->ip;
      break;
   case PCD_INCMAPVAR:
      ++vm->vars[ ( int ) *turn->ip ];
      ++turn->ip;
      break;
   case PCD_DECSCRIPTVAR:
      --turn->script->vars[ ( int ) *turn->ip ];
      ++turn->ip;
      break;
   case PCD_DECMAPVAR:
      --vm->vars[ ( int ) *turn->ip ];
      ++turn->ip;
      break;
   case PCD_GOTO:
      {
         int offset;
         memcpy( &offset, turn->ip, sizeof( offset ) );
         turn->ip = vm->object->data + offset;
      }
      break;
   case PCD_IFGOTO:
      {
         int offset;
         memcpy( &offset, turn->ip, sizeof( offset ) );
         i32 value = pop( turn );
         if ( value != 0 ) {
            turn->ip = vm->object->data + offset;
         }
         else {
            turn->ip += sizeof( offset );
         }
      }
      break;
   case PCD_DROP:
      pop( turn );
      break;
   case PCD_DELAY:
      {
         i32 amount = pop( turn );
         if ( amount > 0 ) {
            turn->script->delay_amount = amount;
            turn->script->state = SCRIPTSTATE_DELAYED;
            turn->script->offset = turn->ip - vm->object->data;
            return;
         }
      }
      break;
   case PCD_DELAYDIRECT:
      {
         int delay_amount;
         memcpy( &delay_amount, turn->ip, sizeof( delay_amount ) );
         turn->ip += sizeof( delay_amount );
         if ( delay_amount > 0 ) {
            turn->script->delay_amount = delay_amount;
            turn->script->state = SCRIPTSTATE_DELAYED;
            turn->script->offset = turn->ip - vm->object->data;
            return;
         }
      }
      break;
   case PCD_RANDOM:
      {
         i32 max = pop( turn );
         i32 min = pop( turn );
         push( turn, min + rand() % ( max - min + 1 ) );
      }
      break;
   case PCD_RANDOMDIRECT:
      {
         struct {
            int min;
            int max;
         } args;
         memcpy( &args, turn->ip, sizeof( args ) );
         turn->ip += sizeof( args );
         push( turn, args.min + rand() % ( args.max - args.min + 1 ) );
      }
      break;
   case PCD_RESTART:
      turn->ip = vm->object->data + turn->script->offset;
      break;
   case PCD_ANDLOGICAL:
      {
         i32 r = pop( turn );
         i32 l = pop( turn );
         push( turn, l && r );
      }
      break;
   case PCD_ORLOGICAL:
      {
         i32 r = pop( turn );
         i32 l = pop( turn );
         push( turn, l || r );
      }
      break;
   case PCD_ANDBITWISE:
      {
         i32 r = pop( turn );
         i32 l = pop( turn );
         push( turn, l & r );
      }
      break;
   case PCD_ORBITWISE:
      {
         i32 r = pop( turn );
         i32 l = pop( turn );
         push( turn, l | r );
      }
      break;
   case PCD_EORBITWISE:
      {
         i32 r = pop( turn );
         i32 l = pop( turn );
         push( turn, l ^ r );
      }
      break;
   case PCD_NEGATELOGICAL:
      push( turn, ! pop( turn ) );
      break;
   case PCD_LSHIFT:
      {
         i32 r = pop( turn );
         i32 l = pop( turn );
         push( turn, l << r );
      }
      break;
   case PCD_RSHIFT:
      {
         i32 r = pop( turn );
         i32 l = pop( turn );
         push( turn, l >> r );
      }
      break;
   case PCD_UNARYMINUS:
      push( turn, - pop( turn ) );
      break;
   case PCD_IFNOTGOTO:
      {
         int offset;
         memcpy( &offset, turn->ip, sizeof( offset ) );
         i32 value = pop( turn );
         if ( value == 0 ) {
            turn->ip = vm->object->data + offset;
         }
         else {
            turn->ip += sizeof( offset );
         }
      }
      break;
   case PCD_SCRIPTWAIT:
   case PCD_SCRIPTWAITDIRECT:
      {
         int number;
         if ( turn->opcode == PCD_SCRIPTWAITDIRECT ) {
            memcpy( &number, turn->ip, sizeof( number ) );
            turn->ip += sizeof( number );
         }
         else {
            number = pop( turn );
         }
         struct script* target_script = get_active_script( vm, number );
         if ( target_script ) {
            add_waiting_script( target_script, turn->script );
            turn->script->state = SCRIPTSTATE_WAITING;
            turn->script->offset = turn->ip - vm->object->data;
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
         memcpy( &args, turn->ip, sizeof( args ) );
         i32 value = pop( turn );
         if ( value == args.value ) {
            turn->ip = vm->object->data + args.offset;
         }
         else {
            turn->ip += sizeof( args );
            push( turn, value );
         }
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
         const i32 requested_string = pop( turn );
         i32 index = 0;
         while ( ! list_end( &i ) ) {
            if ( index == requested_string ) {
               struct str* string = list_data( &i );
               str_append( &vm->msg, string->value );
               break;
            }
            ++index;
            list_next( &i );
         }
      }
      break;
   case PCD_PRINTNUMBER:
      {
         char text[ 11 ];
         sprintf( text, "%d", pop( turn ) );
         str_append( &vm->msg, text );
      }
      break;
   case PCD_PRINTCHARACTER:
      {
         char text[] = { ( char ) pop( turn ), '\0' };
         str_append( &vm->msg, text );
      }
      break;
   case PCD_PUSHBYTE:
      push( turn, *turn->ip );
      ++turn->ip;
      break;
   case PCD_PUSH2BYTES:
      // TODO: Make sure we do not overflow the IP segment. Make a function
      // that reads the IP and protects against overflows.
      push( turn, turn->ip[ 0 ] );
      push( turn, turn->ip[ 1 ] );
      turn->ip +=
         sizeof( turn->ip[ 0 ] ) +
         sizeof( turn->ip[ 1 ] );
      break;
   case PCD_PUSHMAPARRAY: {
      int index = pop( turn );
      if ( index >= 0 && index < vm->arrays[ ( int ) *turn->ip ].size ) {
         push( turn, vm->arrays[ ( int ) *turn->ip ].elements[ pop( turn ) ] );
      }
      else {
         push( turn, 0 );
      }
      ++turn->ip;
      break;
   }
   case PCD_LSPEC1DIRECTB:
      execute_line_special( vm,
         turn->ip[ 0 ],
         turn->ip[ 1 ],
         0, 0 );
      turn->ip += sizeof( turn->ip[ 0 ] ) * 2; // Increment past the two arguments.
      break;
   case PCD_LSPEC2DIRECTB:
      execute_line_special( vm,
         turn->ip[ 0 ],
         turn->ip[ 1 ],
         turn->ip[ 2 ],
         0 );
      turn->ip += sizeof( turn->ip[ 0 ] ) * 3; // Increment past the three arguments.
      break;
   case PCD_LSPEC3DIRECTB:
      execute_line_special( vm,
         turn->ip[ 0 ],
         turn->ip[ 1 ],
         turn->ip[ 2 ],
         turn->ip[ 3 ] );
      turn->ip += sizeof( turn->ip[ 0 ] ) * 4; // Increment past the four arguments.
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
         "unknown instruction (opcode is %d)", turn->opcode );
      v_bail( vm );
   }
}

static void decode_opcode( struct vm* vm, struct turn* turn ) {
   i32 opc = PCD_NOP;
   if ( vm->object->small_code ) {
      unsigned char ch = 0;
      memcpy( &ch, turn->ip, 1 );
      opc = ( int ) ch;
      ++turn->ip;
      if ( opc >= 240 ) {
         memcpy( &ch, turn->ip, 1 );
         opc += ( int ) ch;
         ++turn->ip;
      }
   }
   else {
   }
   turn->opcode = opc;
}

static void push( struct turn* turn, i32 value ) {
   *turn->stack = value;
   ++turn->stack;
}

static i32 pop( struct turn* turn ) {
   --turn->stack;
   return *turn->stack;
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
   i32 arg2, i32 arg3 ) {
   switch ( special ) {
   case LSPEC_ACSEXECUTE:
      execute_acs_execute( vm, arg1, arg2, arg3 );
      break;
   default:
      UNREACHABLE();
   }
}

static void execute_acs_execute( struct vm* vm, i32 script_number, i32 map,
   i32 arg1 ) {
   // Resume a suspended script.
   struct script* script = remove_suspended_script( vm, script_number );
   if ( script != NULL ) {
      enq_script( vm, script );
      return;
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

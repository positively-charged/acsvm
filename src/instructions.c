#include <stdio.h>
#include <string.h>
#include <setjmp.h>

#include "common/misc.h"
#include "common/mem.h"
#include "common/str.h"
#include "common/list.h"
#include "vm.h"
#include "pcode.h"
#include "debug.h"

struct pcode_func {
   const char* name;
   i32 opcode;
   i8 num_args;
   bool returns_value;
};

static void check_div_by_zero( struct vm* vm, struct turn* turn,
   i32 denominator );
static i32* get_script_var( struct vm* vm, struct turn* turn, i32 index );
static i32* get_script_element( struct vm* vm, struct turn* turn,
   i32 array_index, i32 element );
static i32* get_element( struct vm* vm, struct turn* turn, i32* array_data,
   struct script_array* entry, int element );
static void run_updateworldarray( struct vm* vm, struct turn* turn );
static struct vector* get_world_vector( struct vm* vm, i32 index );
static void extend_array_if_out_of_bounds( struct vector* vector, i32 index );
static void run_pushworldarray( struct vm* vm, struct turn* turn );
static void decode_opcode( struct vm* vm, struct turn* turn );
static void push( struct turn* turn, i32 value );
static i32 pop( struct vm* vm, struct turn* turn );
static void add_waiting_script( struct instance* script,
   struct instance* waiting_script );
static void delay_current_script( struct vm* vm, struct turn* turn,
   i32 amount );
static void push_random_number( struct turn* turn, i32 min, i32 max );
static void run_call( struct vm* vm, struct turn* turn );
static struct call* push_call( struct vm* vm );
static void run_return( struct vm* vm, struct turn* turn );
static struct call* pop_call( struct vm* vm );
static void run_pcode_func( struct vm* vm, struct turn* turn );
static enum opcode translate_direct_opcode( enum opcode opcode );
static const struct pcode_func* get_pcode_func( enum opcode opcode );

/**
 * Executes a single instruction of a script.
 */
void vm_run_instruction( struct vm* vm, struct turn* turn ) {
   decode_opcode( vm, turn );
   switch ( turn->opcode ) {
   case PCD_NOP:
      // Does nothing.
      break;
   case PCD_TERMINATE:
      turn->script->state = SCRIPTSTATE_TERMINATED;
      turn->finished = true;
      break;
   case PCD_SUSPEND:
      turn->script->state = SCRIPTSTATE_SUSPENDED;
      turn->script->ip = turn->ip - turn->module->object.data;
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
   case PCD_LSPEC5EX:
	case PCD_LSPEC5EXRESULT:
      vm_run_lspec( vm, turn );
      break;
   case PCD_ADD:
      {
         i32 r = pop( vm, turn );
         i32 l = pop( vm, turn );
         push( turn, l + r );
      }
      break;
   case PCD_SUBTRACT:
      {
         i32 r = pop( vm, turn );
         i32 l = pop( vm, turn );
         push( turn, l - r );
      }
      break;
   case PCD_MULTIPLY:
      {
         i32 r = pop( vm, turn );
         i32 l = pop( vm, turn );
         push( turn, l * r );
      }
      break;
   case PCD_DIVIDE:
      {
         i32 r = pop( vm, turn );
         i32 l = pop( vm, turn );
         if ( r == 0 ) {
            divzero_err:
            v_diag( vm, DIAG_ERR,
               "division by zero in script %d", turn->script->script->number );
            v_bail( vm );
         }
         push( turn, l / r );
      }
      break;
   case PCD_MODULUS:
      {
         i32 r = pop( vm, turn );
         i32 l = pop( vm, turn );
         if ( r == 0 ) {
            modzero_err:
            v_diag( vm, DIAG_ERR,
               "modulo by zero in script %d", turn->script->script->number );
            v_bail( vm );
         }
         push( turn, l % r );
      }
      break;
   case PCD_EQ:
      {
         i32 r = pop( vm, turn );
         i32 l = pop( vm, turn );
         push( turn, l == r );
      }
      break;
   case PCD_NE:
      {
         i32 r = pop( vm, turn );
         i32 l = pop( vm, turn );
         push( turn, l != r );
      }
      break;
   case PCD_LT:
      {
         i32 r = pop( vm, turn );
         i32 l = pop( vm, turn );
         push( turn, l < r );
      }
      break;
   case PCD_GT:
      {
         i32 r = pop( vm, turn );
         i32 l = pop( vm, turn );
         push( turn, l > r );
      }
      break;
   case PCD_LE:
      {
         i32 r = pop( vm, turn );
         i32 l = pop( vm, turn );
         push( turn, l <= r );
      }
      break;
   case PCD_GE:
      {
         i32 r = pop( vm, turn );
         i32 l = pop( vm, turn );
         push( turn, l >= r );
      }
      break;
   case PCD_ASSIGNSCRIPTVAR:
      {
         i32* var = get_script_var( vm, turn, turn->ip[ 0 ] );
         var[ 0 ] = pop( vm, turn );
         ++turn->ip;
      }
      break;
   case PCD_ASSIGNMAPVAR:
      turn->module->map_vars[ ( int ) *turn->ip ]->value = pop( vm, turn );
      ++turn->ip;
      break;
   case PCD_ASSIGNWORLDVAR:
      vm->world_vars[ ( int ) *turn->ip ] = pop( vm, turn );
      ++turn->ip;
      break;
   case PCD_PUSHSCRIPTVAR:
      {
         int index = *turn->ip;
         push( turn, get_script_var( vm, turn, index )[ 0 ] );
         ++turn->ip;
      }
      break;
   case PCD_PUSHMAPVAR:
      push( turn, turn->module->map_vars[ ( int ) *turn->ip ]->value );
      ++turn->ip;
      break;
   case PCD_PUSHWORLDVAR:
      push( turn, vm->world_vars[ ( int ) *turn->ip ] );
      ++turn->ip;
      break;
   case PCD_ADDSCRIPTVAR:
      turn->script->vars[ ( int ) *turn->ip ] += pop( vm, turn );
      ++turn->ip;
      break;
   case PCD_ADDMAPVAR:
      turn->module->map_vars[ ( int ) *turn->ip ]->value += pop( vm, turn );
      ++turn->ip;
      break;
   case PCD_ADDWORLDVAR:
      vm->world_vars[ ( int ) *turn->ip ] += pop( vm, turn );
      ++turn->ip;
      break;
   case PCD_SUBSCRIPTVAR:
      turn->script->vars[ ( int ) *turn->ip ] -= pop( vm, turn );
      ++turn->ip;
      break;
   case PCD_SUBMAPVAR:
      turn->module->map_vars[ ( int ) *turn->ip ]->value -= pop( vm, turn );
      ++turn->ip;
      break;
   case PCD_SUBWORLDVAR:
      vm->world_vars[ ( int ) *turn->ip ] -= pop( vm, turn );
      ++turn->ip;
      break;
   case PCD_MULSCRIPTVAR:
      turn->script->vars[ ( int ) *turn->ip ] *= pop( vm, turn );
      ++turn->ip;
      break;
   case PCD_MULMAPVAR:
      turn->module->map_vars[ ( int ) *turn->ip ]->value *= pop( vm, turn );
      ++turn->ip;
      break;
   case PCD_MULWORLDVAR:
      vm->world_vars[ ( int ) *turn->ip ] *= pop( vm, turn );
      ++turn->ip;
      break;
   case PCD_DIVSCRIPTVAR:
      {
         i32 r = pop( vm, turn );
         if ( r == 0 ) {
            goto divzero_err;
         }
         turn->script->vars[ ( int ) *turn->ip ] /= r;
         ++turn->ip;
      }
      break;
   case PCD_DIVMAPVAR:
      {
         i32 r = pop( vm, turn );
         if ( r == 0 ) {
            goto divzero_err;
         }
         turn->module->map_vars[ ( int ) *turn->ip ]->value /= r;
         ++turn->ip;
      }
      break;
   case PCD_DIVWORLDVAR:
      {
         i32 r = pop( vm, turn );
         if ( r == 0 ) {
            goto divzero_err;
         }
         vm->world_vars[ ( int ) *turn->ip ] /= r;
         ++turn->ip;
      }
      break;
   case PCD_MODSCRIPTVAR:
      {
         i32 r = pop( vm, turn );
         if ( r == 0 ) {
            goto modzero_err;
         }
         turn->script->vars[ ( int ) *turn->ip ] %= r;
         ++turn->ip;
      }
      break;
   case PCD_MODMAPVAR:
      {
         i32 r = pop( vm, turn );
         if ( r == 0 ) {
            goto modzero_err;
         }
         turn->module->map_vars[ ( int ) *turn->ip ]->value %= r;
         ++turn->ip;
      }
      break;
   case PCD_MODWORLDVAR:
      {
         i32 r = pop( vm, turn );
         if ( r == 0 ) {
            goto modzero_err;
         }
         vm->world_vars[ ( int ) *turn->ip ] %= r;
         ++turn->ip;
      }
      break;
   case PCD_INCSCRIPTVAR:
      ++turn->script->vars[ ( int ) *turn->ip ];
      ++turn->ip;
      break;
   case PCD_INCMAPVAR:
      ++turn->module->map_vars[ ( int ) *turn->ip ]->value;
      ++turn->ip;
      break;
   case PCD_INCWORLDVAR:
      ++vm->world_vars[ ( int ) *turn->ip ];
      ++turn->ip;
      break;
   case PCD_DECSCRIPTVAR:
      --turn->script->vars[ ( int ) *turn->ip ];
      ++turn->ip;
      break;
   case PCD_DECMAPVAR:
      --turn->module->map_vars[ ( int ) *turn->ip ]->value;
      ++turn->ip;
      break;
   case PCD_DECWORLDVAR:
      --vm->world_vars[ ( int ) *turn->ip ];
      ++turn->ip;
      break;
   case PCD_ANDSCRIPTVAR:
      turn->script->vars[ ( int ) *turn->ip ] &= pop( vm, turn );
      ++turn->ip;
      break;
   case PCD_ANDMAPVAR:
      turn->module->map_vars[ ( int ) *turn->ip ]->value &= pop( vm, turn );
      ++turn->ip;
      break;
   case PCD_ANDWORLDVAR:
      vm->world_vars[ ( int ) *turn->ip ] &= pop( vm, turn );
      ++turn->ip;
      break;
   case PCD_ANDGLOBALVAR:
      vm->global_vars[ ( int ) *turn->ip ] &= pop( vm, turn );
      ++turn->ip;
      break;
   case PCD_ORSCRIPTVAR:
      turn->script->vars[ ( int ) *turn->ip ] |= pop( vm, turn );
      ++turn->ip;
      break;
   case PCD_ORMAPVAR:
      turn->module->map_vars[ ( int ) *turn->ip ]->value |= pop( vm, turn );
      ++turn->ip;
      break;
   case PCD_ORWORLDVAR:
      vm->world_vars[ ( int ) *turn->ip ] |= pop( vm, turn );
      ++turn->ip;
      break;
   case PCD_ORGLOBALVAR:
      vm->global_vars[ ( int ) *turn->ip ] |= pop( vm, turn );
      ++turn->ip;
      break;
   case PCD_EORSCRIPTVAR:
      turn->script->vars[ ( int ) *turn->ip ] ^= pop( vm, turn );
      ++turn->ip;
      break;
   case PCD_EORMAPVAR:
      turn->module->map_vars[ ( int ) *turn->ip ]->value ^= pop( vm, turn );
      ++turn->ip;
      break;
   case PCD_EORWORLDVAR:
      vm->world_vars[ ( int ) *turn->ip ] ^= pop( vm, turn );
      ++turn->ip;
      break;
   case PCD_EORGLOBALVAR:
      vm->global_vars[ ( int ) *turn->ip ] ^= pop( vm, turn );
      ++turn->ip;
      break;
   case PCD_LSSCRIPTVAR:
      turn->script->vars[ ( int ) *turn->ip ] <<= pop( vm, turn );
      ++turn->ip;
      break;
   case PCD_LSMAPVAR:
      turn->module->map_vars[ ( int ) *turn->ip ]->value <<= pop( vm, turn );
      ++turn->ip;
      break;
   case PCD_LSWORLDVAR:
      vm->world_vars[ ( int ) *turn->ip ] <<= pop( vm, turn );
      ++turn->ip;
      break;
   case PCD_LSGLOBALVAR:
      vm->global_vars[ ( int ) *turn->ip ] <<= pop( vm, turn );
      ++turn->ip;
      break;
   case PCD_RSSCRIPTVAR:
      turn->script->vars[ ( int ) *turn->ip ] >>= pop( vm, turn );
      ++turn->ip;
      break;
   case PCD_RSMAPVAR:
      turn->module->map_vars[ ( int ) *turn->ip ]->value >>= pop( vm, turn );
      ++turn->ip;
      break;
   case PCD_RSWORLDVAR:
      vm->world_vars[ ( int ) *turn->ip ] >>= pop( vm, turn );
      ++turn->ip;
      break;
   case PCD_RSGLOBALVAR:
      vm->global_vars[ ( int ) *turn->ip ] >>= pop( vm, turn );
      ++turn->ip;
      break;
   case PCD_GOTO:
      {
         int offset;
         memcpy( &offset, turn->ip, sizeof( offset ) );
         turn->ip = turn->module->object.data + offset;
      }
      break;
   case PCD_IFGOTO:
      {
         int offset;
         memcpy( &offset, turn->ip, sizeof( offset ) );
         i32 value = pop( vm, turn );
         if ( value != 0 ) {
            turn->ip = turn->module->object.data + offset;
         }
         else {
            turn->ip += sizeof( offset );
         }
      }
      break;
   case PCD_DROP:
      pop( vm, turn );
      break;
   case PCD_DELAY:
      {
         i32 amount = pop( vm, turn );
         if ( amount > 0 ) {
            delay_current_script( vm, turn, amount );
            return;
         }
      }
      break;
   case PCD_DELAYDIRECT:
      {
         i32 amount;
         memcpy( &amount, turn->ip, sizeof( amount ) );
         turn->ip += sizeof( amount );
         if ( amount > 0 ) {
            delay_current_script( vm, turn, amount );
            return;
         }
      }
      break;
   case PCD_RANDOM:
      {
         i32 max = pop( vm, turn );
         i32 min = pop( vm, turn );
         push_random_number( turn, min, max );
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
      run_pcode_func( vm, turn );
      break;
   case PCD_RESTART:
      // TODO: Should be script->start?
      turn->ip = turn->module->object.data + turn->script->ip;
      break;
   case PCD_ANDLOGICAL:
      {
         i32 r = pop( vm, turn );
         i32 l = pop( vm, turn );
         push( turn, l && r );
      }
      break;
   case PCD_ORLOGICAL:
      {
         i32 r = pop( vm, turn );
         i32 l = pop( vm, turn );
         push( turn, l || r );
      }
      break;
   case PCD_ANDBITWISE:
      {
         i32 r = pop( vm, turn );
         i32 l = pop( vm, turn );
         push( turn, l & r );
      }
      break;
   case PCD_ORBITWISE:
      {
         i32 r = pop( vm, turn );
         i32 l = pop( vm, turn );
         push( turn, l | r );
      }
      break;
   case PCD_EORBITWISE:
      {
         i32 r = pop( vm, turn );
         i32 l = pop( vm, turn );
         push( turn, l ^ r );
      }
      break;
   case PCD_NEGATELOGICAL:
      push( turn, ! pop( vm, turn ) );
      break;
   case PCD_NEGATEBINARY:
      push( turn, ~ pop( vm, turn ) );
      break;
   case PCD_LSHIFT:
      {
         i32 r = pop( vm, turn );
         i32 l = pop( vm, turn );
         push( turn, l << r );
      }
      break;
   case PCD_RSHIFT:
      {
         i32 r = pop( vm, turn );
         i32 l = pop( vm, turn );
         push( turn, l >> r );
      }
      break;
   case PCD_UNARYMINUS:
      push( turn, - pop( vm, turn ) );
      break;
   case PCD_IFNOTGOTO:
      {
         int offset;
         memcpy( &offset, turn->ip, sizeof( offset ) );
         i32 value = pop( vm, turn );
         if ( value == 0 ) {
            turn->ip = turn->module->object.data + offset;
         }
         else {
            turn->ip += sizeof( offset );
         }
      }
      break;
   case PCD_LINESIDE:
      run_pcode_func( vm, turn );
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
            number = pop( vm, turn );
         }
         struct instance* target_script = vm_get_active_script( vm, number );
         if ( target_script ) {
            add_waiting_script( target_script, turn->script );
            turn->script->state = SCRIPTSTATE_WAITING;
            turn->script->ip = turn->ip - turn->module->object.data;
            return;
         }
      }
      break;
   case PCD_CLEARLINESPECIAL:
      run_pcode_func( vm, turn );
      break;
   case PCD_CASEGOTO:
      {
         struct {
            int value;
            int offset;
         } args;
         memcpy( &args, turn->ip, sizeof( args ) );
         i32 value = pop( vm, turn );
         if ( value == args.value ) {
            turn->ip = turn->module->object.data + args.offset;
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
   case PCD_PRINTSTRING:
      {
         struct list_iter i;
         list_iterate( &turn->module->strings, &i );
         const i32 requested_string = pop( vm, turn );
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
         sprintf( text, "%d", pop( vm, turn ) );
         str_append( &vm->msg, text );
      }
      break;
   case PCD_PRINTCHARACTER:
      {
         char text[] = { ( char ) pop( vm, turn ), '\0' };
         str_append( &vm->msg, text );
      }
      break;
   case PCD_PRINTSCRIPTCHARARRAY:
   case PCD_PRINTMAPCHARARRAY:
   case PCD_PRINTWORLDCHARARRAY:
   case PCD_PRINTGLOBALCHARARRAY:
   case PCD_PRINTSCRIPTCHRANGE:
   case PCD_PRINTMAPCHRANGE:
   case PCD_PRINTWORLDCHRANGE:
   case PCD_PRINTGLOBALCHRANGE:
   case PCD_PRINTBIND:
   case PCD_PRINTBINARY:
   case PCD_PRINTHEX:
      UNIMPLEMENTED;
      break;
   case PCD_ENDPRINT:
   case PCD_ENDPRINTBOLD:
      v_diag( vm, DIAG_NONE, "%s", vm->msg.value );
      break;
   case PCD_ENDLOG:
      v_diag( vm, DIAG_DBG, "%s", vm->msg.value );
      break;
   case PCD_SAVESTRING:
      UNIMPLEMENTED;
      break;
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
      run_pcode_func( vm, turn );
      break;
   case PCD_PRINTNAME:
      printf( "error: instruction not supported\n" );
      v_bail( vm );
      break;
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
      run_pcode_func( vm, turn );
      break;
   case PCD_PRINTFIXED:
   case PCD_PRINTLOCALIZED:
   case PCD_MOREHUDMESSAGE:
   case PCD_OPTHUDMESSAGE:
   case PCD_ENDHUDMESSAGE:
   case PCD_ENDHUDMESSAGEBOLD:
      UNIMPLEMENTED;
      break;
   case PCD_SETFONT:
   case PCD_SETFONTDIRECT:
      run_pcode_func( vm, turn );
      break;
   case PCD_PUSHBYTE:
      push( turn, *turn->ip );
      ++turn->ip;
      break;
   case PCD_LSPEC1DIRECTB:
   case PCD_LSPEC2DIRECTB:
   case PCD_LSPEC3DIRECTB:
   case PCD_LSPEC4DIRECTB:
   case PCD_LSPEC5DIRECTB:
      vm_run_lspec( vm, turn );
      break;
   case PCD_DELAYDIRECTB:
      {
         i32 amount = turn->ip[ 0 ];
         if ( amount > 0 ) {
            delay_current_script( vm, turn, amount );
            return;
         }
      }
      break;
   case PCD_RANDOMDIRECTB:
      {
         i32 min = turn->ip[ 0 ];
         i32 max = turn->ip[ 1 ];
         push_random_number( turn, min, max );
         turn->ip += sizeof( turn->ip[ 0 ] ) * 2;
      }
      break;
   case PCD_PUSHBYTES:
      {
         i32 count = turn->ip[ 0 ];
         for ( int i = 0; i < count; ++i ) {
            push( turn, turn->ip[ 1 + i ] );
         }
         turn->ip += sizeof( turn->ip[ 0 ] ) * ( 1 + count );
      }
      break;
   case PCD_PUSH2BYTES:
      // TODO: Make sure we do not overflow the IP segment. Make a function
      // that reads the IP and protects against overflows.
      push( turn, turn->ip[ 0 ] );
      push( turn, turn->ip[ 1 ] );
      turn->ip += sizeof( turn->ip[ 0 ] ) * 2;
      break;
   case PCD_PUSH3BYTES:
      push( turn, turn->ip[ 0 ] );
      push( turn, turn->ip[ 1 ] );
      push( turn, turn->ip[ 2 ] );
      turn->ip += sizeof( turn->ip[ 0 ] ) * 3;
      break;
   case PCD_PUSH4BYTES:
      push( turn, turn->ip[ 0 ] );
      push( turn, turn->ip[ 1 ] );
      push( turn, turn->ip[ 2 ] );
      push( turn, turn->ip[ 3 ] );
      turn->ip += sizeof( turn->ip[ 0 ] ) * 4;
      break;
   case PCD_PUSH5BYTES:
      push( turn, turn->ip[ 0 ] );
      push( turn, turn->ip[ 1 ] );
      push( turn, turn->ip[ 2 ] );
      push( turn, turn->ip[ 3 ] );
      push( turn, turn->ip[ 4 ] );
      turn->ip += sizeof( turn->ip[ 0 ] ) * 5;
      break;
   case PCD_SETTHINGSPECIAL:
      run_pcode_func( vm, turn );
      break;
   case PCD_ASSIGNGLOBALVAR:
      vm->global_vars[ ( int ) *turn->ip ] = pop( vm, turn );
      ++turn->ip;
      break;
   case PCD_PUSHGLOBALVAR:
      push( turn, vm->global_vars[ ( int ) *turn->ip ] );
      ++turn->ip;
      break;
   case PCD_ADDGLOBALVAR:
      vm->global_vars[ ( int ) *turn->ip ] += pop( vm, turn );
      ++turn->ip;
      break;
   case PCD_SUBGLOBALVAR:
      vm->global_vars[ ( int ) *turn->ip ] -= pop( vm, turn );
      ++turn->ip;
      break;
   case PCD_MULGLOBALVAR:
      vm->global_vars[ ( int ) *turn->ip ] *= pop( vm, turn );
      ++turn->ip;
      break;
   case PCD_DIVGLOBALVAR:
      {
         i32 r = pop( vm, turn );
         if ( r == 0 ) {
            goto divzero_err;
         }
         vm->global_vars[ ( int ) *turn->ip ] /= r;
         ++turn->ip;
      }
      break;
   case PCD_MODGLOBALVAR:
      {
         i32 r = pop( vm, turn );
         if ( r == 0 ) {
            goto modzero_err;
         }
         vm->global_vars[ ( int ) *turn->ip ] %= r;
         ++turn->ip;
      }
      break;
   case PCD_INCGLOBALVAR:
      ++vm->global_vars[ ( int ) *turn->ip ];
      ++turn->ip;
      break;
   case PCD_DECGLOBALVAR:
      --vm->global_vars[ ( int ) *turn->ip ];
      ++turn->ip;
      break;
   case PCD_FADETO:
   case PCD_FADERANGE:
   case PCD_CANCELFADE:
   case PCD_PLAYMOVIE:
   case PCD_SETFLOORTRIGGER:
   case PCD_SETCEILINGTRIGGER:
   case PCD_GETACTORX:
   case PCD_GETACTORY:
   case PCD_GETACTORZ:
      run_pcode_func( vm, turn );
      break;
   case PCD_STARTTRANSLATION:
   case PCD_TRANSLATIONRANGE1:
   case PCD_TRANSLATIONRANGE2:
	case PCD_TRANSLATIONRANGE3:
	case PCD_TRANSLATIONRANGE4:
	case PCD_TRANSLATIONRANGE5:
   case PCD_ENDTRANSLATION:
      UNIMPLEMENTED;
      break;
   case PCD_CALL:
   case PCD_CALLDISCARD:
      run_call( vm, turn );
      break;
   case PCD_RETURNVOID:
   case PCD_RETURNVAL:
      run_return( vm, turn );
      break;
   case PCD_PUSHMAPARRAY:
      {
         int index = pop( vm, turn );
         if ( index >= 0 && index < turn->module->map_vars[ ( int ) *turn->ip ]->size ) {
            push( turn, turn->module->map_vars[ ( int ) *turn->ip ]->elements[ index ] );
         }
         else {
            push( turn, 0 );
         }
         ++turn->ip;
      }
      break;
   case PCD_ASSIGNMAPARRAY:
      {
         i32 value = pop( vm, turn );
         i32 index = pop( vm, turn );
         if ( index >= 0 && index < turn->module->map_vars[ ( int ) *turn->ip ]->size ) {
            turn->module->map_vars[ ( int ) *turn->ip ]->elements[ index ] = value;
         }
         else {
            push( turn, 0 );
         }
         ++turn->ip;
      }
      break;
   case PCD_ADDMAPARRAY:
   case PCD_SUBMAPARRAY:
   case PCD_MULMAPARRAY:
   case PCD_DIVMAPARRAY:
   case PCD_MODMAPARRAY:
   case PCD_ANDMAPARRAY:
   case PCD_EORMAPARRAY:
   case PCD_ORMAPARRAY:
   case PCD_LSMAPARRAY:
   case PCD_RSMAPARRAY:
      UNIMPLEMENTED;
      break;
   case PCD_INCMAPARRAY:
      {
         int index = pop( vm, turn );
         if ( index >= 0 && index < turn->module->map_vars[ ( int ) *turn->ip ]->size ) {
            ++turn->module->map_vars[ ( int ) *turn->ip ]->elements[ index ];
         }
         else {
         }
         ++turn->ip;
      }
      break;
   case PCD_DECMAPARRAY:
      UNIMPLEMENTED;
      break;
   case PCD_DUP:
      {
         i32 value = pop( vm, turn );
         push( turn, value );
         push( turn, value );
      }
      break;
   case PCD_SWAP:
      {
         i32 a = pop( vm, turn );
         i32 b = pop( vm, turn );
         push( turn, a );
         push( turn, b );
      }
      break;
   case PCD_SIN:
   case PCD_COS:
   case PCD_VECTORANGLE:
   case PCD_CHECKWEAPON:
   case PCD_SETWEAPON:
      run_pcode_func( vm, turn );
      break;
   case PCD_TAGSTRING:
      //UNIMPLEMENTED;
      break;
   case PCD_PUSHWORLDARRAY:
      run_pushworldarray( vm, turn );
      break;
   case PCD_ASSIGNWORLDARRAY:
   case PCD_ADDWORLDARRAY:
   case PCD_SUBWORLDARRAY:
   case PCD_MULWORLDARRAY:
   case PCD_DIVWORLDARRAY:
   case PCD_MODWORLDARRAY:
   case PCD_ANDWORLDARRAY:
   case PCD_EORWORLDARRAY:
   case PCD_ORWORLDARRAY:
   case PCD_LSWORLDARRAY:
   case PCD_RSWORLDARRAY:
      run_updateworldarray( vm, turn );
      break;
   case PCD_INCWORLDARRAY:
   case PCD_DECWORLDARRAY:
      UNIMPLEMENTED;
      break;
   case PCD_PUSHGLOBALARRAY:
   case PCD_ASSIGNGLOBALARRAY:
   case PCD_ADDGLOBALARRAY:
   case PCD_SUBGLOBALARRAY:
   case PCD_MULGLOBALARRAY:
   case PCD_DIVGLOBALARRAY:
   case PCD_MODGLOBALARRAY:
   case PCD_ANDGLOBALARRAY:
   case PCD_EORGLOBALARRAY:
   case PCD_ORGLOBALARRAY:
   case PCD_LSGLOBALARRAY:
   case PCD_RSGLOBALARRAY:
   case PCD_INCGLOBALARRAY:
   case PCD_DECGLOBALARRAY:
      UNIMPLEMENTED;
      break;
   case PCD_SETMARINEWEAPON:
   case PCD_SETACTORPROPERTY:
   case PCD_GETACTORPROPERTY:
   case PCD_PLAYERNUMBER:
   case PCD_ACTIVATORTID:
   case PCD_SETMARINESPRITE:
   case PCD_GETSCREENWIDTH:
   case PCD_GETSCREENHEIGHT:
   case PCD_THINGPROJECTILE2:
   case PCD_STRLEN:
   case PCD_SETHUDSIZE:
   case PCD_GETCVAR:
      run_pcode_func( vm, turn );
      break;
   case PCD_CASEGOTOSORTED:
      UNIMPLEMENTED;
      break;
   case PCD_SETRESULTVALUE:
   case PCD_GETLINEROWOFFSET:
   case PCD_GETACTORFLOORZ:
   case PCD_GETACTORANGLE:
   case PCD_GETSECTORFLOORZ:
   case PCD_GETSECTORCEILINGZ:
      run_pcode_func( vm, turn );
      break;
   case PCD_LSPEC5RESULT:
      vm_run_lspec( vm, turn );
      break;
   case PCD_GETSIGILPIECES:
   case PCD_GETLEVELINFO:
   case PCD_CHANGESKY:
   case PCD_PLAYERINGAME:
   case PCD_PLAYERISBOT:
   case PCD_SETCAMERATOTEXTURE:
   case PCD_GETAMMOCAPACITY:
   case PCD_SETAMMOCAPACITY:
   case PCD_SETACTORANGLE:
   case PCD_SPAWNPROJECTILE:
   case PCD_GETSECTORLIGHTLEVEL:
   case PCD_GETACTORCEILINGZ:
   case PCD_SETACTORPOSITION:
   case PCD_CLEARACTORINVENTORY:
   case PCD_GIVEACTORINVENTORY:
   case PCD_TAKEACTORINVENTORY:
   case PCD_CHECKACTORINVENTORY:
   case PCD_THINGCOUNTNAME:
   case PCD_SPAWNSPOTFACING:
   case PCD_PLAYERCLASS:
   case PCD_GETPLAYERINFO:
   case PCD_CHANGELEVEL:
   case PCD_SECTORDAMAGE:
   case PCD_REPLACETEXTURES:
   case PCD_GETACTORPITCH:
   case PCD_SETACTORPITCH:
   case PCD_SETACTORSTATE:
   case PCD_THINGDAMAGE2:
   case PCD_USEINVENTORY:
   case PCD_USEACTORINVENTORY:
   case PCD_CHECKACTORCEILINGTEXTURE:
   case PCD_CHECKACTORFLOORTEXTURE:
   case PCD_GETACTORLIGHTLEVEL:
   case PCD_SETMUGSHOTSTATE:
   case PCD_THINGCOUNTSECTOR:
   case PCD_THINGCOUNTNAMESECTOR:
   case PCD_CHECKPLAYERCAMERA:
   case PCD_MORPHACTOR:
   case PCD_UNMORPHACTOR:
   case PCD_GETPLAYERINPUT:
   case PCD_CLASSIFYACTOR:
      run_pcode_func( vm, turn );
      break;
   case PCD_CALLFUNC:
      vm_run_callfunc( vm, turn );
      break;
   case PCD_ASSIGNSCRIPTARRAY:
      {
         i32 value = pop( vm, turn );
         i32 index = pop( vm, turn );
         i32* element = get_script_element( vm, turn, turn->ip[ 0 ], index );
         element[ 0 ] = value;
         ++turn->ip;
      }
      break;
   case PCD_PUSHSCRIPTARRAY:
      {
         i32 index = pop( vm, turn );
         i32* element = get_script_element( vm, turn, turn->ip[ 0 ], index );
         push( turn, element[ 0 ] );
         ++turn->ip;
      }
      break;
   case PCD_ADDSCRIPTARRAY:
      {
         i32 value = pop( vm, turn );
         i32 index = pop( vm, turn );
         i32* element = get_script_element( vm, turn, turn->ip[ 0 ], index );
         element[ 0 ] += value;
         ++turn->ip;
      }
      break;
   case PCD_SUBSCRIPTARRAY:
      {
         i32 value = pop( vm, turn );
         i32 index = pop( vm, turn );
         i32* element = get_script_element( vm, turn, turn->ip[ 0 ], index );
         element[ 0 ] -= value;
         ++turn->ip;
      }
      break;
   case PCD_MULSCRIPTARRAY:
      {
         i32 value = pop( vm, turn );
         i32 index = pop( vm, turn );
         i32* element = get_script_element( vm, turn, turn->ip[ 0 ], index );
         element[ 0 ] *= value;
         ++turn->ip;
      }
      break;
   case PCD_DIVSCRIPTARRAY:
      {
         i32 value = pop( vm, turn );
         i32 index = pop( vm, turn );
         i32* element = get_script_element( vm, turn, turn->ip[ 0 ], index );
         check_div_by_zero( vm, turn, value );
         element[ 0 ] /= value;
         ++turn->ip;
      }
      break;
   case PCD_MODSCRIPTARRAY:
      {
         i32 value = pop( vm, turn );
         i32 index = pop( vm, turn );
         i32* element = get_script_element( vm, turn, turn->ip[ 0 ], index );
         check_div_by_zero( vm, turn, value );
         element[ 0 ] %= value;
         ++turn->ip;
      }
      break;
   case PCD_ANDSCRIPTARRAY:
      {
         i32 value = pop( vm, turn );
         i32 index = pop( vm, turn );
         i32* element = get_script_element( vm, turn, turn->ip[ 0 ], index );
         element[ 0 ] &= value;
         ++turn->ip;
      }
      break;
   case PCD_ORSCRIPTARRAY:
      {
         i32 value = pop( vm, turn );
         i32 index = pop( vm, turn );
         i32* element = get_script_element( vm, turn, turn->ip[ 0 ], index );
         element[ 0 ] |= value;
         ++turn->ip;
      }
      break;
   case PCD_EORSCRIPTARRAY:
      {
         i32 value = pop( vm, turn );
         i32 index = pop( vm, turn );
         i32* element = get_script_element( vm, turn, turn->ip[ 0 ], index );
         element[ 0 ] ^= value;
         ++turn->ip;
      }
      break;
   case PCD_LSSCRIPTARRAY:
      {
         i32 value = pop( vm, turn );
         i32 index = pop( vm, turn );
         i32* element = get_script_element( vm, turn, turn->ip[ 0 ], index );
         element[ 0 ] <<= value;
         ++turn->ip;
      }
      break;
   case PCD_RSSCRIPTARRAY:
      {
         i32 value = pop( vm, turn );
         i32 index = pop( vm, turn );
         i32* element = get_script_element( vm, turn, turn->ip[ 0 ], index );
         element[ 0 ] >>= value;
         ++turn->ip;
      }
      break;
   case PCD_INCSCRIPTARRAY:
      {
         i32 index = pop( vm, turn );
         i32* element = get_script_element( vm, turn, turn->ip[ 0 ], index );
         ++element[ 0 ];
         ++turn->ip;
      }
      break;
   case PCD_DECSCRIPTARRAY:
      {
         i32 index = pop( vm, turn );
         i32* element = get_script_element( vm, turn, turn->ip[ 0 ], index );
         --element[ 0 ];
         ++turn->ip;
      }
      break;
   case PCD_STRCPYTOSCRIPTCHRANGE:
   case PCD_STRCPYTOMAPCHRANGE:
   case PCD_STRCPYTOWORLDCHRANGE:
   case PCD_STRCPYTOGLOBALCHRANGE:
      UNIMPLEMENTED;
      break;
   case PCD_PUSHFUNCTION:
   case PCD_CALLSTACK:
   case PCD_SCRIPTWAITNAMED:
   case PCD_GOTOSTACK:
      UNIMPLEMENTED;
      break;
   default:
      v_diag( vm, DIAG_FATALERR,
         "encountered an unknown instruction (opcode is %d)", turn->opcode );
      v_bail( vm );
   }
}


static void check_div_by_zero( struct vm* vm, struct turn* turn,
   i32 denominator ) {
   if ( denominator == 0 ) {
      v_diag( vm, DIAG_FATALERR,
         "division by zero in script %d",
         turn->script->script->number );
      v_bail( vm );
   }
}

static i32* get_script_var( struct vm* vm, struct turn* turn, i32 index ) {
   if ( vm->call_stack != NULL ) {
      return &vm->call_stack->locals[ index ];
   }
   else {
      return &turn->script->vars[ index ];
   }
}

static i32* get_script_element( struct vm* vm, struct turn* turn,
   i32 array_index, i32 element ) {
   if ( vm->call_stack != NULL ) {
      return get_element( vm, turn,
         vm->call_stack->arrays,
         &vm->call_stack->func->arrays[ array_index ],
         element );
   }
   else {
      return get_element( vm, turn,
         turn->script->arrays,
         &turn->script->script->arrays[ array_index ],
         element );
   }
}

static i32* get_element( struct vm* vm, struct turn* turn, i32* array_data,
   struct script_array* entry, int element ) {
   return array_data + entry->start + element;
}

/**
 * Implements the following instructions:
 * - PCD_ASSIGNWORLDARRAY
 * - PCD_ADDWORLDARRAY
 * - PCD_SUBWORLDARRAY
 */
static void run_updateworldarray( struct vm* vm, struct turn* turn ) {
   // Get off the stack the element index and the value to store in the
   // element. The value is at the top of the stack, followed by the element
   // index.
   i32 value = pop( vm, turn );
   i32 index = pop( vm, turn );
   i32 array_index = turn->ip[ 0 ];
   struct vector* vector = get_world_vector( vm, array_index );
   if ( vector ) {
      extend_array_if_out_of_bounds( vector, index );
      // Access the element and store the value in the element.
      struct vector_result result = vector_get( vector, index );
      switch ( result.err ) {
      case VECTORGETERR_NONE:
         {
            i32* element = result.element;
            // Prevent division by zero.
            switch ( turn->opcode ) {
            case PCD_DIVWORLDARRAY:
            case PCD_MODWORLDARRAY:
               if ( value == 0 ) {
                  v_diag( vm, DIAG_ERR,
                     "division by zero in script %d",
                     turn->script->script->number );
                  v_bail( vm );
               }
               break;
            default:
               break;
            }
            switch ( turn->opcode ) {
            case PCD_ASSIGNWORLDARRAY: *element = value; break;
            case PCD_ADDWORLDARRAY: *element += value; break;
            case PCD_SUBWORLDARRAY: *element -= value; break;
            case PCD_MULWORLDARRAY: *element *= value; break;
            case PCD_DIVWORLDARRAY: *element /= value; break;
            case PCD_MODWORLDARRAY: *element %= value; break;
            case PCD_ANDWORLDARRAY: *element &= value; break;
            case PCD_EORWORLDARRAY: *element ^= value; break;
            case PCD_ORWORLDARRAY:  *element |= value; break;
            case PCD_LSWORLDARRAY:  *element <<= value; break;
            case PCD_RSWORLDARRAY:  *element >>= value; break;
            default:
               UNREACHABLE();
            }
         }
         break;
      default:
         UNREACHABLE();
      }
   }
   else {
      v_diag( vm, DIAG_ERR,
         "script %d attempted to assign to a non-existant array "
         "(index of array is %d)", turn->script->script->number, array_index );
      turn->script->state = SCRIPTSTATE_TERMINATED;
   }
   ++turn->ip;
}

/**
 * Retrieves a world array, represented by a vector. If a world array with the
 * specified index does not exist, NULL is returned.
 */
static struct vector* get_world_vector( struct vm* vm, i32 index ) {
   if ( index >= 0 && index < ARRAY_SIZE( vm->world_arrays ) ) {
      return &vm->world_arrays[ index ];
   }
   else {
      return NULL;
   }
}

/**
 * Extends the vector if index is outside its range.
 */
static void extend_array_if_out_of_bounds( struct vector* vector, i32 index ) {
   const i32 required_capacity = index + 1;
   if ( vector->capacity < required_capacity ) {
      const i32 old_capacity = vector->capacity;
      // In user code, for sequentially increasing indexes, like in a for loop,
      // having to extend the array every time a larger index is used is
      // wasteful. To reduce allocations, extra elements are allocated after
      // the index.
      enum { EXTRA_ELEMENTS = 1000 };
      const i32 new_capacity = required_capacity + EXTRA_ELEMENTS;
      switch ( vector_grow( vector, new_capacity ) ) {
      case VECTORGROWERR_NONE:
         // Initialize any new space with zeroes.
         for ( isize i = 0; i < new_capacity - old_capacity; ++i ) {
            struct vector_result result = vector_get( vector,
               old_capacity + i );
            switch ( result.err ) {
            case VECTORGETERR_NONE:
               {
                  const i32 zero = 0;
                  memcpy( result.element, &zero, sizeof( zero ) );
               }
               break;
            default:
               UNREACHABLE();
            }
         }
         break;
      default:
         break;
      }
   }
}

/**
 * Implements the PCD_PUSHWORLDARRAY instruction.
 */
static void run_pushworldarray( struct vm* vm, struct turn* turn ) {
   // PCD_PUSHWORLDARRAY instruction takes one argument off the stack. This
   // argument is an index of the element to retrieve from the array.
   i32 index = pop( vm, turn );
   i32 array_index = turn->ip[ 0 ];
   struct vector* vector = get_world_vector( vm, array_index );
   if ( vector ) {
      struct vector_result result = vector_get( vector, index );
      if ( result.err == VECTORGETERR_NONE ) {
         i32 value;
         memcpy( &value, result.element, sizeof( value ) );
         push( turn, value );
      }
      else {
         // On an out-of-bounds condition, return a 0 just like the ZDoom
         // virtual machine.
         push( turn, 0 );
      }
   }
   else {
      v_diag( vm, DIAG_ERR,
         "script %d attempted to pop non-existant array "
         "(index of array is %d)", turn->script->script->number, array_index );
      turn->script->state = SCRIPTSTATE_TERMINATED;
   }
   // Move past the array index argument.
   ++turn->ip;
}

static void decode_opcode( struct vm* vm, struct turn* turn ) {
   i32 opc = PCD_NOP;
   if ( turn->module->object.small_code ) {
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

void vm_push( struct turn* turn, i32 value ) {
   push( turn, value );
}

static void push( struct turn* turn, i32 value ) {
   *turn->stack = value;
   ++turn->stack;
}

i32 vm_pop( struct vm* vm, struct turn* turn ) {
   return pop( vm, turn );
}

static i32 pop( struct vm* vm, struct turn* turn ) {
   if ( turn->stack == turn->stack_start ) {
      v_diag( vm, DIAG_FATALERR,
         "attempting to pop() empty stack" );
      v_bail( vm );
   }
   --turn->stack;
   return *turn->stack;
}

void add_waiting_script( struct instance* script,
   struct instance* waiting_script ) {
   if ( script->waiting ) {
      script->waiting_tail->next_waiting = waiting_script;
   }
   else {
      script->waiting = waiting_script;
   }
   script->waiting_tail = waiting_script;
}

static void delay_current_script( struct vm* vm, struct turn* turn,
   i32 amount ) {
   turn->script->delay_amount = amount;
   turn->script->state = SCRIPTSTATE_DELAYED;
   turn->script->ip = turn->ip - turn->module->object.data;
   turn->script->resume_time = vm->tics + amount;
}

static void push_random_number( struct turn* turn, i32 min, i32 max ) {
   push( turn, min + rand() % ( max - min + 1 ) );
}

static void run_call( struct vm* vm, struct turn* turn ) {
   u8 index = turn->ip[ 0 ];
   turn->ip += sizeof( index );
   struct func* func = vm_find_func( vm, turn->module, index );
   if ( func == null ) {
      v_diag( vm, DIAG_FATALERR,
         "failed to find function with index %d", index );
      v_bail( vm );
   }
   struct call* call = push_call( vm );
   call->func = func;
   call->return_module = turn->module;
   call->return_addr = turn->ip;
   call->discard_return_value = ( turn->opcode == PCD_CALLDISCARD );
   call->locals = turn->stack - func->params;
   call->arrays = vm_alloc_local_array_space( func->total_array_size );
   turn->stack += func->local_size;
   turn->module = func->module;
   turn->ip = func->module->object.data + func->start;
   // Nullify local variables.
   for ( isize i = 0; i < func->local_size; ++i ) {
      call->locals[ func->params + i ] = 0;
   }
}

struct func* vm_find_func( struct vm* vm, struct module* module, i32 index ) {
   if ( index >= 0 && index < module->func_table.size ) {
      return module->func_table.linked_entries[ index ];
   }
   else {
      v_diag( vm, DIAG_FATALERR,
         "invalid function requested (index of function is %d) ", index );
      v_bail( vm );
      return NULL;
   }
}

static struct call* push_call( struct vm* vm ) {
   struct call* call = mem_alloc( sizeof( *call ) );
   call->prev = vm->call_stack;
   vm->call_stack = call;
   return call;
}

static void run_return( struct vm* vm, struct turn* turn ) {
   struct call* call = pop_call( vm );
   i32 return_value = 0;
   if ( turn->opcode == PCD_RETURNVAL ) {
      return_value = pop( vm, turn );
   }
   turn->stack = call->locals;
   if ( ! call->discard_return_value ) {
      push( turn, return_value );
   }
   turn->module = call->return_module;
   turn->ip = call->return_addr;
   mem_free( call->arrays );
   mem_free( call );
}

static struct call* pop_call( struct vm* vm ) {
   if ( vm->call_stack != NULL ) {
      struct call* call = vm->call_stack;
      vm->call_stack = call->prev;
      return call;
   }
   else {
      v_diag( vm, DIAG_FATALERR,
         "attempting to pop an empty call stack" );
      v_bail( vm );
      return NULL;
   }
}

static void run_pcode_func( struct vm* vm, struct turn* turn ) {
   enum opcode opcode = translate_direct_opcode( turn->opcode );
   bool direct_opcode = ( opcode != turn->opcode );
   const struct pcode_func* func = get_pcode_func( opcode );
   if ( func == NULL ) {
      v_diag( vm, DIAG_INTERNAL | DIAG_ERR,
         "opcode %d missing a builtin function entry", opcode );
      v_bail( vm );
   }
   if ( ! ( direct_opcode || vm_get_stack_size( turn ) >= func->num_args ) ) {
      v_diag( vm, DIAG_FATALERR,
         "not enough arguments on the stack for `%s()` (need %d arguments, "
         "but only %d given)", func->name, func->num_args,
         vm_get_stack_size( turn ) );
      v_bail( vm );
   }
   v_diag( vm, DIAG_DBG | DIAG_WARN | DIAG_MULTI_PART,
      "ignoring %s", func->name );
   if ( func->num_args > 0 ) {
      v_diag_more( vm, "( " );
      for ( i32 i = 0; i < func->num_args; ++i ) {
         i32 arg;
         if ( direct_opcode ) {
            memcpy( &arg, turn->ip + i * sizeof( arg ), sizeof( arg ) );
         }
         else {
            arg = turn->stack[ i - func->num_args ];
         }
         v_diag_more( vm, "%d", arg );
         if ( i + 1 < func->num_args ) {
            v_diag_more( vm, ", " );
         }
      }
      v_diag_more( vm, " )\n" );
   }
   else {
      v_diag_more( vm, "()\n" );
   }
   if ( direct_opcode ) {
      turn->ip += sizeof( i32 ) * func->num_args;
   }
   else {
      turn->stack -= func->num_args;
   }
   if ( func->returns_value ) {
      push( turn, 0 );
   }
}

static enum opcode translate_direct_opcode( enum opcode opcode ) {
   switch ( opcode ) {
   case PCD_THINGCOUNTDIRECT: return PCD_THINGCOUNT;
   case PCD_TAGWAITDIRECT: return PCD_TAGWAIT;
   case PCD_POLYWAITDIRECT: return PCD_POLYWAIT;
   case PCD_CHANGEFLOORDIRECT: return PCD_CHANGEFLOOR;
   case PCD_CHANGECEILINGDIRECT: return PCD_CHANGECEILING;
   case PCD_SCRIPTWAITDIRECT: return PCD_SCRIPTWAIT;
   case PCD_CONSOLECOMMANDDIRECT: return PCD_CONSOLECOMMAND;
   case PCD_SETGRAVITYDIRECT: return PCD_SETGRAVITY;
   case PCD_SETAIRCONTROLDIRECT: return PCD_SETAIRCONTROL;
   case PCD_GIVEINVENTORYDIRECT: return PCD_GIVEINVENTORY;
   case PCD_TAKEINVENTORYDIRECT: return PCD_TAKEINVENTORY;
   case PCD_CHECKINVENTORYDIRECT: return PCD_CHECKINVENTORY;
   case PCD_SPAWNDIRECT: return PCD_SPAWN;
   case PCD_SPAWNSPOTDIRECT: return PCD_SPAWNSPOT;
   case PCD_SETMUSICDIRECT: return PCD_SETMUSIC;
   case PCD_LOCALSETMUSICDIRECT: return PCD_LOCALSETMUSIC;
   case PCD_SETFONTDIRECT: return PCD_SETFONT;
   default: return opcode;
   }
}

static const struct pcode_func* get_pcode_func( enum opcode opcode ) {
   static const struct pcode_func table[] = {
      { "ThingCount", PCD_THINGCOUNT, 2, true },
      { "TagWait", PCD_TAGWAIT, 1, false },
      { "PolyWait", PCD_POLYWAIT, 1, false },
      { "ChangeFloor", PCD_CHANGEFLOOR, 2, false },
      { "ChangeCeiling", PCD_CHANGECEILING, 2, false },
      { "LineSide", PCD_LINESIDE, 0, true },
      { "ClearLineSpecial", PCD_CLEARLINESPECIAL, 0, false },
      { "PlayerCount", PCD_PLAYERCOUNT, 0, true },
      { "GameType", PCD_GAMETYPE, 0, true },
      { "GameSkill", PCD_GAMESKILL, 0, true },
      { "Timer", PCD_TIMER, 0, true },
      { "SectorSound", PCD_SECTORSOUND, 2, false },
      { "AmbientSound", PCD_AMBIENTSOUND, 2, false },
      { "SoundSequence", PCD_SOUNDSEQUENCE, 1, false },
      { "SetLineTexture", PCD_SETLINETEXTURE, 4, false },
      { "SetLineBlocking", PCD_SETLINEBLOCKING, 2, false },
      { "SetLineSpecial", PCD_SETLINESPECIAL, 6, false },
      { "ThingSound", PCD_THINGSOUND, 3, false },
      { "ActivatorSound", PCD_ACTIVATORSOUND, 2, false },
      { "LocalAmbientSound", PCD_LOCALAMBIENTSOUND, 2, false },
      { "SetLineMonsterBlocking", PCD_SETLINEMONSTERBLOCKING, 2, false },
      { "IsNetworkGame", PCD_ISMULTIPLAYER, 0, true },
      { "PlayerTeam", PCD_PLAYERTEAM, 0, true },
      { "PlayerHealth", PCD_PLAYERHEALTH, 0, true },
      { "PlayerArmorPoints", PCD_PLAYERARMORPOINTS, 0, true },
      { "PlayerFrags", PCD_PLAYERFRAGS, 0, true },
      { "BlueTeamCount", PCD_BLUETEAMCOUNT, 0, true },
      { "RedTeamCount", PCD_REDTEAMCOUNT, 0, true },
      { "BlueTeamScore", PCD_BLUETEAMSCORE, 0, true },
      { "RedTeamScore", PCD_REDTEAMSCORE, 0, true },
      { "IsOneFlagCTF", PCD_ISONEFLAGCTF, 0, true },
      { "GetInvasionWave", PCD_GETINVASIONWAVE, 0, true },
      { "GetInvasionState", PCD_GETINVASIONSTATE, 0, true },
      { "Music_Change", PCD_MUSICCHANGE, 2, false },
      { "ConsoleCommand", PCD_CONSOLECOMMAND, 3, false },
      { "SinglePlayer", PCD_SINGLEPLAYER, 0, true },
      { "FixedMul", PCD_FIXEDMUL, 2, true },
      { "FixedDiv", PCD_FIXEDDIV, 2, true },
      { "SetGravity", PCD_SETGRAVITY, 1, false },
      { "SetAirControl", PCD_SETAIRCONTROL, 1, false },
      { "ClearInventory", PCD_CLEARINVENTORY, 0, false },
      { "GiveInventory", PCD_GIVEINVENTORY, 2, false },
      { "TakeInventory", PCD_TAKEINVENTORY, 2, false },
      { "CheckInventory", PCD_CHECKINVENTORY, 1, true },
      { "Spawn", PCD_SPAWN, 6, true },
      { "SpawnSpot", PCD_SPAWNSPOT, 4, true },
      { "SetMusic", PCD_SETMUSIC, 3, false },
      { "LocalSetMusic", PCD_LOCALSETMUSIC, 3, false },
      { "SetFont", PCD_SETFONT, 1, false },
      { "SetThingSpecial", PCD_SETTHINGSPECIAL, 7, false },
      { "FadeTo", PCD_FADETO, 5, false },
      { "FadeRange", PCD_FADERANGE, 9, false },
      { "CancelFade", PCD_CANCELFADE, 0, false },
      { "PlayMovie", PCD_PLAYMOVIE, 1, true },
      { "SetFloorTrigger", PCD_SETFLOORTRIGGER, 8, false },
      { "SetCeilingTrigger", PCD_SETCEILINGTRIGGER, 8, false },
      { "GetActorX", PCD_GETACTORX, 1, true },
      { "GetActorY", PCD_GETACTORY, 1, true },
      { "GetActorZ", PCD_GETACTORZ, 1, true },
      { "Sin", PCD_SIN, 1, true },
      { "Cos", PCD_COS, 1, true },
      { "VectorAngle", PCD_VECTORANGLE, 2, true },
      { "CheckWeapon", PCD_CHECKWEAPON, 1, true },
      { "SetWeapon", PCD_SETWEAPON, 1, true },
      { "SetMarineWeapon", PCD_SETMARINEWEAPON, 2, false },
      { "SetActorProperty", PCD_SETACTORPROPERTY, 3, false },
      { "GetActorProperty", PCD_GETACTORPROPERTY, 2, true },
      { "PlayerNumber", PCD_PLAYERNUMBER, 0, true },
      { "ActivatorTID", PCD_ACTIVATORTID, 0, true },
      { "SetMarineSprite", PCD_SETMARINESPRITE, 2, false },
      { "GetScreenWidth", PCD_GETSCREENWIDTH, 0, true },
      { "GetScreenHeight", PCD_GETSCREENHEIGHT, 0, true },
      { "Thing_Projectile2", PCD_THINGPROJECTILE2, 7, false },
      { "StrLen", PCD_STRLEN, 1, true },
      { "SetHudSize", PCD_SETHUDSIZE, 3, false },
      { "GetCVar", PCD_GETCVAR, 1, true },
      { "SetResultValue", PCD_SETRESULTVALUE, 1, false },
      { "GetLineRowOffset", PCD_GETLINEROWOFFSET, 0, true },
      { "GetActorFloorZ", PCD_GETACTORFLOORZ, 1, true },
      { "GetActorAngle", PCD_GETACTORANGLE, 1, true },
      { "GetSectorFloorZ", PCD_GETSECTORFLOORZ, 3, true },
      { "GetSectorCeilingZ", PCD_GETSECTORCEILINGZ, 3, true },
      { "GetSigilPieces", PCD_GETSIGILPIECES, 0, true },
      { "GetLevelInfo", PCD_GETLEVELINFO, 1, true },
      { "ChangeSky", PCD_CHANGESKY, 2, false },
      { "PlayerInGame", PCD_PLAYERINGAME, 1, true },
      { "PlayerIsBot", PCD_PLAYERISBOT, 1, true },
      { "SetCameraToTexture", PCD_SETCAMERATOTEXTURE, 3, false },
      { "GetAmmoCapacity", PCD_GETAMMOCAPACITY, 1, true },
      { "SetAmmoCapacity", PCD_SETAMMOCAPACITY, 2, false },
      { "SetActorAngle", PCD_SETACTORANGLE, 2, false },
      { "SpawnProjectile", PCD_SPAWNPROJECTILE, 7, false },
      { "GetSectorLightLevel", PCD_GETSECTORLIGHTLEVEL, 1, true },
      { "GetActorCeilingZ", PCD_GETACTORCEILINGZ, 1, true },
      { "SetActorPosition", PCD_SETACTORPOSITION, 5, true },
      { "ClearActorInventory", PCD_CLEARACTORINVENTORY, 1, false },
      { "GiveActorInventory", PCD_GIVEACTORINVENTORY, 3, false },
      { "TakeActorInventory", PCD_TAKEACTORINVENTORY, 3, false },
      { "CheckActorInventory", PCD_CHECKACTORINVENTORY, 2, true },
      { "ThingCountName", PCD_THINGCOUNTNAME, 2, true },
      { "SpawnSpotFacing", PCD_SPAWNSPOTFACING, 3, true },
      { "PlayerClass", PCD_PLAYERCLASS, 1, true },
      { "GetPlayerInfo", PCD_GETPLAYERINFO, 2, true },
      { "ChangeLevel", PCD_CHANGELEVEL, 4, false },
      { "SectorDamage", PCD_SECTORDAMAGE, 5, false },
      { "ReplaceTextures", PCD_REPLACETEXTURES, 3, false },
      { "GetActorPitch", PCD_GETACTORPITCH, 1, true },
      { "SetActorPitch", PCD_SETACTORPITCH, 2, false },
      { "SetActorState", PCD_SETACTORSTATE, 3, true },
      { "Thing_Damage2", PCD_THINGDAMAGE2, 3, true },
      { "UseInventory", PCD_USEINVENTORY, 1, true },
      { "UseActorInventory", PCD_USEACTORINVENTORY, 2, true },
      { "CheckActorCeilingTexture", PCD_CHECKACTORCEILINGTEXTURE, 2, true },
      { "CheckActorFloorTexture", PCD_CHECKACTORFLOORTEXTURE, 2, true },
      { "GetActorLightLevel", PCD_GETACTORLIGHTLEVEL, 1, true },
      { "SetMugShotState", PCD_SETMUGSHOTSTATE, 1, false },
      { "ThingCountSector", PCD_THINGCOUNTSECTOR, 3, true },
      { "ThingCountNameSector", PCD_THINGCOUNTNAMESECTOR, 3, true },
      { "CheckPlayerCamera", PCD_CHECKPLAYERCAMERA, 1, true },
      { "MorphActor", PCD_MORPHACTOR, 7, true },
      { "UnMorphActor", PCD_UNMORPHACTOR, 2, true },
      { "GetPlayerInput", PCD_GETPLAYERINPUT, 2, true },
      { "ClassifyActor", PCD_CLASSIFYACTOR, 1, true },
   };
   for ( isize i = 0; i < ARRAY_SIZE( table ); ++i ) {
      if ( table[ i ].opcode == opcode ) {
         return &table[ i ];
      }
   }
   return NULL;
}

/**
 * Returns the number of values currently in the values stack.
 */
isize vm_get_stack_size( struct turn* turn ) {
   return turn->stack - turn->stack_start;
}
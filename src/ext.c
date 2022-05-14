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

enum arg {
   ARG_U8,
   ARG_I16,
   ARG_I32,
};

enum ext_func {
   EXTFUNC_DUMPSCRIPT = 20000,
   EXTFUNC_DUMPLOCALVARS = 20001,
};

static void call_dump_script( struct vm* vm, struct turn* turn, i32 script );
static void show_ext_func( struct vm* vm, struct turn* turn, i32 id, i32* args,
   i32 total_args );
static i32 read_arg( struct vm* vm, struct turn* turn, enum arg arg );
static const char* get_ext_func_name( i32 id );

void vm_run_callfunc( struct vm* vm, struct turn* turn ) {
   i32 num_args = read_arg( vm, turn, ARG_U8 );
   i32 func = read_arg( vm, turn, ARG_I16 );
   switch ( func ) {
   case EXTFUNC_DUMPSCRIPT:
      call_dump_script( vm, turn,
         vm_pop( vm, turn )
      );
      break;
   case EXTFUNC_DUMPLOCALVARS:
      dbg_dump_local_vars( vm, turn );
      vm_push( turn, 1 );
      break;
   default:
      show_ext_func( vm, turn, func, turn->stack - num_args, num_args );
      turn->stack -= num_args;
      vm_push( turn, 0 );
   }
}

static void call_dump_script( struct vm* vm, struct turn* turn, i32 number ) {
   struct script* script = vm_find_script_by_number( vm, number );
   if ( script != null ) {
      dbg_dump_script( vm, script );
      vm_push( turn, 1 );
   }
   else {
      v_diag( vm, DIAG_DBG,
         "script %d not found", number );
      vm_push( turn, 0 );
   }
}

static void show_ext_func( struct vm* vm, struct turn* turn, i32 id, i32* args,
   i32 total_args ) {
   v_diag( vm, DIAG_MULTI_PART, "-%d:", id );

   const char* special_name = get_ext_func_name( id );
   if ( special_name != NULL ) {
      v_diag_more( vm, "%s", special_name );
   }
   else {
      v_diag_more( vm, "?" );
   }

   v_diag_more( vm, "(" );
   if ( total_args > 0 ) {
      v_diag_more( vm, " " );
      for ( i32 i = 0; i < total_args; ++i ) {
         v_diag_more( vm, "%d", args[ i ] );
         if ( i + 1 < total_args ) {
            v_diag_more( vm, ", " );
         }
      }
      v_diag_more( vm, " " );
   }
   v_diag_more( vm, ")\n" );

   /*
   v_diag( vm, DIAG_DBG | DIAG_WARN | DIAG_MULTI_PART,
      "ignoring -%d:ExtensionFunction(", id );
   if ( total_args > 0 ) {
      v_diag_more( vm, " " ); 
      for ( i32 i = 0; i < total_args; ++i ) {
         v_diag_more( vm, "%d", args[ i ] );
         if ( i + 1 < total_args ) {
            v_diag_more( vm, ", " );
         }
      }
      v_diag_more( vm, " " ); 
   }
   v_diag_more( vm, ")\n" );
   */
}

static i32 read_arg( struct vm* vm, struct turn* turn, enum arg arg ) {
   if ( ! turn->module->object.small_code ) {
      arg = ARG_I32;
   }
   switch ( arg ) {
   case ARG_U8:
      {
         u8 arg = turn->ip[ 0 ];
         ++turn->ip;
         return arg;
      }
      break;
   case ARG_I16:
      {
         i16 arg;
         memcpy( &arg, turn->ip, sizeof( arg ) );
         turn->ip += sizeof( arg );
         return arg;
      }
      break;
   case ARG_I32:
      {
         i32 arg;
         memcpy( &arg, turn->ip, sizeof( arg ) );
         turn->ip += sizeof( arg );
         return arg;
      }
      break;
   default:
      UNIMPLEMENTED;
      v_bail( vm );
      return 0;
   }
}

static const char* get_ext_func_name( i32 id ) {
   switch ( id ) {
   case 1: return "GetLineUDMFInt";
   case 2: return "GetLineUDMFFixed";
   case 3: return "GetThingUDMFInt";
   case 4: return "GetThingUDMFFixed";
   case 5: return "GetSectorUDMFInt";
   case 6: return "GetSectorUDMFFixed";
   case 7: return "GetSideUDMFInt";
   case 8: return "GetSideUDMFFixed";
   case 9: return "GetActorVelX";
   case 10: return "GetActorVelY";
   case 11: return "GetActorVelZ";
   case 12: return "SetActivator";
   case 13: return "SetActivatorToTarget";
   case 14: return "GetActorViewHeight";
   case 15: return "GetChar";
   case 16: return "GetAirSupply";
   case 17: return "SetAirSupply";
   case 18: return "SetSkyScrollSpeed";
   case 19: return "GetArmorType";
   case 20: return "SpawnSpotForced";
   case 21: return "SpawnSpotFacingForced";
   case 22: return "CheckActorProperty";
   case 23: return "SetActorVelocity";
   case 24: return "SetUserVariable";
   case 25: return "GetUserVariable";
   case 26: return "Radius_Quake2";
   case 27: return "CheckActorClass";
   case 28: return "SetUserArray";
   case 29: return "GetUserArray";
   case 30: return "SoundSequenceOnActor";
   case 31: return "SoundSequenceOnSector";
   case 32: return "SoundSequenceOnPolyobj";
   case 33: return "GetPolyobjX";
   case 34: return "GetPolyobjY";
   case 35: return "CheckSight";
   case 36: return "SpawnForced";
   case 37: return "AnnouncerSound";
   case 38: return "SetPointer";
   case 39: return "ACS_NamedExecute";
   case 40: return "ACS_NamedSuspend";
   case 41: return "ACS_NamedTerminate";
   case 42: return "ACS_NamedLockedExecute";
   case 43: return "ACS_NamedLockedExecuteDoor";
   case 44: return "ACS_NamedExecuteWithResult";
   case 45: return "ACS_NamedExecuteAlways";
   case 46: return "UniqueTID";
   case 47: return "IsTIDUsed";
   case 48: return "Sqrt";
   case 49: return "FixedSqrt";
   case 50: return "VectorLength";
   case 51: return "SetHUDClipRect";
   case 52: return "SetHUDWrapWidth";
   case 53: return "SetCVar";
   case 54: return "GetUserCVar";
   case 55: return "SetUserCVar";
   case 56: return "GetCVarString";
   case 57: return "SetCVarString";
   case 58: return "GetUserCVarString";
   case 59: return "SetUserCVarString";
   case 60: return "LineAttack";
   case 61: return "PlaySound";
   case 62: return "StopSound";
   case 63: return "strcmp";
   case 64: return "stricmp";
   case 65: return "StrLeft";
   case 66: return "StrRight";
   case 67: return "StrMid";
   case 68: return "GetActorClass";
   case 69: return "GetWeapon";
   case 70: return "SoundVolume";
   case 71: return "PlayActorSound";
   case 72: return "SpawnDecal";
   case 73: return "CheckFont";
   case 74: return "DropItem";
   case 75: return "CheckFlag";
   case 76: return "SetLineActivation";
   case 77: return "GetLineActivation";
   case 78: return "GetActorPowerupTics";
   case 79: return "ChangeActorAngle";
   case 80: return "ChangeActorPitch";
   case 81: return "GetArmorInfo";
   case 82: return "DropInventory";
   case 83: return "PickActor";
   case 84: return "IsPointerEqual";
   case 85: return "CanRaiseActor";
   case 86: return "SetActorTeleFog";
   case 87: return "SwapActorTeleFog";
   case 88: return "SetActorRoll";
   case 89: return "ChangeActorRoll";
   case 90: return "GetActorRoll";
   case 91: return "QuakeEx";
   case 92: return "Warp";
   case 93: return "GetMaxInventory";
   case 94: return "SetSectorDamage";
   case 95: return "SetSectorTerrain";
   case 96: return "SpawnParticle";
   case 97: return "SetMusicVolume";
   case 98: return "CheckProximity";
   case 99: return "CheckActorState";

   // Zandronum
   case 100: return "ResetMap";
   case 101: return "PlayerIsSpectator";
   case 102: return "ConsolePlayerNumber";
   case 103: return "GetTeamProperty";
   case 104: return "GetPlayerLivesLeft";
   case 105: return "SetPlayerLivesLeft";
   case 106: return "KickFromGame";
   case 107: return "GetGamemodeState";
   case 108: return "SetDBEntry";
   case 109: return "GetDBEntry";
   case 110: return "SetDBEntryString";
   case 111: return "GetDBEntryString";
   case 112: return "IncrementDBEntry";
   case 113: return "PlayerIsLoggedIn";
   case 114: return "GetPlayerAccountName";
   case 115: return "SortDBEntries";
   case 116: return "CountDBResults";
   case 117: return "FreeDBResults";
   case 118: return "GetDBResultKeyString";
   case 119: return "GetDBResultValueString";
   case 120: return "GetDBResultValue";
   case 121: return "GetDBEntryRank";
   case 122: return "RequestScriptPuke";
   case 123: return "BeginDBTransaction";
   case 124: return "EndDBTransaction";
   case 125: return "GetDBEntries";

   case 200: return "CheckClass";
   case 201: return "DamageActor";
   case 202: return "SetActorFlag";
   case 203: return "SetTranslation";
   case 204: return "GetActorFloorTexture";
   case 205: return "GetActorFloorTerrain";
   case 206: return "StrArg";
   case 207: return "Floor";
   case 208: return "Round";
   case 209: return "Ceil";
   case 210: return "ScriptCall";
   case 211: return "StartSlideshow";

   // Eternity
   case 300: return "GetLineX";
   case 301: return "GetLineY";

   // GZDoom
   case 400: return "SetSectorGlow";
   case 401: return "SetFogDensity";

   // ZDaemon
   case 19620: return "GetTeamScore";
   case 19621: return "SetTeamScore";

   default:
      return NULL;
   }
}
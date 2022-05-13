#include <stdio.h>
#include <string.h>
#include <setjmp.h>

#include "common/misc.h"
#include "common/mem.h"
#include "common/str.h"
#include "common/list.h"
#include "vm.h"
#include "pcode.h"

static i32 read_lspec_id( struct turn* turn );
static bool execute_line_special( struct vm* vm, i32 special,
   bool push_return_value, i32 arg1, i32 arg2, i32 arg3, i32 arg4, i32 arg5 );
static void execute_acs_execute( struct vm* vm, i32 script_number, i32 map,
   i32 arg1 );
static void show_line_special( struct vm* vm, i32 id, i32* args,
   i32 total_args );
static const char* get_special_name( i32 id );

void vm_run_lspec( struct vm* vm, struct turn* turn ) {
   i32 id = read_lspec_id( turn );

   bool direct_opcode = false;
   bool byte_args = false;
   switch ( turn->opcode ) {
   case PCD_LSPEC1DIRECT:
   case PCD_LSPEC2DIRECT:
   case PCD_LSPEC3DIRECT:
   case PCD_LSPEC4DIRECT:
   case PCD_LSPEC5DIRECT:
      direct_opcode = true;
      break;
   case PCD_LSPEC1DIRECTB:
   case PCD_LSPEC2DIRECTB:
   case PCD_LSPEC3DIRECTB:
   case PCD_LSPEC4DIRECTB:
   case PCD_LSPEC5DIRECTB:
      direct_opcode = true;
      byte_args = true;
      break;
   default:
      break;
   }

   i32 total_args = 1;
   switch ( turn->opcode ) {
   case PCD_LSPEC1:
   case PCD_LSPEC1DIRECT:
   case PCD_LSPEC1DIRECTB:
      break;
   case PCD_LSPEC2:
   case PCD_LSPEC2DIRECT:
   case PCD_LSPEC2DIRECTB:
      total_args = 2;
      break;
   case PCD_LSPEC3:
   case PCD_LSPEC3DIRECT:
   case PCD_LSPEC3DIRECTB:
      total_args = 3;
      break;
   case PCD_LSPEC4:
   case PCD_LSPEC4DIRECT:
   case PCD_LSPEC4DIRECTB:
      total_args = 4;
      break;
   case PCD_LSPEC5:
   case PCD_LSPEC5DIRECT:
   case PCD_LSPEC5DIRECTB:
   case PCD_LSPEC5RESULT:
   case PCD_LSPEC5EX:
   case PCD_LSPEC5EXRESULT:
      total_args = 5;
      break;
   default:
      UNIMPLEMENTED;
   }

   bool push_return_value = false;
   switch ( turn->opcode ) {
   case PCD_LSPEC5RESULT:
   case PCD_LSPEC5EXRESULT:
      push_return_value = true;
      break;
   default:
      break;
   }

   i32 args[ 5 ];
   for ( i32 i = 0; i < ARRAY_SIZE( args ); ++i ) {
      if ( i < total_args ) {
         if ( direct_opcode ) {
            if ( byte_args ) {
               args[ i ] = turn->ip[ 0 ];
               ++turn->ip;
            }
            else {
               memcpy( &args[ i ], turn->ip, sizeof( args[ i ] ) );
               turn->ip += sizeof( args[ i ] );
            }
         }
         else {
            args[ total_args - i - 1 ] = vm_pop( vm, turn );
         }
      }
      else {
         args[ i ] = 0;
      }
   }

   bool executed = execute_line_special( vm, id, push_return_value, args[ 0 ],
      args[ 1 ], args[ 2 ], args[ 3 ], args[ 4 ] );
   if ( ! executed ) {
      show_line_special( vm, id, args, total_args );
      if ( push_return_value ) {
         vm_push( turn, 0 );
      }
   }
}

static i32 read_lspec_id( struct turn* turn ) {
   bool small_code = turn->module->object.small_code;
   switch ( turn->opcode ) {
   case PCD_LSPEC5EX:
   case PCD_LSPEC5EXRESULT:
      small_code = false;
      break;
   default:
      break;
   }
   if ( small_code ) {
      //expect_pcode_data( viewer, segment, sizeof( *segment->data ) );
      i32 id = turn->ip[ 0 ];
      ++turn->ip;
      return id;
   }
   else {
      //expect_pcode_data( viewer, segment, sizeof( id ) );
      i32 id = 0;
      memcpy( &id, turn->ip, sizeof( id ) );
      turn->ip += sizeof( id );
      return id;
   }
}

static bool execute_line_special( struct vm* vm, i32 special,
   bool push_return_value, i32 arg1, i32 arg2, i32 arg3, i32 arg4, i32 arg5 ) {
   switch ( special ) {
   case LSPEC_ACSEXECUTE:
      execute_acs_execute( vm, arg1, arg2, arg3 );
      break;
   default:
      return false;
   }
   return true;
}

static void execute_acs_execute( struct vm* vm, i32 script_number, i32 map,
   i32 arg1 ) {
   // Resume a suspended script.
   struct script* script = vm_remove_suspended_script( vm, script_number );
   if ( script != NULL ) {
//      enq_script( vm, script );
      return;
   }
}

static void show_line_special( struct vm* vm, i32 id, i32* args,
   i32 total_args ) {
   v_diag( vm, DIAG_MULTI_PART, "%d:", id );

   const char* special_name = get_special_name( id );
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
      "ignoring LineSpecial:%d( ", id );
   for ( i32 i = 0; i < total_args; ++i ) {
      v_diag_more( vm, "%d", args[ i ] );
      if ( i + 1 < total_args ) {
         v_diag_more( vm, ", " );
      }
   }
   v_diag_more( vm, " )\n" );
   */
}

static const char* get_special_name( i32 id ) {
   switch ( id ) {
   case 1: return "Polyobj_StartLine";
   case 2: return "Polyobj_RotateLeft";
   case 3: return "Polyobj_RotateRight";
   case 4: return "Polyobj_Move";
   case 5: return "Polyobj_ExplicitLine";
   case 6: return "Polyobj_MoveTimes8";
   case 7: return "Polyobj_DoorSwing";
   case 8: return "Polyobj_DoorSlide";
   case 9: return "Line_Horizon";
   case 10: return "Door_Close";
   case 11: return "Door_Open";
   case 12: return "Door_Raise";
   case 13: return "Door_LockedRaise";
   case 14: return "Door_Animated";
   case 15: return "Autosave";
   case 16: return "Transfer_WallLight";
   case 17: return "Thing_Raise";
   case 18: return "StartConversation";
   case 19: return "Thing_Stop";
   case 20: return "Floor_LowerByValue";
   case 21: return "Floor_LowerToLowest";
   case 22: return "Floor_LowerToNearest";
   case 23: return "Floor_RaiseByValue";
   case 24: return "Floor_RaiseToHighest";
   case 25: return "Floor_RaiseToNearest";
   case 26: return "Stairs_BuildDown";
   case 27: return "Stairs_BuildUp";
   case 28: return "Floor_RaiseAndCrush";
   case 29: return "Pillar_Build";
   case 30: return "Pillar_Open";
   case 31: return "Stairs_BuildDownSync";
   case 32: return "Stairs_BuildUpSync";
   case 33: return "ForceField";
   case 34: return "ClearForceField";
   case 35: return "Floor_RaiseByValueTimes8";
   case 36: return "Floor_LowerByValueTimes8";
   case 37: return "Floor_MoveToValue";
   case 38: return "Ceiling_Waggle";
   case 39: return "Teleport_ZombieChanger";
   case 40: return "Ceiling_LowerByValue";
   case 41: return "Ceiling_RaiseByValue";
   case 42: return "Ceiling_CrushAndRaise";
   case 43: return "Ceiling_LowerAndCrush";
   case 44: return "Ceiling_CrushStop";
   case 45: return "Ceiling_CrushRaiseAndStay";
   case 46: return "Floor_CrushStop";
   case 47: return "Ceiling_MoveToValue";
   case 48: return "Sector_Attach3dMidTex";
   case 49: return "GlassBreak";
   case 50: return "ExtraFloor_LightOnly";
   case 51: return "Sector_SetLink";
   case 52: return "Scroll_Wall";
   case 53: return "Line_SetTextureOffset";
   case 54: return "Sector_ChangeFlags";
   case 55: return "Line_SetBlocking";
   case 56: return "Line_SetTextureScale";
   case 57: return "Sector_SetPortal";
   case 58: return "Sector_CopyScroller";
   case 59: return "Polyobj_Or_MoveToSpot";
   case 60: return "Plat_PerpetualRaise";
   case 61: return "Plat_Stop";
   case 62: return "Plat_DownWaitUpStay";
   case 63: return "Plat_DownByValue";
   case 64: return "Plat_UpWaitDownStay";
   case 65: return "Plat_UpByValue";
   case 66: return "Floor_LowerInstant";
   case 67: return "Floor_RaiseInstant";
   case 68: return "Floor_MoveToValueTimes8";
   case 69: return "Ceiling_MoveToValueTimes8";
   case 70: return "Teleport";
   case 71: return "Teleport_NoFog";
   case 72: return "ThrustThing";
   case 73: return "DamageThing";
   case 74: return "Teleport_NewMap";
   case 75: return "Teleport_EndGame";
   case 76: return "TeleportOther";
   case 77: return "TeleportGroup";
   case 78: return "TeleportInSector";
   case 79: return "Thing_SetConversation";
   case 80: return "Acs_Execute";
   case 81: return "Acs_Suspend";
   case 82: return "Acs_Terminate";
   case 83: return "Acs_LockedExecute";
   case 84: return "Acs_ExecuteWithResult";
   case 85: return "Acs_LockedExecuteDoor";
   case 86: return "Polyobj_MoveToSpot";
   case 87: return "Polyobj_Stop";
   case 88: return "Polyobj_MoveTo";
   case 89: return "Polyobj_Or_MoveTo";
   case 90: return "Polyobj_Or_RotateLeft";
   case 91: return "Polyobj_Or_RotateRight";
   case 92: return "Polyobj_Or_Move";
   case 93: return "Polyobj_Or_MoveTimes8";
   case 94: return "Pillar_BuildAndCrush";
   case 95: return "FloorAndCeiling_LowerByValue";
   case 96: return "FloorAndCeiling_RaiseByValue";
   case 97: return "Ceiling_LowerAndCrushDist";
   case 98: return "Sector_SetTranslucent";
   case 99: return "Floor_RaiseAndCrushDoom";
   case 100: return "Scroll_Texture_Left";
   case 101: return "Scroll_Texture_Right";
   case 102: return "Scroll_Texture_Up";
   case 103: return "Scroll_Texture_Down";
   case 104: return "Ceiling_CrushAndRaiseSilentDist";
   case 105: return "Door_WaitRaise";
   case 106: return "Door_WaitClose";
   case 107: return "Line_SetPortalTarget";
   case 109: return "Light_ForceLightning";
   case 110: return "Light_RaiseByValue";
   case 111: return "Light_LowerByValue";
   case 112: return "Light_ChangeToValue";
   case 113: return "Light_Fade";
   case 114: return "Light_Glow";
   case 115: return "Light_Flicker";
   case 116: return "Light_Strobe";
   case 117: return "Light_Stop";
   case 118: return "Plane_Copy";
   case 119: return "Thing_Damage";
   case 120: return "Radius_Quake";
   case 121: return "Line_SetIdentification";
   case 125: return "Thing_Move";
   case 127: return "Thing_SetSpecial";
   case 128: return "ThrustThingZ";
   case 129: return "UsePuzzleItem";
   case 130: return "Thing_Activate";
   case 131: return "Thing_Deactivate";
   case 132: return "Thing_Remove";
   case 133: return "Thing_Destroy";
   case 134: return "Thing_Projectile";
   case 135: return "Thing_Spawn";
   case 136: return "Thing_ProjectileGravity";
   case 137: return "Thing_SpawnNoFog";
   case 138: return "Floor_Waggle";
   case 139: return "Thing_SpawnFacing";
   case 140: return "Sector_ChangeSound";
   case 145: return "Player_SetTeam";
   case 152: return "Team_Score";
   case 153: return "Team_GivePoints";
   case 154: return "Teleport_NoStop";
   case 157: return "SetGlobalFogParameter";
   case 158: return "Fs_Execute";
   case 159: return "Sector_SetPlaneReflection";
   case 160: return "Sector_Set3dFloor";
   case 161: return "Sector_SetContents";
   case 168: return "Ceiling_CrushAndRaiseDist";
   case 169: return "Generic_Crusher2";
   case 170: return "Sector_SetCeilingScale2";
   case 171: return "Sector_SetFloorScale2";
   case 172: return "Plat_UpNearestWaitDownStay";
   case 173: return "NoiseAlert";
   case 174: return "SendToCommunicator";
   case 175: return "Thing_ProjectileIntercept";
   case 176: return "Thing_ChangeTid";
   case 177: return "Thing_Hate";
   case 178: return "Thing_ProjectileAimed";
   case 179: return "ChangeSkill";
   case 180: return "Thing_SetTranslation";
   case 181: return "Plane_Align";
   case 182: return "Line_Mirror";
   case 183: return "Line_AlignCeiling";
   case 184: return "Line_AlignFloor";
   case 185: return "Sector_SetRotation";
   case 186: return "Sector_SetCeilingPanning";
   case 187: return "Sector_SetFloorPanning";
   case 188: return "Sector_SetCeilingScale";
   case 189: return "Sector_SetFloorScale";
   case 190: return "Static_Init";
   case 191: return "SetPlayerProperty";
   case 192: return "Ceiling_LowerToHighestFloor";
   case 193: return "Ceiling_LowerInstant";
   case 194: return "Ceiling_RaiseInstant";
   case 195: return "Ceiling_CrushRaiseAndStayA";
   case 196: return "Ceiling_CrushAndRaiseA";
   case 197: return "Ceiling_CrushAndRaiseSilentA";
   case 198: return "Ceiling_RaiseByValueTimes8";
   case 199: return "Ceiling_LowerByValueTimes8";
   case 200: return "Generic_Floor";
   case 201: return "Generic_Ceiling";
   case 202: return "Generic_Door";
   case 203: return "Generic_Lift";
   case 204: return "Generic_Stairs";
   case 205: return "Generic_Crusher";
   case 206: return "Plat_DownWaitUpStayLip";
   case 207: return "Plat_PerpetualRaiseLip";
   case 208: return "TranslucentLine";
   case 209: return "Transfer_Heights";
   case 210: return "Transfer_FloorLight";
   case 211: return "Transfer_CeilingLight";
   case 212: return "Sector_SetColor";
   case 213: return "Sector_SetFade";
   case 214: return "Sector_SetDamage";
   case 215: return "Teleport_Line";
   case 216: return "Sector_SetGravity";
   case 217: return "Stairs_BuildUpDoom";
   case 218: return "Sector_SetWind";
   case 219: return "Sector_SetFriction";
   case 220: return "Sector_SetCurrent";
   case 221: return "Scroll_Texture_Both";
   case 222: return "Scroll_Texture_Model";
   case 223: return "Scroll_Floor";
   case 224: return "Scroll_Ceiling";
   case 225: return "Scroll_Texture_Offsets";
   case 226: return "Acs_ExecuteAlways";
   case 227: return "PointPush_SetForce";
   case 228: return "Plat_RaiseAndStayTx0";
   case 229: return "Thing_SetGoal";
   case 230: return "Plat_UpByValueStayTx";
   case 231: return "Plat_ToggleCeiling";
   case 232: return "Light_StrobeDoom";
   case 233: return "Light_MinNeighbor";
   case 234: return "Light_MaxNeighbor";
   case 235: return "Floor_TransferTrigger";
   case 236: return "Floor_TransferNumeric";
   case 237: return "ChangeCamera";
   case 238: return "Floor_RaiseToLowestCeiling";
   case 239: return "Floor_RaiseByValueTxTy";
   case 240: return "Floor_RaiseByTexture";
   case 241: return "Floor_LowerToLowestTxTy";
   case 242: return "Floor_LowerToHighest";
   case 243: return "Exit_Normal";
   case 244: return "Exit_Secret";
   case 245: return "Elevator_RaiseToNearest";
   case 246: return "Elevator_MoveToFloor";
   case 247: return "Elevator_LowerToNearest";
   case 248: return "HealThing";
   case 249: return "Door_CloseWaitOpen";
   case 250: return "Floor_Donut";
   case 251: return "FloorAndCeiling_LowerRaise";
   case 252: return "Ceiling_RaiseToNearest";
   case 253: return "Ceiling_LowerToLowest";
   case 254: return "Ceiling_LowerToFloor";
   case 255: return "Ceiling_CrushRaiseAndStaySilA";
   case 256: return "Floor_LowerToHighestEE";
   case 257: return "Floor_RaiseToLowest";
   case 258: return "Floor_LowerToLowestCeiling";
   case 259: return "Floor_RaiseToCeiling";
   case 260: return "Floor_ToCeilingInstant";
   case 261: return "Floor_LowerByTexture";
   case 262: return "Ceiling_RaiseToHighest";
   case 263: return "Ceiling_ToHighestInstant";
   case 264: return "Ceiling_LowerToNearest";
   case 265: return "Ceiling_RaiseToLowest";
   case 266: return "Ceiling_RaiseToHighestFloor";
   case 267: return "Ceiling_ToFloorInstant";
   case 268: return "Ceiling_RaiseByTexture";
   case 269: return "Ceiling_LowerByTexture";
   case 270: return "Stairs_BuildDownDoom";
   case 271: return "Stairs_BuildUpDoomSync";
   case 272: return "Stairs_BuildDownDoomSync";
   case 273: return "Stairs_BuildUpDoomCrush";
   case 274: return "Door_AnimatedClose";
   case 275: return "Floor_Stop";
   case 276: return "Ceiling_Stop";
   case 277: return "Sector_SetFloorGlow";
   case 278: return "Sector_SetCeilingGlow";
   case 279: return "Floor_MoveToValueAndCrush";
   case 280: return "Ceiling_MoveToValueAndCrush";
   }
   static const char* table[] = {
      "Polyobj_StartLine",
      "Polyobj_RotateLeft",
   };
   if ( id >= 0 && id < ARRAY_SIZE( table ) ) {
      return table[ id ];
   }
   return NULL;
}
#ifndef SRC_VM_H
#define SRC_VM_H

#include <stdbool.h>

#include "common/vector.h"

enum { MAX_MAP_VARS = 128 };
enum { MAX_WORLD_VARS = 256 };
enum { MAX_GLOBAL_VARS = 64 };

struct module_arg {
   const char* name;
   const char* path;
};

struct options {
   const char* object_file;
   struct list libraries; // Contains paths to library files.
   struct list modules;   // Contains module_args.
   bool verbose;
};

struct file_request {
   enum {
      FILEREQUESTERR_NONE,
      FILEREQUESTERR_OPEN,
      FILEREQUESTERR_READ,
   } err;
   u8* data;
   usize size;
};

struct indexed_string {
   struct str* value;
   isize ref_count;
};

struct str_pool {
   struct indexed_string* strings;
   isize total;
};

// For deallocating strings, we need to know whether a value is a string or
// not. To do that, we represent a value as a "slot." A slot contains a value
// and the type of the value. Anything that can contain values, of which might
// be strings, must be made a slot. That means, variables, array elements, and
// stack values are all slots.
struct slot {
   i32 value;
   bool is_string;
};

struct var {
   const char* name;
   i32* elements;
   i32 value;
   i32 size;
   bool array;
   bool imported;
};

/**
 * This structure describes the local arrays found in a script or a function. 
 */
struct script_array {
   i32 start; // Start of the array data.
   i32 size;
};

enum script_flag {
   FLAG_NET = 0x1,
   FLAG_CLIENTSIDE = 0x2,
};

struct script {
   const char* name;
   i32 number;
   enum {
      SCRIPTTYPE_UNKNOWN = -1,
      SCRIPTTYPE_CLOSED,
      SCRIPTTYPE_OPEN,
      SCRIPTTYPE_RESPAWN,
      SCRIPTTYPE_DEATH,
      SCRIPTTYPE_ENTER,
      SCRIPTTYPE_PICKUP,
      SCRIPTTYPE_BLUERETURN,
      SCRIPTTYPE_REDRETURN,
      SCRIPTTYPE_WHITERETURN,
      SCRIPTTYPE_LIGHTNING = 12,
      SCRIPTTYPE_UNLOADING,
      SCRIPTTYPE_DISCONNECT,
      SCRIPTTYPE_RETURN,
      SCRIPTTYPE_EVENT,
      SCRIPTTYPE_KILL,
      SCRIPTTYPE_REOPEN,
   } type;
   u32 flags;
   i32 start; // Beginning of a script's code.
   struct script_array* arrays;
   i32 num_vars;
   i32 num_arrays;
   isize total_array_size;
};

// An instance of a running script.
struct instance {
   struct script* script;
   struct instance* next;
   struct instance* next_waiting;
   struct instance* waiting;
   struct instance* waiting_tail;
   i32* vars;
   i32* arrays; // Array data.
   i32 delay_amount;
   enum {
      SCRIPTSTATE_TERMINATED,
      SCRIPTSTATE_RUNNING,
      SCRIPTSTATE_SUSPENDED,
      SCRIPTSTATE_DELAYED,
      SCRIPTSTATE_WAITING,
   } state;
   isize resume_time;
   isize ip; // Position of the instruction pointer for the script.
};

struct func {
   struct module* module;
   const char* name;
   i32 params; // Number of parameters.
   i32 local_size;
   i32 start; // Offset to start of function code.
   struct script_array* arrays;
   i32 num_arrays;
   isize total_array_size;
   bool imported;
};

struct func_table {
   struct func* entries;
   struct func** linked_entries;
   i32 size;
};

struct call {
   struct call* prev;
   struct func* func; // The function that is called.
   struct module* return_module;
   const u8* return_addr;
   i32* locals; // Scalar variable data.
   i32* arrays; // Array data.
   bool discard_return_value;
};

struct object {
   const u8* data;
   int size;
   enum {
      FORMAT_UNKNOWN,
      FORMAT_ZERO,
      FORMAT_BIG_E,
      FORMAT_LITTLE_E,
   } format;
   int chunk_offset;
   int chunk_end;
   int dummy_offset;
   bool indirect_format;
   bool small_code;
   struct module* module;
};

struct import {
   // Name of the imported module. 
   const char* module_name;
   // The imported module.
   struct module* module;
};

// The maximum module name length is the same as the maximum length of a lump
// name in a WAD file.
enum { MODULE_NAME_MAX_LENGTH = 8 };

struct module {
   char name[ MODULE_NAME_MAX_LENGTH + 1 ]; // Plus one for NUL character.
   struct object object;
   struct list imports;
   struct list scripts;
   struct list strings;
   struct list waiting_scripts; // Queue of scripts waiting to run.
   // Map scalar variables and arrays share the same namespace.
   struct var vars[ MAX_MAP_VARS ];
   struct var* map_vars[ MAX_MAP_VARS ];
   struct func_table func_table;
};

struct turn {
   struct module* module;
   struct instance* script;
   i32 opcode;
   const u8* ip; // Instruction pointer.
   bool finished;
   i32* stack_start;
   i32* stack;
};

struct vm {
   struct options* options;
   jmp_buf* bail;
   //struct object* object;
   struct list modules;
   struct list scripts;
   struct list waiting_scripts;
   struct list suspended_scripts;
   struct str msg;
   i32 world_vars[ MAX_WORLD_VARS ];
   i32 global_vars[ MAX_GLOBAL_VARS ];
   struct vector world_arrays[ MAX_WORLD_VARS ];
   struct vector global_arrays[ MAX_GLOBAL_VARS ];
   isize tics;
   isize num_active_scripts;
   struct call* call_stack;
   struct str temp_str;
   // Master string table. All the strings from every module are referenced by
   // this table. This table also contains dynamically generated strings.
   struct indexed_string* str_table;
   struct vector strings;
};

#define DIAG_NONE 0x0
#define DIAG_WARN 0x1
#define DIAG_ERR 0x2
#define DIAG_FATALERR 0x4
#define DIAG_DBG 0x8
#define DIAG_MULTI_PART 0x10
#define DIAG_INTERNAL 0x20

#define UNIMPLEMENTED \
   v_diag( vm, DIAG_FATALERR, \
      "instruction opcode %d not implemented", turn->opcode ); \
   v_bail( vm );

void vm_run( struct options* options );
void vm_load_modules( struct vm* vm );
void vm_init_file_request( struct file_request* request );
void vm_load_file( struct file_request* request, const char* path );
void vm_init_object( struct object* object, const u8* data, int size );
void vm_run_instruction( struct vm* vm, struct turn* turn );
struct instance* vm_get_active_script( struct vm* vm, int number );
struct script* vm_remove_suspended_script( struct vm* vm, i32 script_number );
struct func* vm_find_func( struct vm* vm, struct module* module, i32 index );
i32* vm_alloc_local_array_space( isize count );
void v_diag( struct vm* machine, int flags, ... );
void v_diag_more( struct vm* machine, ... );
void v_bail( struct vm* machine );
isize vm_get_stack_size( struct turn* turn );
i32* vm_get_map_var( struct vm* vm, struct module* module, i32 index );
struct script* vm_find_script_by_number( struct vm* vm, i32 number );
const char* vm_present_script( struct vm* vm, struct script* script );
void vm_run_lspec( struct vm* vm, struct turn* turn );
void vm_push( struct turn* turn, i32 value );
i32 vm_pop( struct vm* vm, struct turn* turn );
void vm_run_callfunc( struct vm* vm, struct turn* turn );

#endif

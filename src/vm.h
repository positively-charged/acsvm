#ifndef SRC_VM_H
#define SRC_VM_H

#include <stdbool.h>

#include "common/vector.h"

enum { MAX_MAP_VARS = 128 };
enum { MAX_WORLD_VARS = 256 };
enum { MAX_GLOBAL_VARS = 64 };

struct options {
   const char* object_file;
   struct list libraries; // Contains paths to library files.
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

struct var {
   int* elements;
   int size;
   bool array;
};

struct script {
   struct script* next;
   struct script* next_waiting;
   struct script* waiting;
   struct script* waiting_tail;
   int vars[ 20 ];
   int number;
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
   int offset;
   int delay_amount;
   enum {
      SCRIPTSTATE_TERMINATED,
      SCRIPTSTATE_RUNNING,
      SCRIPTSTATE_SUSPENDED,
      SCRIPTSTATE_DELAYED,
      SCRIPTSTATE_WAITING,
   } state;
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
};

struct vm {
   jmp_buf* bail;
   struct object* object;
   struct list scripts;
   struct list waiting_scripts;
   struct list suspended_scripts;
   struct list strings;
   struct var* arrays;
   struct str msg;
   i32 vars[ MAX_MAP_VARS ];
   i32 world_vars[ MAX_WORLD_VARS ];
   i32 global_vars[ MAX_GLOBAL_VARS ];
   struct vector world_arrays[ MAX_WORLD_VARS ];
};

#define DIAG_NONE 0x0
#define DIAG_WARN 0x1
#define DIAG_ERR 0x2
#define DIAG_FATALERR 0x4

void vm_run( struct options* options );
void vm_init_file_request( struct file_request* request );
void vm_load_file( struct file_request* request, const char* path );
void vm_init_object( struct object* object, const u8* data, int size );
void vm_list_chunks( struct vm* vm, struct object* object );
void v_diag( struct vm* machine, int flags, ... );
void v_bail( struct vm* machine );

#endif

#ifndef SRC_DEBUG_H
#define SRC_DEBUG_H

/**
 * Functions for debugging purposes.
 */

void dgb_show_waiting_scripts( struct vm* vm );
void dbg_show_options( struct options* options );
void dbg_dump_stack( struct vm* vm, struct turn* turn );
void dbg_dump_funcs( struct vm* vm, struct turn* turn );
void dbg_dump_func_table( struct vm* vm, struct func_table* table );
void dbg_dump_script_arrays( struct vm* vm, struct script_array* arrays,
   i32 total );
void dbg_dump_scripts( struct vm* vm );
void dbg_dump_script( struct vm* vm, struct script* script );
void dbg_dump_local_vars( struct vm* vm, struct turn* turn );
void dbg_dump_module_strings( struct vm* vm, struct module* module );

#endif

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

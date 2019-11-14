#include <stdio.h>

#include "common.h"
#include "common/list.h"

// NOTE: The functions below may be violating the strict-aliasing rule.
// ==========================================================================

// Linked list of current allocations. The head is the most recent allocation.
// This way, a short-term allocation can be found and removed quicker.
static struct alloc {
   struct alloc* next;
}* g_alloc = NULL;
// Allocation sizes for bulk allocation.
static struct {
   size_t size;
   size_t quantity;
} g_bulk_sizes[] = {
   { sizeof( struct list_link ), 256 },
};
// Bulk allocations.
static struct {
   struct {
      size_t size;
      size_t quantity;
      size_t left;
      char* block;
      struct free_block {
         struct free_block* next;
      }* free_block;
   } slots[ ARRAY_SIZE( g_bulk_sizes ) ];
   size_t slots_used;
} g_bulk;

static void unlink_alloc( struct alloc* );

void mem_init( void ) {
   g_bulk.slots_used = 0;
   size_t i = 0;
   while ( i < ARRAY_SIZE( g_bulk_sizes ) ) {
      // Find slot with specified allocation size.
      size_t k = 0;
      while ( k < g_bulk.slots_used &&
         g_bulk.slots[ k ].size != g_bulk_sizes[ i ].size ) {
         ++k;
      }
      // If slot doesn't exist, allocate one.
      if ( k == g_bulk.slots_used ) {
         g_bulk.slots[ k ].size = g_bulk_sizes[ i ].size;
         g_bulk.slots[ k ].quantity = g_bulk_sizes[ i ].quantity;
         g_bulk.slots[ k ].left = 0;
         g_bulk.slots[ k ].block = NULL;
         g_bulk.slots[ k ].free_block = NULL;
         ++g_bulk.slots_used;
      }
      else {
         // On duplicate allocation size, use higher quantity.
         if ( g_bulk.slots[ k ].quantity < g_bulk_sizes[ i ].quantity ) {
            g_bulk.slots[ k ].quantity = g_bulk_sizes[ i ].quantity;
         }
      }
      ++i;
   }
}

void* mem_alloc( size_t size ) {
   return mem_realloc( NULL, size );
}

void* mem_realloc( void* block, size_t size ) {
   struct alloc* alloc = NULL;
   if ( block ) {
      alloc = ( struct alloc* ) block - 1;
      unlink_alloc( alloc );
   }
   alloc = realloc( alloc, sizeof( *alloc ) + size );
   if ( ! alloc ) {
      mem_free_all();
      printf( "error: failed to allocate memory block of %zd bytes\n", size );
      exit( EXIT_FAILURE );
   }
   alloc->next = g_alloc;
   g_alloc = alloc;
   return alloc + 1;
}

void unlink_alloc( struct alloc* alloc ) {
   struct alloc* curr = g_alloc;
   struct alloc* prev = NULL;
   while ( curr != alloc ) {
      prev = curr;
      curr = curr->next;
   }
   if ( prev ) {
      prev->next = alloc->next;
   }
   else {
      g_alloc = alloc->next;
   }
}

void* mem_slot_alloc( size_t size ) {
   size_t i = 0;
   while ( i < g_bulk.slots_used ) {
      if ( g_bulk.slots[ i ].size == size ) {
         // Reuse a previously allocated block.
         if ( g_bulk.slots[ i ].free_block ) {
            struct free_block* free_block = g_bulk.slots[ i ].free_block;
            g_bulk.slots[ i ].free_block = free_block->next;
            return free_block;
         }
         // When no more blocks are left, allocate a series of blocks in a
         // single allocation.
         if ( ! g_bulk.slots[ i ].left ) {
            g_bulk.slots[ i ].left = g_bulk.slots[ i ].quantity;
            g_bulk.slots[ i ].block = mem_alloc( g_bulk.slots[ i ].size *
               g_bulk.slots[ i ].quantity );
         }
         char* block = g_bulk.slots[ i ].block;
         g_bulk.slots[ i ].block += g_bulk.slots[ i ].size;
         --g_bulk.slots[ i ].left;
         return block;
      }
      ++i;
   }
   return mem_alloc( size );
}

void mem_free( void* block ) {
   struct alloc* alloc = ( struct alloc* ) block - 1;
   unlink_alloc( alloc );
   free( alloc );
}

void mem_slot_free( void* block, size_t size ) {
   size_t i = 0;
   while ( i < g_bulk.slots_used ) {
      if ( g_bulk.slots[ i ].size == size ) {
         struct free_block* free_block = block;
         free_block->next = g_bulk.slots[ i ].free_block;
         g_bulk.slots[ i ].free_block = free_block;
         return;
      }
      ++i;
   }
   mem_free( block );
}

void mem_free_all( void ) {
   while ( g_alloc ) {
      struct alloc* next = g_alloc->next;
      free( g_alloc );
      g_alloc = next;
   }
}

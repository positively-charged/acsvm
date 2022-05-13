#ifndef SRC_COMMON_LIST_H
#define SRC_COMMON_LIST_H

/**
 * Singly linked list
 */

struct list_link {
   struct list_link* next;
   void* data;
};

struct list {
   struct list_link* head;
   struct list_link* tail;
   int size;
};

struct list_iter {
   struct list_link* prev;
   struct list_link* link;
};

void list_init( struct list* list );
int list_size( struct list* list );
void* list_head( struct list* list );
void* list_tail( struct list* list );
void list_append( struct list*, void* data );
void list_prepend( struct list*, void* data );
void list_iterate( struct list* list, struct list_iter* iter );
bool list_end( struct list_iter* iter );
void list_next( struct list_iter* iter );
void* list_data( struct list_iter* iter );
void list_insert_after( struct list* list,
   struct list_iter* iter, void* data );
void list_insert_before( struct list* list,
   struct list_iter* iter, void* data );
// Updates the data at the specified node and returns the old data.
void* list_replace( struct list* list,
   struct list_iter* iter, void* data );
void list_merge( struct list* receiver, struct list* giver );
// Removes the first node of the list and returns the data of the removed node.
void* list_shift( struct list* list );
void* list_remove( struct list* list, struct list_iter* iter );
void list_deinit( struct list* list );

#endif

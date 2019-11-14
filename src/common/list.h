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

typedef struct list_link* list_iter_t;

#define list_iter_init( i, list ) ( ( *i ) = ( list )->head )
#define list_end( i ) ( *( i ) == NULL )
#define list_data( i ) ( ( *i )->data )
#define list_next( i ) ( ( *i ) = ( *i )->next )
#define list_size( list ) ( ( list )->size )
#define list_head( list ) ( ( list )->head->data )
#define list_tail( list ) ( ( list )->tail->data )

void list_init( struct list* );
void list_append( struct list*, void* );
void list_append_head( struct list*, void* );
void list_merge( struct list* receiver, struct list* giver );
void* list_shift( struct list* list );
void list_deinit( struct list* );

#endif

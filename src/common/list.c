#include "misc.h"
#include "mem.h"
#include "list.h"

static struct list_link* alloc_list_link( void* data );

void list_init( struct list* list ) {
   list->head = NULL;
   list->tail = NULL;
   list->size = 0;
}

int list_size( struct list* list ) {
   return list->size;
}

void* list_head( struct list* list ) {
   if ( list->head ) {
      return list->head->data;
   }
   else {
      return NULL;
   }
}

void* list_tail( struct list* list ) {
   if ( list->tail ) {
      return list->tail->data;
   }
   else {
      return NULL;
   }
}

void list_append( struct list* list, void* data ) {
   struct list_link* link = alloc_list_link( data );
   if ( list->head ) {
      list->tail->next = link;
   }
   else {
      list->head = link;
   }
   list->tail = link;
   ++list->size;
}

static struct list_link* alloc_list_link( void* data ) {
   struct list_link* link = mem_slot_alloc( sizeof( *link ) );
   link->data = data;
   link->next = NULL;
   return link;
}

void list_prepend( struct list* list, void* data ) {
   struct list_link* link = alloc_list_link( data );
   link->next = list->head;
   list->head = link;
   if ( ! list->tail ) {
      list->tail = link;
   }
   ++list->size;
}

void list_iterate( struct list* list, struct list_iter* iter ) {
   iter->prev = NULL;
   iter->link = list->head;
}

bool list_end( struct list_iter* iter ) {
   return ( iter->link == NULL );
}

void list_next( struct list_iter* iter ) {
   if ( iter->link ) {
      iter->prev = iter->link;
      iter->link = iter->link->next;
   }
}

void* list_data( struct list_iter* iter ) {
   if ( iter->link ) {
      return iter->link->data;
   }
   else {
      return NULL;
   }
}

void list_insert_after( struct list* list,
   struct list_iter* iter, void* data ) {
   if ( iter->link ) {
      struct list_link* link = alloc_list_link( data );
      link->next = iter->link->next;
      iter->link->next = link;
      if ( ! link->next ) {
         list->tail = link;
      }
      ++list->size;
   }
   else {
      list_append( list, data );
   }
}

void list_insert_before( struct list* list,
   struct list_iter* iter, void* data ) {
   if ( iter->prev ) {
      struct list_link* link = alloc_list_link( data );
      link->next = iter->link;
      iter->prev->next = link;
      if ( ! link->next ) {
         list->tail = link;
      }
      ++list->size;
   }
   else {
      list_prepend( list, data );
   }
}

void* list_replace( struct list* list,
   struct list_iter* iter, void* data ) {
   void* replaced_data = NULL;
   if ( iter->link ) {
      replaced_data = iter->link->data;
      iter->link->data = data;
   }
   return replaced_data;
}

void list_merge( struct list* receiver, struct list* giver ) {
   if ( giver->head ) {
      if ( receiver->head ) {
         receiver->tail->next = giver->head;
      }
      else {
         receiver->head = giver->head;
      }
      receiver->tail = giver->tail;
      receiver->size += giver->size;
      list_init( giver );
   }
}

void* list_shift( struct list* list ) {
   if ( list->head ) {
      void* data = list->head->data;
      struct list_link* next_link = list->head->next;
      mem_slot_free( list->head, sizeof( *list->head ) );
      list->head = next_link;
      if ( ! list->head ) {
         list->tail = NULL;
      }
      --list->size;
      return data;
   }
   else {
      return NULL;
   }
}

/**
 * Removes a node from the list at the position specified by an iterator.
 */
void* list_remove( struct list* list, struct list_iter* iter ) {
   // Only remove a node if the iterator is at a valid node.
   if ( iter->link ) {
      struct list_link* link = iter->link;
      // Remove node at the middle or end of the list.
      if ( iter->prev ) {
         iter->prev->next = link->next;
         iter->link = iter->prev->next;
         if ( ! iter->link ) {
            list->tail = iter->prev;
         }
      }
      // Remove node at the start of the list.
      else {
         list->head = link->next;
         iter->link = list->head;
         if ( ! list->head ) {
            list->tail = NULL;
         }
      }
      void* data = link->data;
      mem_slot_free( link, sizeof( *link ) );
      --list->size;
      return data;
   }
   else {
      return NULL;
   }
}

void list_deinit( struct list* list ) {
   struct list_link* link = list->head;
   while ( link ) {
      struct list_link* next = link->next;
      mem_slot_free( link, sizeof( *link ) );
      link = next;
   }
}

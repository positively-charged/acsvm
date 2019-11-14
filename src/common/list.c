#include "misc.h"
#include "mem.h"
#include "list.h"

void list_init( struct list* list ) {
   list->head = NULL;
   list->tail = NULL;
   list->size = 0;
}

void list_append( struct list* list, void* data ) {
   struct list_link* link = mem_slot_alloc( sizeof( *link ) );
   link->data = data;
   link->next = NULL;
   if ( list->head ) {
      list->tail->next = link;
   }
   else {
      list->head = link;
   }
   list->tail = link;
   ++list->size;
}

void list_append_head( struct list* list, void* data ) {
   struct list_link* link = mem_slot_alloc( sizeof( *link ) );
   link->data = data;
   link->next = list->head;
   list->head = link;
   if ( ! list->tail ) {
      list->tail = link;
   }
   ++list->size;
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

void list_deinit( struct list* list ) {
   struct list_link* link = list->head;
   while ( link ) {
      struct list_link* next = link->next;
      mem_slot_free( link, sizeof( *link ) );
      link = next;
   }
}

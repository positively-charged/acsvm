#include <string.h>

#include "misc.h"
#include "mem.h"
#include "vector.h"

static u8* calc_element( struct vector* vector, isize index );

/**
 * Initializes a vector. A vector should then be grown to the appropriate size
 * before accessing its elements.
 */
void vector_init( struct vector* vector, isize element_size ) {
   vector->elements = NULL;
   vector->element_size = element_size;
   vector->size = 0;
   vector->capacity = 0;
}

/**
 * Changes the capacity of the vector to hold at least `n` elements. If the
 * specified capacity is lower than or equal to the current capacity, the
 * request is ignored.
 */
enum vector_grow_err vector_grow( struct vector* vector, isize n ) {
   if ( n > vector->capacity ) {
      void* elements = mem_realloc( vector->elements,
         n * vector->element_size );
      if ( elements ) {
         vector->elements = elements;
         vector->capacity = n;
         return VECTORGROWERR_NONE;
      }
      else {
         return VECTORGROWERR_MEMALLOC;
      }
   }
   else {
      return VECTORGROWERR_UNCHANGED;
   }
}

/**
 * Doubles the capacity of the vector.
 */
enum vector_grow_err vector_double( struct vector* vector ) {
   return vector_grow( vector, vector->capacity * 2 );
}

/**
 * Retrieves a pointer to the element at the specified index.
 */
struct vector_result vector_get( struct vector* vector, isize index ) {
   if ( index >= 0 && index < vector->size ) {
      void* element = ( u8* ) vector->elements +
         ( index * vector->element_size );
      return ( struct vector_result ) {
         .err = VECTORGETERR_NONE,
         .element = element
      };
   }
   else {
      return ( struct vector_result ) {
         .err = VECTORGETERR_OUTOFBOUNDS,
      };
   }
}

static u8* calc_element( struct vector* vector, isize index ) {
   return ( u8* ) vector->elements + ( index * vector->element_size );
}

void* vector_append( struct vector* vector ) {
   if ( vector->size == vector->capacity ) {
      vector_double( vector );
   }
   void* element = calc_element( vector, vector->size );
   ++vector->size;
   return element;
}

/**
 * Destroys the vector and its contents. The vector should not be used after
 * calling this function.
 */
void vector_deinit( struct vector* vector ) {
   if ( vector->elements ) {
      mem_free( vector->elements );
   }
}

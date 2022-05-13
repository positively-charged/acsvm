#ifndef SRC_COMMON_VECTOR_H
#define SRC_COMMON_VECTOR_H

struct vector {
   void* elements;
   isize element_size;
   isize size;     // Number of elements currently used in the vector.
   isize capacity; // Number of elements currently allocated.
};

enum vector_grow_err {
    VECTORGROWERR_NONE,
    VECTORGROWERR_MEMALLOC,
    VECTORGROWERR_UNCHANGED,
};

struct vector_result {
   enum {
      VECTORGETERR_NONE,
      VECTORGETERR_OUTOFBOUNDS,
   } err;
   void* element;
};

void vector_init( struct vector* vector, isize element_size );
enum vector_grow_err vector_grow( struct vector* vector, isize n );
enum vector_grow_err vector_double( struct vector* vector );
struct vector_result vector_get( struct vector* vector, isize index );
void* vector_append( struct vector* vector );
void vector_deinit( struct vector* vector );

#endif

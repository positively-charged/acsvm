#ifndef SRC_COMMON_VECTOR_H
#define SRC_COMMON_VECTOR_H

struct vector {
   void* elements;
   isize element_size;
   isize capacity;
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
void vector_deinit( struct vector* vector );

#endif

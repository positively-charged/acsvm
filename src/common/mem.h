#ifndef SRC_COMMON_MEM_H
#define SRC_COMMON_MEM_H

void mem_init( void );
void* mem_alloc( size_t );
void* mem_realloc( void*, size_t );
void* mem_slot_alloc( size_t );
void mem_slot_free( void* block, size_t size );
void mem_free( void* );
void mem_free_all( void );

#endif

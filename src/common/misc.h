#ifndef COMMON_H
#define COMMON_H

#include <stdlib.h>
#include <stdbool.h>
#include <limits.h>

#if defined( _WIN32 ) || defined( _WIN64 )
#   define OS_WINDOWS 1
#else
#   define OS_WINDOWS 0
#endif

#define ARRAY_SIZE( a ) ( sizeof( a ) / sizeof( a[ 0 ] ) )
#define STATIC_ASSERT( ... ) \
  STATIC_ASSERT_REAL( __VA_ARGS__,, )
#define STATIC_ASSERT_REAL( cond, msg, ... ) \
   extern int STATIC_ASSERT__##msg[ !! ( cond ) ]
#define UNREACHABLE() printf( \
   "%s:%d: internal error: unreachable code\n", \
   __FILE__, __LINE__ );

// Make sure the data types are of sizes we want.
STATIC_ASSERT( CHAR_BIT == 8, CHAR_BIT_must_be_8 );
STATIC_ASSERT( sizeof( char ) == 1, char_must_be_1_byte );
STATIC_ASSERT( sizeof( short ) == 2, short_must_be_2_bytes );
STATIC_ASSERT( sizeof( int ) == 4, int_must_be_4_bytes );
STATIC_ASSERT( sizeof( long long ) == 8, long_long_must_be_8_bytes );

typedef signed char i8;
typedef signed short i16;
typedef signed int i32;
typedef signed long long i64;
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef size_t usize;
typedef ssize_t isize;

#define null NULL

#endif

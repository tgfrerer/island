#ifndef _LOCK_FREE_RING_BUFFER_H_
#define _LOCK_FREE_RING_BUFFER_H_

#include <stdint.h>
#include <stddef.h>

struct lockfree_ring_buffer_t;

lockfree_ring_buffer_t* lockfree_ring_buffer_create( uint32_t power_of_2_size );
void                    lockfree_ring_buffer_destroy( lockfree_ring_buffer_t* rb );
size_t                  lockfree_ring_buffer_size( const lockfree_ring_buffer_t* rb );
int                     lockfree_ring_buffer_trypush( lockfree_ring_buffer_t* rb, void* in );
void                    lockfree_ring_buffer_push( lockfree_ring_buffer_t* rb, void* in );
void*                   lockfree_ring_buffer_trypop( lockfree_ring_buffer_t* rb );
void*                   lockfree_ring_buffer_pop( lockfree_ring_buffer_t* rb );

#endif

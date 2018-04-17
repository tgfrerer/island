#ifndef GUARD_LE_ALLOCATOR_LINEAR_H
#define GUARD_LE_ALLOCATOR_LINEAR_H

#include <stdint.h>

/*

  Api is declared as part of le_backend_vk.h

*/

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

void register_le_allocator_linear_api( void *api_ );

struct le_buffer_o;

struct LE_AllocatorCreateInfo {
	uint64_t resourceId;
	uint8_t *bufferBaseMemoryAddress = nullptr; // mapped memory address
	uint64_t bufferBaseOffsetInBytes = 0;       // offset into buffer for first address belonging to this allocator
	uint64_t capacity                = 0;
	uint64_t alignment               = 256; // minimum allocation chunk size
};

#ifdef __cplusplus
}
#	endif // __cplusplus

#endif // GUARD_LE_ALLOCATOR_LINEAR_H

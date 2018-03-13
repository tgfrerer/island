#include "pal_api_loader/ApiRegistry.hpp"

#include "le_backend_vk/le_backend_vk.h"

#include "le_backend_vk/private/le_allocator_linear.h"

#include "le_renderer/private/le_renderer_types.h"

#include "vulkan/vulkan.hpp"

/*

Linear sub-allocator

	+ Hands out memory addresses which can be written to.

	+ Memory must have been allocated and mapped before

	+ Memory must be associated to a buffer, but this association is done through
	the resource-system, we only need to know the LE-api specific handle for the
	buffer

*/

struct le_allocator_linear_o {

	le_buffer_o *leBufferHandle = nullptr;

	uint8_t *bufferBaseMemoryAddress = nullptr; // mapped memory address
	uint64_t bufferBaseOffsetInBytes = 0;       // offset into buffer for first address belonging to this allocator
	uint64_t capacity                = 0;
	uint64_t alignment               = 256; // minimum allocation chunk size

	uint8_t *pData               = bufferBaseMemoryAddress; // address of last allocation, initially
	uint64_t bufferOffsetInBytes = bufferBaseOffsetInBytes;
};

// ----------------------------------------------------------------------

static void allocator_reset(le_allocator_linear_o* self){
	self->pData               = self->bufferBaseMemoryAddress;
	self->bufferOffsetInBytes = self->bufferBaseOffsetInBytes;
}

// ----------------------------------------------------------------------

static le_allocator_linear_o* allocator_create(const LE_AllocatorCreateInfo& info){
	auto self                     = new le_allocator_linear_o;

	self->bufferBaseMemoryAddress = info.bufferBaseMemoryAddress;
	self->bufferBaseOffsetInBytes = info.bufferBaseOffsetInBytes;
	self->capacity                = info.capacity;
	self->leBufferHandle          = info.bufferHandle;
	self->alignment               = info.alignment;

	allocator_reset(self);

	return self;
}

// ----------------------------------------------------------------------

static void allocator_destroy(le_allocator_linear_o* self){
	delete self;
}

// ----------------------------------------------------------------------

static bool allocator_allocate(le_allocator_linear_o* self, uint64_t numBytes, void ** pData, uint64_t* bufferOffset){

	// Calculate allocation size as a multiple (rounded up) of alignment
	// TODO: check alignment-based allocation size calculation is correct.
	auto allocationSizeInBytes  = self->alignment * (( numBytes + ( self->alignment - 1 ) ) / self->alignment);

	auto addressAfterAllocation = self->pData + allocationSizeInBytes;

	if ( (addressAfterAllocation) > (self->bufferBaseMemoryAddress + self->capacity) ) {
		return false;
	}

	// ----------| invariant: enough capacity to accomodate numBytes

	*pData        = self->pData; // point to next free memory address
	*bufferOffset = self->bufferOffsetInBytes;

	self->pData               += allocationSizeInBytes;
	self->bufferOffsetInBytes += allocationSizeInBytes;

	return true;
}

// ----------------------------------------------------------------------

static le_buffer_o* allocator_get_le_buffer_handle(le_allocator_linear_o* self){
	return self->leBufferHandle;
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_allocator_linear_api( void *api_ ) {

	auto  le_backend_vk_api_i           = static_cast<le_backend_vk_api *>( api_ );
	auto &le_allocator_linear_i = le_backend_vk_api_i->le_allocator_linear_i;

	le_allocator_linear_i.create               = allocator_create;
	le_allocator_linear_i.destroy              = allocator_destroy;
	le_allocator_linear_i.get_le_buffer_handle = allocator_get_le_buffer_handle;
	le_allocator_linear_i.allocate             = allocator_allocate;
	le_allocator_linear_i.reset                = allocator_reset;
}

// ----------------------------------------------------------------------

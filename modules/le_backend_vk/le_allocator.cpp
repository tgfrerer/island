#include "le_core/le_core.h"
#include "le_backend_vk/le_backend_vk.h"
#include "le_backend_vk/le_backend_types_internal.h"

#include "le_renderer/private/le_renderer_types.h"
#include "le_backend_vk/util/vk_mem_alloc/vk_mem_alloc.h"

/*

Linear sub-allocator

	+ Hands out memory addresses which can be written to.

	+ Memory must have been allocated and mapped before

	+ Memory must be associated to a buffer, but this association is done through
	the resource-system, we only need to know the LE-api specific handle for the
	buffer

*/

struct le_allocator_o {

	le_buf_resource_handle resourceId = {}; // for transient allocators, this must contain index of transient allocator

	uint8_t *bufferBaseMemoryAddress = nullptr; // mapped memory address
	uint64_t bufferBaseOffsetInBytes = 0;       // offset into buffer for first address belonging to this allocator
	uint64_t capacity                = 0;
	uint64_t alignment               = 256; // 1<<8== 256, minimum allocation chunk size (should proabbly be VkPhysicalDeviceLimits::minTexelBufferOffsetAlignment - see bufferView offset "valid use" in Spec: 11.2 )

	uint8_t *pData               = bufferBaseMemoryAddress; // address of last allocation, initially: (bufferBaseMemoryAddress + bufferBaseOffsetInBytes)
	uint64_t bufferOffsetInBytes = bufferBaseOffsetInBytes;
};

// ----------------------------------------------------------------------

static void allocator_reset( le_allocator_o *self ) {
	self->bufferOffsetInBytes = self->bufferBaseOffsetInBytes;
	self->pData               = self->bufferBaseMemoryAddress + self->bufferBaseOffsetInBytes;
}

// ----------------------------------------------------------------------

static le_allocator_o *allocator_create( VmaAllocationInfo const *info, uint16_t alignment ) {
	auto self = new le_allocator_o{};

	self->bufferBaseMemoryAddress = static_cast<uint8_t *>( info->pMappedData );

	self->bufferBaseOffsetInBytes = info->offset;
	self->capacity                = info->size;
	self->alignment               = alignment;

	// -- Fetch resource handle of underlying buffer from VmaAllocation info
	memcpy( &self->resourceId, &info->pUserData, sizeof( void * ) ); // note we copy pUserData as a value

	//	self->resourceId = reinterpret_cast<le_buf_resource_handle const &>( info->pUserData );

	allocator_reset( self );

	return self;
}

// ----------------------------------------------------------------------

static void allocator_destroy( le_allocator_o *self ) {
	delete self;
}

// ----------------------------------------------------------------------

static bool allocator_allocate( le_allocator_o *self, uint64_t numBytes, void **pData, uint64_t *bufferOffset ) {

	// Calculate allocation size as a multiple (rounded up) of alignment

	auto allocationSizeInBytes = self->alignment * ( ( numBytes + ( self->alignment - 1 ) ) / self->alignment );

	auto addressAfterAllocation = self->pData + allocationSizeInBytes;

	if ( ( addressAfterAllocation ) > ( self->bufferBaseMemoryAddress + self->bufferBaseOffsetInBytes + self->capacity ) ) {
		return false;
	}

	// ----------| invariant: enough capacity to accomodate numBytes

	*pData        = self->pData; // point to next free memory address
	*bufferOffset = self->bufferOffsetInBytes;

	self->pData = addressAfterAllocation;

	self->bufferOffsetInBytes += allocationSizeInBytes;

	return true;
}

// ----------------------------------------------------------------------

static le_buf_resource_handle allocator_get_le_resource_id( le_allocator_o *self ) {
	return self->resourceId;
}

// ----------------------------------------------------------------------

void register_le_allocator_linear_api( void *api_ ) {

	auto  le_backend_vk_api_i   = static_cast<le_backend_vk_api *>( api_ );
	auto &le_allocator_linear_i = le_backend_vk_api_i->le_allocator_linear_i;

	le_allocator_linear_i.create             = allocator_create;
	le_allocator_linear_i.destroy            = allocator_destroy;
	le_allocator_linear_i.get_le_resource_id = allocator_get_le_resource_id;
	le_allocator_linear_i.allocate           = allocator_allocate;
	le_allocator_linear_i.reset              = allocator_reset;
}

// ----------------------------------------------------------------------

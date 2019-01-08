#include "pal_api_loader/ApiRegistry.hpp"

#include "le_renderer/le_renderer.h"
#include "le_renderer/private/le_renderer_types.h"

#include "le_backend_vk/le_backend_vk.h"

#include <cstring>
#include <iostream>
#include <iomanip>
#include <assert.h>
#include <vector>

#define EMPLACE_CMD( x ) new ( &self->mCommandStream[ 0 ] + self->mCommandStreamSize )( x )

struct le_command_buffer_encoder_o {
	char                    mCommandStream[ 4096 * 16 ]; // 16 pages of memory
	size_t                  mCommandStreamSize = 0;
	size_t                  mCommandCount      = 0;
	le_allocator_o *        pAllocator         = nullptr; // allocator is owned by backend, externally
	le_pipeline_manager_o * pipelineManager    = nullptr;
	le_staging_allocator_o *stagingAllocator   = nullptr; // Borrowed from backend - used for larger, permanent resources, shared amongst encoders
	le::Extent2D            extent             = {};      // Renderpass extent, otherwise swapchain extent inferred via renderer, this may be queried by users of encoder.
};

// ----------------------------------------------------------------------

static le_command_buffer_encoder_o *cbe_create( le_allocator_o *allocator, le_pipeline_manager_o *pipelineManager, le_staging_allocator_o *stagingAllocator, le::Extent2D const &extent = {} ) {
	auto self              = new le_command_buffer_encoder_o;
	self->pAllocator       = allocator;
	self->pipelineManager  = pipelineManager;
	self->stagingAllocator = stagingAllocator;
	self->extent           = extent;
	//	std::cout << "encoder create : " << std::hex << self << std::endl
	//	          << std::flush;
	return self;
};

// ----------------------------------------------------------------------

static void cbe_destroy( le_command_buffer_encoder_o *self ) {
	//	std::cout << "encoder destroy: " << std::hex << self << std::endl
	//	          << std::flush;
	delete ( self );
}

// ----------------------------------------------------------------------
// Returns extent to which this encoder has been set up to
static le::Extent2D const &cbe_get_extent( le_command_buffer_encoder_o *self ) {
	return self->extent;
}

// ----------------------------------------------------------------------

static void cbe_set_line_width( le_command_buffer_encoder_o *self, float lineWidth ) {

	auto cmd        = EMPLACE_CMD( le::CommandSetLineWidth ); // placement new into data array
	cmd->info.width = lineWidth;

	self->mCommandStreamSize += sizeof( le::CommandSetLineWidth );
	self->mCommandCount++;
}

// ----------------------------------------------------------------------

static void cbe_draw( le_command_buffer_encoder_o *self,
                      uint32_t                     vertexCount,
                      uint32_t                     instanceCount,
                      uint32_t                     firstVertex,
                      uint32_t                     firstInstance ) {

	auto cmd  = EMPLACE_CMD( le::CommandDraw ); // placement new!
	cmd->info = {vertexCount, instanceCount, firstVertex, firstInstance};

	self->mCommandStreamSize += sizeof( le::CommandDraw );
	self->mCommandCount++;
}

// ----------------------------------------------------------------------

static void cbe_draw_indexed( le_command_buffer_encoder_o *self,
                              uint32_t                     indexCount,
                              uint32_t                     instanceCount,
                              uint32_t                     firstIndex,
                              int32_t                      vertexOffset,
                              uint32_t                     firstInstance ) {

	auto cmd  = EMPLACE_CMD( le::CommandDrawIndexed );
	cmd->info = {
	    indexCount,
	    instanceCount,
	    firstIndex,
	    vertexOffset,
	    firstInstance,
	    0 // padding must be set to zero
	};

	self->mCommandStreamSize += sizeof( le::CommandDrawIndexed );
	self->mCommandCount++;
}

// ----------------------------------------------------------------------

static void cbe_set_viewport( le_command_buffer_encoder_o *self,
                              uint32_t                     firstViewport,
                              const uint32_t               viewportCount,
                              const le::Viewport *         pViewports ) {

	auto cmd = EMPLACE_CMD( le::CommandSetViewport ); // placement new!

	// We point data to the next available position in the data stream
	// so that we can store the data for viewports inline.
	void * data     = ( cmd + 1 ); // note: this increments a le::CommandSetViewport pointer by one time its object size, then gets the address
	size_t dataSize = sizeof( le::Viewport ) * viewportCount;

	cmd->info = {firstViewport, viewportCount};
	cmd->header.info.size += dataSize; // we must increase the size of this command by its payload size

	memcpy( data, pViewports, dataSize );

	self->mCommandStreamSize += cmd->header.info.size;
	self->mCommandCount++;
};

// ----------------------------------------------------------------------

static void cbe_set_scissor( le_command_buffer_encoder_o *self,
                             uint32_t                     firstScissor,
                             const uint32_t               scissorCount,
                             le::Rect2D const *           pScissors ) {

	auto cmd = EMPLACE_CMD( le::CommandSetScissor ); // placement new!

	// We point to the next available position in the data stream
	// so that we can store the data for scissors inline.
	void * data     = ( cmd + 1 );
	size_t dataSize = sizeof( le::Rect2D ) * scissorCount;

	cmd->info = {firstScissor, scissorCount};
	cmd->header.info.size += dataSize; // we must increase the size of this command by its payload size

	memcpy( data, pScissors, dataSize );

	self->mCommandStreamSize += cmd->header.info.size;
	self->mCommandCount++;
}

// ----------------------------------------------------------------------

static void cbe_bind_vertex_buffers( le_command_buffer_encoder_o *self,
                                     uint32_t                     firstBinding,
                                     uint32_t                     bindingCount,
                                     le_resource_handle_t const * pBuffers,
                                     uint64_t const *             pOffsets ) {

	// NOTE: pBuffers will hold ids for virtual buffers, we must match these
	// in the backend to actual vulkan buffer ids.
	// Buffer must be annotated whether it is transient or not

	auto cmd = EMPLACE_CMD( le::CommandBindVertexBuffers ); // placement new!

	size_t dataBuffersSize = ( sizeof( le_resource_handle_t ) ) * bindingCount;
	size_t dataOffsetsSize = ( sizeof( uint64_t ) ) * bindingCount;

	void *dataBuffers = ( cmd + 1 );
	void *dataOffsets = ( static_cast<char *>( dataBuffers ) + dataBuffersSize ); // start address for offset data

	cmd->info = {firstBinding, bindingCount, static_cast<le_resource_handle_t *>( dataBuffers ), static_cast<uint64_t *>( dataOffsets )};
	cmd->header.info.size += dataBuffersSize + dataOffsetsSize; // we must increase the size of this command by its payload size

	memcpy( dataBuffers, pBuffers, dataBuffersSize );
	memcpy( dataOffsets, pOffsets, dataOffsetsSize );

	self->mCommandStreamSize += cmd->header.info.size;
	self->mCommandCount++;
}

// ----------------------------------------------------------------------
static void cbe_bind_index_buffer( le_command_buffer_encoder_o *self,
                                   le_resource_handle_t const   buffer,
                                   uint64_t                     offset,
                                   le::IndexType const &        indexType ) {

	auto cmd = EMPLACE_CMD( le::CommandBindIndexBuffer );

	// Note: indexType==0 means uint16, indexType==1 means uint32
	cmd->info = {buffer, offset, static_cast<uint32_t>( indexType )};

	self->mCommandStreamSize += cmd->header.info.size;
	self->mCommandCount++;
}

// ----------------------------------------------------------------------

static void cbe_set_vertex_data( le_command_buffer_encoder_o *self,
                                 void const *                 data,
                                 uint64_t                     numBytes,
                                 uint32_t                     bindingIndex ) {

	// -- Allocate data on scratch buffer
	// -- Upload data via scratch allocator
	// -- Bind vertex buffers to scratch allocator

	using namespace le_backend_vk; // for le_allocator_linear_i

	void *   memAddr;
	uint64_t bufferOffset = 0;

	if ( le_allocator_linear_i.allocate( self->pAllocator, numBytes, &memAddr, &bufferOffset ) ) {
		memcpy( memAddr, data, numBytes );

		le_resource_handle_t allocatorBufferId = le_allocator_linear_i.get_le_resource_id( self->pAllocator );

		cbe_bind_vertex_buffers( self, bindingIndex, 1, &allocatorBufferId, &bufferOffset );
	} else {
		std::cerr << "ERROR " << __PRETTY_FUNCTION__ << " could not allocate " << numBytes << " Bytes." << std::endl
		          << std::flush;
	}
}

// ----------------------------------------------------------------------

static void cbe_set_index_data( le_command_buffer_encoder_o *self,
                                void const *                 data,
                                uint64_t                     numBytes,
                                le::IndexType const &        indexType ) {

	using namespace le_backend_vk; // for le_allocator_linear_i

	void *   memAddr;
	uint64_t bufferOffset = 0;

	// -- Allocate data on scratch buffer
	if ( le_allocator_linear_i.allocate( self->pAllocator, numBytes, &memAddr, &bufferOffset ) ) {

		// -- Upload data via scratch allocator
		memcpy( memAddr, data, numBytes );

		le_resource_handle_t allocatorBufferId = le_allocator_linear_i.get_le_resource_id( self->pAllocator );

		// -- Bind index buffer to scratch allocator
		cbe_bind_index_buffer( self, allocatorBufferId, bufferOffset, indexType );
	} else {
		std::cerr << "ERROR " << __PRETTY_FUNCTION__ << " could not allocate " << numBytes << " Bytes." << std::endl
		          << std::flush;
	}
}

// ----------------------------------------------------------------------

static void cbe_set_argument_data( le_command_buffer_encoder_o *self,
                                   uint64_t                     argumentNameId, // hash id of argument name
                                   void const *                 data,
                                   size_t                       numBytes ) {

	using namespace le_backend_vk; // for le_allocator_linear_i

	auto cmd = EMPLACE_CMD( le::CommandSetArgumentData );

	void *   memAddr;
	uint64_t bufferOffset = 0;

	// -- Allocate memory on scratch buffer for ubo
	//
	// Note that we might want to have specialised ubo memory eventually if that
	// made a performance difference.
	if ( le_allocator_linear_i.allocate( self->pAllocator, numBytes, &memAddr, &bufferOffset ) ) {

		// -- Store ubo data to scratch allocator
		memcpy( memAddr, data, numBytes );

		le_resource_handle_t allocatorBufferId = le_allocator_linear_i.get_le_resource_id( self->pAllocator );

		cmd->info.argument_name_id = argumentNameId;
		cmd->info.buffer_id        = allocatorBufferId;
		cmd->info.offset           = uint32_t( bufferOffset ); // Note: we are assuming offset is never > 4GB, which appears realistic for now
		cmd->info.range            = uint32_t( numBytes );

	} else {
		std::cerr << "ERROR " << __PRETTY_FUNCTION__ << " could not allocate " << numBytes << " Bytes." << std::endl
		          << std::flush;
		return;
	}

	self->mCommandStreamSize += sizeof( le::CommandSetArgumentData );
	self->mCommandCount++;
}

// ----------------------------------------------------------------------

static void cbe_set_argument_texture( le_command_buffer_encoder_o *self, le_resource_handle_t const textureId, uint64_t argumentName, uint64_t arrayIndex ) {

	auto cmd = EMPLACE_CMD( le::CommandSetArgumentTexture );

	cmd->info.argument_name_id = argumentName;
	cmd->info.texture_id       = textureId;
	cmd->info.array_index      = arrayIndex;

	self->mCommandStreamSize += sizeof( le::CommandSetArgumentTexture );
	self->mCommandCount++;
}

// ----------------------------------------------------------------------

static void cbe_bind_graphics_pipeline( le_command_buffer_encoder_o *self, le_gpso_handle gpsoHandle ) {

	// -- insert graphics PSO pointer into command stream
	auto cmd = EMPLACE_CMD( le::CommandBindGraphicsPipeline );

	cmd->info.gpsoHandle = gpsoHandle;

	self->mCommandStreamSize += sizeof( le::CommandBindGraphicsPipeline );
	self->mCommandCount++;
}

// ----------------------------------------------------------------------

static void cbe_bind_compute_pipeline( le_command_buffer_encoder_o *self, le_cpso_handle cpsoHandle ) {

	// -- insert compute PSO pointer into command stream
	auto cmd = EMPLACE_CMD( le::CommandBindComputePipeline );

	cmd->info.cpsoHandle = cpsoHandle;

	self->mCommandStreamSize += sizeof( le::CommandBindComputePipeline );
	self->mCommandCount++;
}

// ----------------------------------------------------------------------

static void cbe_write_to_buffer( le_command_buffer_encoder_o *self, le_resource_handle_t const &resourceId, size_t offset, void const *data, size_t numBytes ) {

	auto cmd = EMPLACE_CMD( le::CommandWriteToBuffer );

	using namespace le_backend_vk; // for le_allocator_linear_i
	void *               memAddr;
	le_resource_handle_t srcResourceId;

	// -- Allocate memory using staging allocator
	//
	// We don't use the encoder local scratch linear allocator, since memory written to buffers is
	// typically a lot larger than uniforms and other small settings structs. Staging memory is also
	// allocated so that it is only used for TRANSFER_SRC, and shared amongst encoders so that we
	// use available memory more efficiently.
	//
	if ( le_staging_allocator_i.map( self->stagingAllocator, numBytes, &memAddr, &srcResourceId ) ) {
		// -- Write data to scratch memory now
		memcpy( memAddr, data, numBytes );

		cmd->info.src_buffer_id = srcResourceId;
		cmd->info.src_offset    = 0; // staging allocator will give us a fresh buffer, and src memory will be placed at its start
		cmd->info.dst_offset    = offset;
		cmd->info.numBytes      = numBytes;
		cmd->info.dst_buffer_id = resourceId;
	} else {
		std::cerr << "ERROR " << __PRETTY_FUNCTION__ << " could not allocate " << numBytes << " Bytes." << std::endl
		          << std::flush;
		return;
	}

	self->mCommandStreamSize += sizeof( le::CommandWriteToBuffer );
	self->mCommandCount++;
}

// ----------------------------------------------------------------------

static void cbe_write_to_image( le_command_buffer_encoder_o *self,
                                le_resource_handle_t const & resourceId,
                                le_resource_info_t const &   resourceInfo,
                                void const *                 data,
                                size_t                       numBytes ) {

	assert( resourceInfo.type == LeResourceType::eImage );

	// ----------| invariant: resource info represents an image

	auto const &imageInfo = resourceInfo.image;

	auto cmd = EMPLACE_CMD( le::CommandWriteToImage );

	using namespace le_backend_vk; // for le_allocator_linear_i
	void *               memAddr;
	le_resource_handle_t srcResourceId;

	// -- Allocate memory using staging allocator
	//
	// We don't use the encoder local scratch linear allocator, since memory written to buffers is
	// typically a lot larger than uniforms and other small settings structs. Staging memory is also
	// allocated so that it is only used for TRANSFER_SRC, and shared amongst encoders so that we
	// use available memory more efficiently.
	//
	if ( le_staging_allocator_i.map( self->stagingAllocator, numBytes, &memAddr, &srcResourceId ) ) {

		// -- Write data to the freshly allocated buffer
		memcpy( memAddr, data, numBytes );

		cmd->info.src_buffer_id = srcResourceId;           // resource id of staging buffer
		cmd->info.numBytes      = numBytes;                // total number of bytes from staging buffer which need to be synchronised.
		cmd->info.dst_image_id  = resourceId;              // resouce id for target image resource
		cmd->info.mipLevelCount = imageInfo.mipLevels;     // number of miplevels to generate - default is 1, *must not* be 0.
		cmd->info.image_w       = imageInfo.extent.width;  // image extent
		cmd->info.image_h       = imageInfo.extent.height; // image extent

	} else {
		std::cerr << "ERROR " << __PRETTY_FUNCTION__ << " could not allocate " << numBytes << " Bytes." << std::endl
		          << std::flush;
		return;
	}
	// increase command stream size by size of command, plus size of regions attached to command.
	self->mCommandStreamSize += cmd->header.info.size;
	self->mCommandCount++;
}

// ----------------------------------------------------------------------

static void cbe_get_encoded_data( le_command_buffer_encoder_o *self,
                                  void **                      data,
                                  size_t *                     numBytes,
                                  size_t *                     numCommands ) {

	*data        = self->mCommandStream;
	*numBytes    = self->mCommandStreamSize;
	*numCommands = self->mCommandCount;
}

// ----------------------------------------------------------------------

static le_pipeline_manager_o *cbe_get_pipeline_manager( le_command_buffer_encoder_o *self ) {
	return self->pipelineManager;
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_command_buffer_encoder_api( void *api_ ) {

	auto &cbe_i = static_cast<le_renderer_api *>( api_ )->le_command_buffer_encoder_i;

	cbe_i.create                 = cbe_create;
	cbe_i.destroy                = cbe_destroy;
	cbe_i.draw                   = cbe_draw;
	cbe_i.draw_indexed           = cbe_draw_indexed;
	cbe_i.get_extent             = cbe_get_extent;
	cbe_i.set_line_width         = cbe_set_line_width;
	cbe_i.set_viewport           = cbe_set_viewport;
	cbe_i.set_scissor            = cbe_set_scissor;
	cbe_i.bind_vertex_buffers    = cbe_bind_vertex_buffers;
	cbe_i.bind_index_buffer      = cbe_bind_index_buffer;
	cbe_i.set_index_data         = cbe_set_index_data;
	cbe_i.set_vertex_data        = cbe_set_vertex_data;
	cbe_i.set_argument_data      = cbe_set_argument_data;
	cbe_i.set_argument_texture   = cbe_set_argument_texture;
	cbe_i.bind_graphics_pipeline = cbe_bind_graphics_pipeline;
	cbe_i.bind_compute_pipeline  = cbe_bind_compute_pipeline;
	cbe_i.get_encoded_data       = cbe_get_encoded_data;
	cbe_i.write_to_buffer        = cbe_write_to_buffer;
	cbe_i.write_to_image         = cbe_write_to_image;
	cbe_i.get_pipeline_manager   = cbe_get_pipeline_manager;
}

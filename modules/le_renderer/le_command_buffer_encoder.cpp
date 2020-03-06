#include "le_core/le_core.h"

#include "le_renderer/le_renderer.h"
#include "le_renderer/private/le_renderer_types.h"

#include "le_backend_vk/le_backend_vk.h"

#include <cstring>
#include <iostream>
#include <iomanip>
#include <assert.h>
#include <vector>

#ifndef LE_MT
#	define LE_MT 0
#endif

#if ( LE_MT > 0 )
#	include "le_jobs/le_jobs.h"
#endif

// ----------------------------------------------------------------------
// return allocator offset based on current worker thread index.
static inline int fetch_allocator_index() {
#if ( LE_MT > 0 )
	int result = le_jobs::get_current_worker_id();
	assert( result >= 0 );
	return result;
#else
	return 0;
#endif
}

static inline le_allocator_o *fetch_allocator( le_allocator_o **ppAlloc ) {
	int             index  = fetch_allocator_index();
	le_allocator_o *result = *( ppAlloc + index );
	assert( result );
	return result;
}

// ----------------------------------------------------------------------

#define EMPLACE_CMD( x ) new ( &self->mCommandStream[ 0 ] + self->mCommandStreamSize )( x )

// ----------------------------------------------------------------------

struct le_command_buffer_encoder_o {
	char                    mCommandStream[ 4096 * 512 ]; // 512 pages of memory = 2MB
	size_t                  mCommandStreamSize = 0;
	size_t                  mCommandCount      = 0;
	le_allocator_o **       ppAllocator        = nullptr; // allocator list is owned by backend, externally
	le_pipeline_manager_o * pipelineManager    = nullptr;
	le_staging_allocator_o *stagingAllocator   = nullptr; // Borrowed from backend - used for larger, permanent resources, shared amongst encoders
	le::Extent2D            extent             = {};      // Renderpass extent, otherwise swapchain extent inferred via renderer, this may be queried by users of encoder.
};

// ----------------------------------------------------------------------

static le_command_buffer_encoder_o *cbe_create( le_allocator_o **allocator, le_pipeline_manager_o *pipelineManager, le_staging_allocator_o *stagingAllocator, le::Extent2D const &extent = {} ) {
	auto self              = new le_command_buffer_encoder_o;
	self->ppAllocator      = allocator;
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

static void cbe_dispatch( le_command_buffer_encoder_o *self, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ ) {

	auto cmd  = EMPLACE_CMD( le::CommandDispatch ); // placement new!
	cmd->info = {groupCountX, groupCountY, groupCountZ, 0};

	self->mCommandStreamSize += sizeof( le::CommandDispatch );
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

	if ( /* DISABLES CODE */ ( true ) ) {

		memcpy( data, pViewports, dataSize );

	} else {

		// Copy viewport data whilst flipping viewports,
		//
		// see: <https://www.saschawillems.de/blog/2019/03/29/flipping-the-vulkan-viewport/>
		// We do this instead of directly moving data, because it allows us to keep our
		// shaders to have the Y-Axis go up as it does in OpenGL, whereas otherwise
		// we would have the Vulkan default, with the Y-axis pointing down.

		auto const    pViewportsEnd   = pViewports + viewportCount;
		le::Viewport *pTargetViewport = static_cast<le::Viewport *>( data );

		for ( auto v = pViewports; v != pViewportsEnd; v++, pTargetViewport++ ) {
			*pTargetViewport = *v;
			pTargetViewport->y += pTargetViewport->height;
			pTargetViewport->height = -pTargetViewport->height;
		}
	}

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
	cmd->info = {buffer, offset, indexType, 0};

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

	if ( data == nullptr || numBytes == 0 )
		return;

	// --------| invariant: there are some bytes to set

	void *   memAddr      = nullptr;
	uint64_t bufferOffset = 0;

	le_allocator_o *allocator = fetch_allocator( self->ppAllocator );

	if ( le_allocator_linear_i.allocate( allocator, numBytes, &memAddr, &bufferOffset ) ) {

		memcpy( memAddr, data, numBytes );

		le_resource_handle_t allocatorBufferId = le_allocator_linear_i.get_le_resource_id( allocator );

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

	if ( data == nullptr || numBytes == 0 )
		return;

	// --------| invariant: there are some bytes to set

	void *   memAddr;
	uint64_t bufferOffset = 0;

	le_allocator_o *allocator = fetch_allocator( self->ppAllocator );

	// -- Allocate data on scratch buffer
	if ( le_allocator_linear_i.allocate( allocator, numBytes, &memAddr, &bufferOffset ) ) {

		// -- Upload data via scratch allocator
		memcpy( memAddr, data, numBytes );

		le_resource_handle_t allocatorBufferId = le_allocator_linear_i.get_le_resource_id( allocator );

		// -- Bind index buffer to scratch allocator
		cbe_bind_index_buffer( self, allocatorBufferId, bufferOffset, indexType );
	} else {
		std::cerr << "ERROR " << __PRETTY_FUNCTION__ << " could not allocate " << numBytes << " Bytes." << std::endl
		          << std::flush;
	}
}

// ----------------------------------------------------------------------

static void cbe_bind_argument_buffer( le_command_buffer_encoder_o *self, le_resource_handle_t const bufferId, uint64_t argumentName, uint64_t offset, uint64_t range ) {

	auto cmd = EMPLACE_CMD( le::CommandBindArgumentBuffer );

	cmd->info.argument_name_id = argumentName;
	cmd->info.buffer_id        = bufferId;
	cmd->info.offset           = offset;
	cmd->info.range            = range;

	self->mCommandStreamSize += sizeof( le::CommandBindArgumentBuffer );
	self->mCommandCount++;
}

// ----------------------------------------------------------------------
static void cbe_set_argument_data( le_command_buffer_encoder_o *self,
                                   uint64_t                     argumentNameId, // hash id of argument name
                                   void const *                 data,
                                   size_t                       numBytes ) {

	using namespace le_backend_vk; // for le_allocator_linear_i

	if ( data == nullptr || numBytes == 0 )
		return;

	// --------| invariant: there are some bytes to set

	void *   memAddr;
	uint64_t bufferOffset = 0;

	le_allocator_o *allocator = fetch_allocator( self->ppAllocator );

	// -- Allocate memory on scratch buffer for ubo
	//
	// Note that we might want to have specialised ubo memory eventually if that
	// made a performance difference.
	if ( le_allocator_linear_i.allocate( allocator, numBytes, &memAddr, &bufferOffset ) ) {

		// -- Store ubo data to scratch allocator
		memcpy( memAddr, data, numBytes );

		le_resource_handle_t allocatorBuffer = le_allocator_linear_i.get_le_resource_id( allocator );

		cbe_bind_argument_buffer( self, allocatorBuffer, argumentNameId, uint32_t( bufferOffset ), uint32_t( numBytes ) );

	} else {
		std::cerr << "ERROR " << __PRETTY_FUNCTION__ << " could not allocate " << numBytes << " Bytes." << std::endl
		          << std::flush;
		return;
	}
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

static void cbe_set_argument_image( le_command_buffer_encoder_o *self, le_resource_handle_t const imageId, uint64_t argumentName, uint64_t arrayIndex ) {

	auto cmd = EMPLACE_CMD( le::CommandSetArgumentImage );

	cmd->info.argument_name_id = argumentName;
	cmd->info.image_id         = imageId;
	cmd->info.array_index      = arrayIndex;

	self->mCommandStreamSize += sizeof( le::CommandSetArgumentImage );
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

static void cbe_bind_rtx_pipeline( le_command_buffer_encoder_o *self, le_rtxpso_handle psoHandle ) {

	// -- insert rtx PSO pointer into command stream
	auto cmd = EMPLACE_CMD( le::CommandBindRtxPipeline );

	cmd->info.rtxpsoHandle = psoHandle;

	self->mCommandStreamSize += sizeof( le::CommandBindRtxPipeline );
	self->mCommandCount++;
}

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
	// typically a lot larger than uniforms and other small settings structs. Staging memory is
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

static void cbe_write_to_image( le_command_buffer_encoder_o *       self,
                                le_resource_handle_t const &        imageId,
                                le_write_to_image_settings_t const &writeInfo,
                                void const *                        data,
                                size_t                              numBytes ) {

	assert( imageId.handle.as_handle.meta.as_meta.type == LeResourceType::eImage );

	// ----------| invariant: resource info represents an image

	auto cmd = EMPLACE_CMD( le::CommandWriteToImage );

	using namespace le_backend_vk; // for le_allocator_linear_i
	void *               memAddr;
	le_resource_handle_t stagingBufferId;

	// -- Allocate memory using staging allocator
	//
	// We don't use the encoder local scratch linear allocator, since memory written to buffers is
	// typically a lot larger than uniforms and other small settings structs. Staging memory is also
	// allocated so that it is only used for TRANSFER_SRC, and shared amongst encoders so that we
	// use available memory more efficiently.
	//
	if ( le_staging_allocator_i.map( self->stagingAllocator, numBytes, &memAddr, &stagingBufferId ) ) {

		// -- Write data to the freshly allocated buffer
		memcpy( memAddr, data, numBytes );

		assert( writeInfo.num_miplevels != 0 ); // number of miplevels must be at least 1.

		cmd->info.src_buffer_id = stagingBufferId;         // resource id of staging buffer
		cmd->info.numBytes      = numBytes;                // total number of bytes from staging buffer which need to be synchronised.
		cmd->info.dst_image_id  = imageId;                 // resouce id for target image resource
		cmd->info.dst_miplevel  = writeInfo.dst_miplevel;  // default 0, use higher number to manually upload higher mip levels.
		cmd->info.num_miplevels = writeInfo.num_miplevels; // default is 1, *must not* be 0. More than 1 means to auto-generate these miplevels
		cmd->info.image_w       = writeInfo.image_w;       // image extent
		cmd->info.image_h       = writeInfo.image_h;       // image extent
		cmd->info.offset_h      = writeInfo.offset_h;      // offset into image where to place data
		cmd->info.offset_w      = writeInfo.offset_w;      // offset into image where to place data

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

static void cbe_build_rtx_blas( le_command_buffer_encoder_o *     self,
                                le_resource_handle_t const *const p_blas_handles,
                                const uint32_t                    handles_count ) {

	if ( handles_count == 0 || nullptr == p_blas_handles ) {
		assert( p_blas_handles && handles_count > 0 && "must provide handles, and handles_count must be at least 1" );
		// no-op: no handles specified to be built.
		return;
	}

	auto   cmd       = EMPLACE_CMD( le::CommandBuildRtxBlas );
	void * data      = cmd + 1;
	size_t data_size = sizeof( le_resource_handle_t ) * handles_count;

	cmd->info                    = {};
	cmd->info.blas_handles_count = handles_count;
	cmd->header.info.size += data_size;

	memcpy( data, p_blas_handles, data_size );

	self->mCommandStreamSize += cmd->header.info.size;
	self->mCommandCount++;
}

// ----------------------------------------------------------------------

void cbe_build_rtx_tlas( le_command_buffer_encoder_o *     self,
                         le_resource_handle_t const *      tlas_handle,
                         le_rtx_geometry_instance_t const *instances,
                         le_resource_handle_t const *      blas_handles,
                         uint32_t                          instances_count ) {

	auto cmd = EMPLACE_CMD( le::CommandBuildRtxTlas );

	cmd->info                          = {};
	cmd->info.tlas_handle              = *tlas_handle;
	cmd->info.geometry_instances_count = instances_count;

	// We allocate memory from our scratch allocator, and write geometry instance data into the allocated memory.
	// since instance data contains le_resource_handles for blas instances these need to be resolved
	// in the backend when processing the command, and patched into the memory, before that memory
	// is used to build the tlas.

	// We can access that memory with confidence since that area of memory is associated with that command,
	// and there will ever only be one thread processing ever processing the command.
	//
	// Command buffer encoder writes only to that memory, then its ownership moves - together with the frame -
	// to the backend, where the backend has exclusive ownership of the memory.

	size_t gpu_memory_bytes_required = sizeof( le_rtx_geometry_instance_t ) * instances_count;

	le_allocator_o *allocator = fetch_allocator( self->ppAllocator );
	uint64_t        offset    = 0;

	using namespace le_backend_vk; // for le_allocator_linear_i

	if ( le_allocator_linear_i.allocate( allocator, gpu_memory_bytes_required, &cmd->info.staging_buffer_mapped_memory, &offset ) ) {

		// Store geometry instances data in GPU mapped scratch buffer - we will patch
		// blas references in the backend later, once we know how to resolve them.
		memcpy( cmd->info.staging_buffer_mapped_memory, instances, gpu_memory_bytes_required );

		cmd->info.staging_buffer_offset = uint32_t( offset );
		cmd->info.staging_buffer_id     = le_allocator_linear_i.get_le_resource_id( allocator );

	} else {
		std::cerr << "ERROR " << __PRETTY_FUNCTION__ << " could not allocate " << gpu_memory_bytes_required << " Bytes." << std::endl
		          << std::flush;
		return;
	}

	// We store the blas handles inline with the command - so that we can patch them with actual
	// VkAccelerationStructureHandles in the backend, where the names of the actual objects
	// are known.

	size_t payload_size = sizeof( le_resource_handle_t ) * instances_count;
	cmd->header.info.size += payload_size;

	void *memAddr = cmd + 1; // move to position just after command
	memcpy( memAddr, blas_handles, payload_size );

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

void register_le_command_buffer_encoder_api( void *api_ ) {

	auto &cbe_i = static_cast<le_renderer_api *>( api_ )->le_command_buffer_encoder_i;

	cbe_i.create                 = cbe_create;
	cbe_i.destroy                = cbe_destroy;
	cbe_i.draw                   = cbe_draw;
	cbe_i.draw_indexed           = cbe_draw_indexed;
	cbe_i.dispatch               = cbe_dispatch;
	cbe_i.get_extent             = cbe_get_extent;
	cbe_i.set_line_width         = cbe_set_line_width;
	cbe_i.set_viewport           = cbe_set_viewport;
	cbe_i.set_scissor            = cbe_set_scissor;
	cbe_i.bind_vertex_buffers    = cbe_bind_vertex_buffers;
	cbe_i.bind_index_buffer      = cbe_bind_index_buffer;
	cbe_i.set_index_data         = cbe_set_index_data;
	cbe_i.set_vertex_data        = cbe_set_vertex_data;
	cbe_i.set_argument_data      = cbe_set_argument_data;
	cbe_i.bind_argument_buffer   = cbe_bind_argument_buffer;
	cbe_i.set_argument_texture   = cbe_set_argument_texture;
	cbe_i.set_argument_image     = cbe_set_argument_image;
	cbe_i.bind_graphics_pipeline = cbe_bind_graphics_pipeline;
	cbe_i.bind_compute_pipeline  = cbe_bind_compute_pipeline;
	cbe_i.bind_rtx_pipeline      = cbe_bind_rtx_pipeline;
	cbe_i.get_encoded_data       = cbe_get_encoded_data;
	cbe_i.write_to_buffer        = cbe_write_to_buffer;
	cbe_i.write_to_image         = cbe_write_to_image;
	cbe_i.build_rtx_blas         = cbe_build_rtx_blas;
	cbe_i.build_rtx_tlas         = cbe_build_rtx_tlas;
	cbe_i.get_pipeline_manager   = cbe_get_pipeline_manager;
}

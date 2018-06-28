#include "pal_api_loader/ApiRegistry.hpp"

#include "le_renderer/le_renderer.h"
#include "le_renderer/private/le_renderer_types.h"

#include "le_backend_vk/le_backend_vk.h"

#include <cstring>
#include <iostream>
#include <iomanip>

#define EMPLACE_CMD( x ) new ( &self->mCommandStream[ 0 ] + self->mCommandStreamSize )( x )

struct le_command_buffer_encoder_o {
	char            mCommandStream[ 4096 ];
	size_t          mCommandStreamSize = 0;
	size_t          mCommandCount      = 0;
	le_allocator_o *pAllocator         = nullptr; // allocator is owned by backend, externally
};

// ----------------------------------------------------------------------

static le_command_buffer_encoder_o *cbe_create( le_allocator_o *allocator_ ) {
	auto self        = new le_command_buffer_encoder_o;
	self->pAllocator = allocator_;

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
                             const le::Rect2D *           pScissors ) {

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
                                     uint64_t *                   pBuffers,
                                     uint64_t *                   pOffsets ) {

	// Note: pBuffers will hold ids for virtual buffers, we must match these
	// in the backend to actual vulkan buffer ids.

	auto cmd = EMPLACE_CMD( le::CommandBindVertexBuffers ); // placement new!

	void *dataBuffers = ( cmd + 1 );
	void *dataOffsets = ( static_cast<char *>( dataBuffers ) + sizeof( uint64_t ) * bindingCount );

	size_t dataBuffersSize = ( sizeof( void * ) + sizeof( uint64_t ) ) * bindingCount;
	size_t dataOffsetsSize = ( sizeof( void * ) + sizeof( uint64_t ) ) * bindingCount;

	cmd->info = {firstBinding, bindingCount, static_cast<uint64_t *>( dataBuffers ), static_cast<uint64_t *>( dataOffsets )};
	cmd->header.info.size += dataBuffersSize + dataOffsetsSize; // we must increase the size of this command by its payload size

	memcpy( dataBuffers, pBuffers, dataBuffersSize );
	memcpy( dataOffsets, pOffsets, dataOffsetsSize );

	self->mCommandStreamSize += cmd->header.info.size;
	self->mCommandCount++;
}

// ----------------------------------------------------------------------

static void cbe_bind_index_buffer( le_command_buffer_encoder_o *self,
                                   uint64_t                     buffer,
                                   uint64_t                     offset,
                                   uint64_t                     indexType ) {

	auto cmd = EMPLACE_CMD( le::CommandBindIndexBuffer );

	cmd->info = {buffer, offset, indexType};

	self->mCommandStreamSize += cmd->header.info.size;
	self->mCommandCount++;
}

// ----------------------------------------------------------------------

static void cbe_set_vertex_data( le_command_buffer_encoder_o *self,
                                 void *                       data,
                                 uint64_t                     numBytes,
                                 uint32_t                     bindingIndex ) {

	// -- Allocate data on scratch buffer
	// -- Upload data via scratch allocator
	// -- Bind vertex buffers to scratch allocator

	static auto &allocator_i = Registry::getApi<le_backend_vk_api>()->le_allocator_linear_i;

	void *   memAddr;
	uint64_t bufferOffset = 0;

	if ( allocator_i.allocate( self->pAllocator, numBytes, &memAddr, &bufferOffset ) ) {
		memcpy( memAddr, data, numBytes );

		auto allocatorBufferId = allocator_i.get_le_resource_id( self->pAllocator );

		cbe_bind_vertex_buffers( self, bindingIndex, 1, &allocatorBufferId, &bufferOffset );
	} else {
		std::cerr << "ERROR " << __PRETTY_FUNCTION__ << " could not allocate " << numBytes << "bytes.";
	}
}

// ----------------------------------------------------------------------

static void cbe_set_index_data( le_command_buffer_encoder_o *self,
                                void *                       data,
                                uint64_t                     numBytes,
                                uint64_t                     indexType ) {

	static auto &allocator_i = Registry::getApi<le_backend_vk_api>()->le_allocator_linear_i;

	void *   memAddr;
	uint64_t bufferOffset = 0;

	// -- Allocate data on scratch buffer
	if ( allocator_i.allocate( self->pAllocator, numBytes, &memAddr, &bufferOffset ) ) {

		// -- Upload data via scratch allocator
		memcpy( memAddr, data, numBytes );

		auto allocatorBufferId = allocator_i.get_le_resource_id( self->pAllocator );

		// -- Bind index buffer to scratch allocator
		cbe_bind_index_buffer( self, allocatorBufferId, bufferOffset, indexType );
	} else {
		std::cerr << "ERROR " << __PRETTY_FUNCTION__ << " could not allocate " << numBytes << "bytes.";
	}
}

static void cbe_set_argument_ubo_data( le_command_buffer_encoder_o *self,
                                       uint64_t                     argumentNameId, // hash id of argument name
                                       void *                       data,
                                       size_t                       numBytes ) {

	static auto &allocator_i = Registry::getApi<le_backend_vk_api>()->le_allocator_linear_i;

	auto cmd = EMPLACE_CMD( le::CommandSetArgumentUbo );

	void *   memAddr;
	uint64_t bufferOffset = 0;

	// -- Allocate memory on scratch buffer for ubo
	//
	// Note that we might want to have specialised ubo memory eventually if that
	// made a performance difference.
	if ( allocator_i.allocate( self->pAllocator, numBytes, &memAddr, &bufferOffset ) ) {

		// -- Store ubo data to scratch allocator
		memcpy( memAddr, data, numBytes );

		auto allocatorBufferId = allocator_i.get_le_resource_id( self->pAllocator );

		cmd->info.argument_name_id = argumentNameId;
		cmd->info.buffer_id        = allocatorBufferId;
		cmd->info.offset           = uint32_t( bufferOffset ); // Note: we are assuming offset is never > 4GB, which appears realistic for now
		cmd->info.range            = uint32_t( numBytes );

	} else {
		std::cerr << "ERROR " << __PRETTY_FUNCTION__ << " could not allocate " << numBytes << "bytes.";
		return;
	}

	self->mCommandStreamSize += sizeof( le::CommandSetArgumentUbo );
	self->mCommandCount++;
}

// ----------------------------------------------------------------------

static void cbe_bind_pipeline( le_command_buffer_encoder_o *self, le_graphics_pipeline_state_o *pso ) {

	// -- insert PSO pointer into command stream
	auto cmd = EMPLACE_CMD( le::CommandBindPipeline );

	cmd->info.pso = pso;

	//	std::cout << "binding pipeline" << std::endl
	//	          << std::flush;

	self->mCommandStreamSize += sizeof( le::CommandBindPipeline );
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

ISL_API_ATTR void register_le_command_buffer_encoder_api( void *api_ ) {

	auto &le_command_buffer_encoder_i = static_cast<le_renderer_api *>( api_ )->le_command_buffer_encoder_i;

	le_command_buffer_encoder_i.create                 = cbe_create;
	le_command_buffer_encoder_i.destroy                = cbe_destroy;
	le_command_buffer_encoder_i.draw                   = cbe_draw;
	le_command_buffer_encoder_i.draw_indexed           = cbe_draw_indexed;
	le_command_buffer_encoder_i.set_line_width         = cbe_set_line_width;
	le_command_buffer_encoder_i.set_viewport           = cbe_set_viewport;
	le_command_buffer_encoder_i.set_scissor            = cbe_set_scissor;
	le_command_buffer_encoder_i.bind_vertex_buffers    = cbe_bind_vertex_buffers;
	le_command_buffer_encoder_i.bind_index_buffer      = cbe_bind_index_buffer;
	le_command_buffer_encoder_i.set_index_data         = cbe_set_index_data;
	le_command_buffer_encoder_i.set_vertex_data        = cbe_set_vertex_data;
	le_command_buffer_encoder_i.set_argument_ubo_data  = cbe_set_argument_ubo_data;
	le_command_buffer_encoder_i.bind_graphics_pipeline = cbe_bind_pipeline;
	le_command_buffer_encoder_i.get_encoded_data       = cbe_get_encoded_data;
}

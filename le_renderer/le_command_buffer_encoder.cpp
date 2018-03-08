#include "pal_api_loader/ApiRegistry.hpp"
#include "le_renderer/le_renderer.h"
#include "le_renderer/private/le_renderer_types.h"
#include <cstring>

struct le_command_buffer_encoder_o {
	char   mCommandStream[ 4096 ];
	size_t mCommandStreamSize = 0;
	size_t mCommandCount      = 0;
};

// ----------------------------------------------------------------------

// TODO: must assign allocator
static le_command_buffer_encoder_o *cbe_create() {
	auto self = new le_command_buffer_encoder_o;
	return self;
};

// ----------------------------------------------------------------------

static void cbe_destroy( le_command_buffer_encoder_o *self ) {
	delete ( self );
}

// ----------------------------------------------------------------------

static void cbe_set_line_width( le_command_buffer_encoder_o *self, float lineWidth ) {

	// placement new into data array
	le::CommandSetLineWidth *cmd = new ( &self->mCommandStream[ 0 ] + self->mCommandStreamSize ) le::CommandSetLineWidth;

	cmd->info.width = lineWidth;

	self->mCommandStreamSize += sizeof( le::CommandSetLineWidth );
	self->mCommandCount++;
}

// ----------------------------------------------------------------------

static void cbe_draw( le_command_buffer_encoder_o *self, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance ) {
	le::CommandDraw *cmd = new ( &self->mCommandStream[ 0 ] + self->mCommandStreamSize ) le::CommandDraw; // placement new!
	cmd->info            = {vertexCount, instanceCount, firstVertex, firstInstance};
	self->mCommandStreamSize += sizeof( le::CommandDraw );
	self->mCommandCount++;
}

// ----------------------------------------------------------------------

static void cbe_set_viewport( le_command_buffer_encoder_o *self, uint32_t firstViewport, const uint32_t viewportCount, const le::Viewport *pViewports ) {

	le::CommandSetViewport *cmd = new ( &self->mCommandStream[ 0 ] + self->mCommandStreamSize ) le::CommandSetViewport; // placement new!

	// We point to the next available position in the data stream
	// so that we can store the data for viewports inline.
	void * data = (cmd + sizeof( le::CommandSetViewport ));
	size_t dataSize = sizeof(le::Viewport) * viewportCount;

	cmd->info = {firstViewport, viewportCount, static_cast<le::Viewport *>( data )};
	cmd->header.info.size += dataSize; // we must increase the size of this command by its payload size

	memcpy( data, pViewports, dataSize );

	self->mCommandStreamSize += cmd->header.info.size;
	self->mCommandCount++;
};

// ----------------------------------------------------------------------

static void cbe_set_scissor(le_command_buffer_encoder_o* self, uint32_t firstScissor, const uint32_t scissorCount, const le::Rect2D*pScissors){

	le::CommandSetScissor *cmd = new ( &self->mCommandStream[ 0 ] + self->mCommandStreamSize ) le::CommandSetScissor; // placement new!

	// We point to the next available position in the data stream
	// so that we can store the data for viewports inline.
	void * data = (cmd + sizeof( le::CommandSetScissor));
	size_t dataSize = sizeof(le::Rect2D) * scissorCount;

	cmd->info = {firstScissor, scissorCount, static_cast<le::Rect2D *>( data )};
	cmd->header.info.size += dataSize; // we must increase the size of this command by its payload size

	memcpy( data, pScissors, dataSize );

	self->mCommandStreamSize += cmd->header.info.size;
	self->mCommandCount++;

}

// ----------------------------------------------------------------------

static void cbe_get_encoded_data( le_command_buffer_encoder_o *self, void **data, size_t *numBytes, size_t *numCommands ) {
	*data        = self->mCommandStream;
	*numBytes    = self->mCommandStreamSize;
	*numCommands = self->mCommandCount;
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_command_buffer_encoder_api( void *api_ ) {

	auto  le_renderer_api_i           = static_cast<le_renderer_api *>( api_ );
	auto &le_command_buffer_encoder_i = le_renderer_api_i->le_command_buffer_encoder_i;

	le_command_buffer_encoder_i.create           = cbe_create;
	le_command_buffer_encoder_i.destroy          = cbe_destroy;
	le_command_buffer_encoder_i.set_line_width   = cbe_set_line_width;
	le_command_buffer_encoder_i.draw             = cbe_draw;
	le_command_buffer_encoder_i.get_encoded_data = cbe_get_encoded_data;
	le_command_buffer_encoder_i.set_viewport     = cbe_set_viewport;
	le_command_buffer_encoder_i.set_scissor      = cbe_set_scissor;
}

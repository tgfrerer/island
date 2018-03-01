#include "pal_api_loader/ApiRegistry.hpp"
#include "le_renderer/le_renderer.h"
#include "le_renderer/private/le_renderer_types.h"

struct le_command_buffer_encoder_o {
	char mCommandStream[4096];
	size_t mCommandStreamSize = 0;
	size_t mCommandCount      = 0;
};

// ----------------------------------------------------------------------

static le_command_buffer_encoder_o* cbe_create(){
	auto obj = new le_command_buffer_encoder_o;
	return obj;
};

// ----------------------------------------------------------------------

static void cbe_destroy(le_command_buffer_encoder_o* self){
	delete(self);
}

// ----------------------------------------------------------------------

static void cbe_set_line_width(le_command_buffer_encoder_o* self, float lineWidth){

	// placement new into data array
	le::CommandSetLineWidth * cmd = new(&self->mCommandStream[0] + self->mCommandStreamSize) le::CommandSetLineWidth;
	
	cmd->info.width = lineWidth;
	
	self->mCommandStreamSize += sizeof( le::CommandSetLineWidth );
	self->mCommandCount++;
}

static void cbe_draw(le_command_buffer_encoder_o* self, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstIndex, uint32_t firstInstance){
	le::CommandDraw * cmd = new(&self->mCommandStream[0]+self->mCommandStreamSize) le::CommandDraw;
	cmd->info = {vertexCount, instanceCount,firstIndex,firstInstance};
	self->mCommandStreamSize += sizeof(le::CommandDraw);
	self->mCommandCount ++;
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_command_buffer_encoder_api( void *api_ ) {

	auto  le_renderer_api_i           = static_cast<le_renderer_api *>( api_ );
	auto &le_command_buffer_encoder_i = le_renderer_api_i->le_command_buffer_encoder_i;

	le_command_buffer_encoder_i.create         = cbe_create;
	le_command_buffer_encoder_i.destroy        = cbe_destroy;
	le_command_buffer_encoder_i.set_line_width = cbe_set_line_width;
	le_command_buffer_encoder_i.draw           = cbe_draw;
}

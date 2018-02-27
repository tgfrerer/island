#include "pal_api_loader/ApiRegistry.hpp"
#include "le_renderer/le_renderer.h"

struct le_command_buffer_encoder_o {
	uint64_t test;
};

static le_command_buffer_encoder_o* command_buffer_encoder_create(){
	auto obj = new le_command_buffer_encoder_o;
	return obj;
};

static void command_buffer_encoder_destroy(le_command_buffer_encoder_o* self){
	delete(self);
}

ISL_API_ATTR void register_le_command_buffer_encoder_api(void*api_){

	    auto  le_renderer_api_i = static_cast<le_renderer_api *>( api_ );
		auto &le_command_buffer_encoder_i      = le_renderer_api_i->le_command_buffer_encoder_i;

		le_command_buffer_encoder_i.create  = command_buffer_encoder_create;
		le_command_buffer_encoder_i.destroy = command_buffer_encoder_destroy;
}

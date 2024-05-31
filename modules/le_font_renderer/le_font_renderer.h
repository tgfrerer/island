#ifndef GUARD_le_font_renderer_H
#define GUARD_le_font_renderer_H

#include "le_core.h"

struct le_font_renderer_o;
struct le_font_o;
struct le_renderer_o;
struct le_rendergraph_o;
struct le_renderpass_o;
struct le_command_buffer_encoder_o;

LE_OPAQUE_HANDLE( le_image_resource_handle );
LE_OPAQUE_HANDLE( le_texture_handle );

// clang-format off
struct le_font_renderer_api {

	struct draw_string_info_t{
		char const * str;
		float x;
		float y;
		struct {
			float r;
			float g;
			float b;
			float a;
		} color;
	};

	struct le_font_renderer_interface_t {

		le_font_renderer_o*    (* create                 )( le_renderer_o* renderer);
		void                   (* destroy                )( le_font_renderer_o* self );

		void                   (* add_font               )( le_font_renderer_o* self, le_font_o* font );

		bool                   (* setup_resources        )( le_font_renderer_o* self, le_rendergraph_o* module );
		bool                   (* use_fonts              )( le_font_renderer_o* self, le_font_o**, size_t num_fonts, le_renderpass_o* pass);

		bool (*draw_string)( le_font_renderer_o* self, le_font_o* font, le_command_buffer_encoder_o* encoder, draw_string_info_t  & info );

		le_texture_handle      (* get_font_image_sampler )( le_font_renderer_o* self, le_font_o* font );
		le_image_resource_handle (* get_font_image         )( le_font_renderer_o* self, le_font_o* font );

	};

	le_font_renderer_interface_t       le_font_renderer_i;
};
// clang-format on
LE_MODULE( le_font_renderer );
LE_MODULE_LOAD_DEFAULT( le_font_renderer );

#ifdef __cplusplus

namespace le_font_renderer {
static const auto& api                = le_font_renderer_api_i;
static const auto& le_font_renderer_i = api -> le_font_renderer_i;
using draw_string_info_t              = le_font_renderer_api::draw_string_info_t;
} // namespace le_font_renderer

#endif // __cplusplus

#endif

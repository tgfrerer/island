#ifndef GUARD_le_font_renderer_H
#define GUARD_le_font_renderer_H

#include <stdint.h>
#include "le_core/le_core.hpp"

#ifdef __cplusplus
extern "C" {
#endif

struct le_font_renderer_o;
struct le_font_o;

struct le_renderer_o;

struct le_render_module_o;
struct le_renderpass_o;
struct le_command_buffer_encoder_o;

struct le_resource_handle_t;

void register_le_font_renderer_api( void *api );

// clang-format off
struct le_font_renderer_api {
	static constexpr auto id      = "le_font_renderer";
	static constexpr auto pRegFun = register_le_font_renderer_api;

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

		bool                   (* setup_resources        )( le_font_renderer_o* self, le_render_module_o* module );
		bool                   (* use_fonts              )( le_font_renderer_o* self, le_font_o**, size_t num_fonts, le_renderpass_o* pass);

		bool (*draw_string)( le_font_renderer_o* self, le_font_o* font, le_command_buffer_encoder_o* encoder, draw_string_info_t  & info );

		le_resource_handle_t * (* get_font_image_sampler )( le_font_renderer_o* self, le_font_o* font );
		le_resource_handle_t * (* get_font_image         )( le_font_renderer_o* self, le_font_o* font );

	};

	le_font_renderer_interface_t       le_font_renderer_i;
};
// clang-format on

#ifdef __cplusplus
} // extern c

namespace le_font_renderer {
#	ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<le_font_renderer_api>( true );
#	else
const auto api = Registry::addApiStatic<le_font_renderer_api>();
#	endif

static const auto &le_font_renderer_i = api -> le_font_renderer_i;

using draw_string_info_t = le_font_renderer_api::draw_string_info_t;

} // namespace le_font_renderer

#endif // __cplusplus

#endif

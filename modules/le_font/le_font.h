#ifndef GUARD_le_font_H
#define GUARD_le_font_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#define ISL_ALLOW_GLM_TYPES
// Life is terrible without 3d type primitives, so let's include some glm forward declarations
#ifdef ISL_ALLOW_GLM_TYPES
#	include <glm/fwd.hpp>
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct le_font_o;
struct le_glyph_shape_o;
struct le_path_o;

void register_le_font_api( void *api );

// clang-format off
struct le_font_api {

#ifdef ISL_ALLOW_GLM_TYPES
	typedef glm::vec2 Vertex;
#else
	struct Vertex{
		float x;
		float y;
	};
#endif

	static constexpr auto id      = "le_font";
	static constexpr auto pRegFun = register_le_font_api;


	struct le_font_interface_t {
		le_font_o *			 ( * create                   ) ( char const * font_filename, float font_size );
		void                 ( * destroy                  ) ( le_font_o* self );
		le_glyph_shape_o*	 ( * get_shape_for_glyph      ) ( le_font_o* font, int32_t codepoint, size_t* num_contours);
		bool                 ( * create_atlas             ) ( le_font_o* self );
		bool                 ( * get_atlas                ) ( le_font_o* self, uint8_t const ** pixels, uint32_t * width, uint32_t * height, uint32_t *pix_stride_in_bytes );
		size_t				 ( * draw_utf8_string         ) ( le_font_o *self, const char *str, float* x_pos, float* y_pos, glm::vec4 *vertices, size_t max_vertices, size_t vertex_offset );

		// NOTE: `codepoint_prev` is optional, if 0, no kerning is applied, any other value will apply kerning for kerning pair (`codepoint_prev`,`codepoint`).
		void                 ( * add_paths_for_glyph   ) ( le_font_o const * self, int32_t const codepoint,  int32_t const codepoint_prev, le_path_o* path, float const scale, glm::vec2 *offset);
	};

	struct glyph_shape_interface_t{
		// created via font_interface
		void				( * destroy                        ) ( le_glyph_shape_o* self );
		size_t              ( * get_num_contours               ) ( le_glyph_shape_o* self );
		Vertex*				( * get_vertices_for_shape_contour ) ( le_glyph_shape_o* shape, size_t const &contour_idx, size_t* num_vertices);
	};


	le_font_interface_t       le_font_i;
	glyph_shape_interface_t   le_glyph_shape_i;
};
// clang-format on

#ifdef __cplusplus
} // extern c

namespace le_font {
#	ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<le_font_api>( true );
#	else
const auto api = Registry::addApiStatic<le_font_api>();
#	endif

static const auto &le_font_i        = api -> le_font_i;
static const auto &le_glyph_shape_i = api -> le_glyph_shape_i;

} // namespace le_font

class LeFont : NoCopy, NoMove {

	le_font_o *self;

  public:
	LeFont( char const *font_filename, float font_size = 24.f )
	    : self( le_font::le_font_i.create( font_filename, font_size ) ) {
	}

	~LeFont() {
		le_font::le_font_i.destroy( self );
	}
};

#endif // __cplusplus

#endif

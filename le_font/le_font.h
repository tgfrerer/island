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
		le_font_o *			 ( * create                   ) ( char const * font_filename );
		void                 ( * destroy                  ) ( le_font_o* self );
		le_glyph_shape_o*	 ( * get_shape_for_glyph      ) ( le_font_o* font, int32_t codepoint, size_t* num_contours);
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
	LeFont( char const *font_filename )
	    : self( le_font::le_font_i.create( font_filename ) ) {
	}

	~LeFont() {
		le_font::le_font_i.destroy( self );
	}
};

#endif // __cplusplus

#endif

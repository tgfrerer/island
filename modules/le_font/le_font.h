#ifndef GUARD_le_font_H
#define GUARD_le_font_H

#include "le_core.h"
#include "glm/fwd.hpp"

struct le_font_o;
struct le_path_o;
struct le_path_operations_interface_t;

// clang-format off
struct le_font_api {

	typedef void le_uft8_iterator_cb_t( uint32_t codepoint, void *user_data );

	// Iterate over utf-8 glyphs: <https://en.m.wikipedia.org/wiki/UTF-8>
	// Calls given callback for each codepoint in str.
	// Runs until it meets '\0' (end of c-string) character.
	// Returns true on success, false if the last codepoint was not completely
	// parsed.
	bool  (*le_utf8_iterator)( char const *str, void *user_data, le_uft8_iterator_cb_t cb );

	struct le_font_interface_t {
		le_font_o *			 ( * create                     ) ( char const * font_filename, float font_size );
		void                 ( * destroy                    ) ( le_font_o* self );
		bool                 ( * create_atlas               ) ( le_font_o* self );
		bool                 ( * get_atlas                  ) ( le_font_o* self, uint8_t const ** pixels, uint32_t * width, uint32_t * height, uint32_t *pix_stride_in_bytes );
		size_t				 ( * draw_utf8_string           ) ( le_font_o *self, const char *str, float* x_pos, float* y_pos, glm::vec4 *vertices, size_t max_vertices, size_t vertex_offset );
		float                ( * get_scale_for_pixel_height ) ( le_font_o const * self, float height_in_pixels);

		uint8_t*             ( * create_codepoint_sdf_bitmap  ) ( le_font_o* self, float scale, int codepoint, int padding, unsigned char onedge_value, float pixel_dist_scale, int *width, int *height, int *xoff, int *yoff);
		void                 ( * destroy_codepoint_sdf_bitmap ) ( le_font_o* self, uint8_t * bitmap);

		// NOTE: `codepoint_prev` is optional, if 0, no kerning is applied, any other value will apply kerning for kerning pair (`codepoint_prev`,`codepoint`).
		void                 ( * add_paths_for_glyph      ) ( le_font_o const * self, void* path_or_user_data, int32_t const codepoint, float const scale, glm::vec2 *offset, int32_t const codepoint_prev, le_path_operations_interface_t const * optional_path_operations_interface);

	};

	le_font_interface_t       le_font_i;
};
// clang-format on

LE_MODULE( le_font );
LE_MODULE_LOAD_DEFAULT( le_font );

#ifdef __cplusplus

namespace le_font {
static const auto& api              = le_font_api_i;
static const auto& le_font_i        = api -> le_font_i;
static const auto& le_utf8_iterator = api -> le_utf8_iterator;

} // namespace le_font

namespace le {

class Font : NoCopy, NoMove {

	le_font_o* self;

  public:
	Font( char const* font_filename, float font_size = 24.f )
	    : self( le_font::le_font_i.create( font_filename, font_size ) ) {
	}

	bool createAtlas() {
		return le_font::le_font_i.create_atlas( self );
	}

	bool getAtlas( uint8_t const** pixels, uint32_t& width, uint32_t& height, uint32_t& pix_stride_in_bytes ) {
		return le_font::le_font_i.get_atlas( self, pixels, &width, &height, &pix_stride_in_bytes );
	}

	~Font() {
		le_font::le_font_i.destroy( self );
	}
};
} // namespace le

#endif // __cplusplus

#endif

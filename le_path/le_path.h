#ifndef GUARD_le_path_H
#define GUARD_le_path_H

/* le_path
 *
 * A module to handle vector paths using bezier curves.
 *
*/

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#ifdef __cplusplus

#	define ISL_ALLOW_GLM_TYPES
// Life is terrible without 3d type primitives, so let's include some glm forward declarations
#	ifdef ISL_ALLOW_GLM_TYPES
#		include <glm/fwd.hpp>
#	endif

extern "C" {
#endif

struct le_path_o;

void register_le_path_api( void *api );

// clang-format off
struct le_path_api {

#ifdef ISL_ALLOW_GLM_TYPES
	typedef glm::vec2 Vertex;
#else
	struct Vertex{
		float x;
		float y;
	};
#endif

	static constexpr auto id      = "le_path";
	static constexpr auto pRegFun = register_le_path_api;

	struct le_path_interface_t {

		le_path_o *	( * create                   ) ( );
		void        ( * destroy                  ) ( le_path_o* self );

		void        (* move_to                   ) ( le_path_o* self, Vertex const& p);
		void        (* line_to                   ) ( le_path_o* self, Vertex const& p);
		void        (* quad_bezier_to            ) ( le_path_o* self, Vertex const& p, Vertex const & c1);
		void        (* cubic_bezier_to           ) ( le_path_o* self, Vertex const& p, Vertex const & c1, Vertex const & c2);
		void        (* close_path                ) ( le_path_o* self);

		void        (* add_from_simplified_svg ) (le_path_o* self, char const* svg);

		void        (* trace_path                ) ( le_path_o* self );

		size_t      (* get_num_polylines         ) ( le_path_o* self );
		void        (* get_vertices_for_polyline ) ( le_path_o* self, size_t const &polyline_index, Vertex const **vertices, size_t * numVertices );

	};

	le_path_interface_t       le_path_i;
};
// clang-format on

#ifdef __cplusplus
} // extern c

namespace le_path {
#	ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<le_path_api>( true );
#	else
const auto api = Registry::addApiStatic<le_path_api>();
#	endif

static const auto &le_path_i = api -> le_path_i;

} // namespace le_path

class LePath : NoCopy, NoMove {

	le_path_o *self;

  public:
	LePath()
	    : self( le_path::le_path_i.create() ) {
	}

	~LePath() {
		le_path::le_path_i.destroy( self );
	}

	operator auto() {
		return self;
	}
};

#endif // __cplusplus

#endif

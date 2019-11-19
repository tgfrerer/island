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

#	include <glm/fwd.hpp>

extern "C" {
#endif

struct le_path_o;

void register_le_path_api( void *api );

// clang-format off
struct le_path_api {

	typedef glm::vec2 Vertex;

	static constexpr auto id      = "le_path";
	static constexpr auto pRegFun = register_le_path_api;

    typedef void contour_vertex_cb (void *user_data, Vertex const& p);
    typedef void contour_quad_bezier_cb(void *user_data, Vertex const& p0, Vertex const& p1, Vertex const& c);

	struct le_path_interface_t {

		le_path_o *	( * create                   ) ( );
		void        ( * destroy                  ) ( le_path_o* self );

		void        (* move_to                   ) ( le_path_o* self, Vertex const* p );
		void        (* line_to                   ) ( le_path_o* self, Vertex const* p );
		void        (* quad_bezier_to            ) ( le_path_o* self, Vertex const* p, Vertex const * c1 );
		void        (* cubic_bezier_to           ) ( le_path_o* self, Vertex const* p, Vertex const * c1, Vertex const * c2 );
		void        (* close                     ) ( le_path_o* self);

		void        (* add_from_simplified_svg   ) ( le_path_o* self, char const* svg );

		void        (* trace                     ) ( le_path_o* self, size_t resolution );
		void        (* resample                  ) ( le_path_o* self, float interval);

		void        (* clear                     ) ( le_path_o* self );

        size_t      (* get_num_contours          ) ( le_path_o* self );
		size_t      (* get_num_polylines         ) ( le_path_o* self );

		void        (* get_vertices_for_polyline ) ( le_path_o* self, size_t const &polyline_index, Vertex const **vertices, size_t * numVertices );
		void        (* get_tangents_for_polyline ) ( le_path_o* self, size_t const &polyline_index, Vertex const **tangents, size_t * numTangents );

		void        (* get_polyline_at_pos_interpolated ) ( le_path_o* self, size_t const &polyline_index, float normPos, Vertex& result);

        void        (* iterate_vertices_for_contour)(le_path_o* self, size_t const & contour_index, contour_vertex_cb callback, void* user_data);
        void        (* iterate_quad_beziers_for_contour)(le_path_o* self, size_t const & contour_index, contour_quad_bezier_cb callback, void* user_data);

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

namespace le {

class Path : NoCopy, NoMove {

	le_path_o *self;

  public:
	Path()
	    : self( le_path::le_path_i.create() ) {
	}

	~Path() {
		le_path::le_path_i.destroy( self );
	}

	Path &moveTo( le_path_api::Vertex const &p ) {
		le_path::le_path_i.move_to( self, &p );
		return *this;
	}

	Path &lineTo( le_path_api::Vertex const &p ) {
		le_path::le_path_i.line_to( self, &p );
		return *this;
	}

	Path &quadBezierTo( le_path_api::Vertex const &p, le_path_api::Vertex const &c1 ) {
		le_path::le_path_i.quad_bezier_to( self, &p, &c1 );
		return *this;
	}

	Path &cubicBezierTo( le_path_api::Vertex const &p, le_path_api::Vertex const &c1, le_path_api::Vertex const &c2 ) {
		le_path::le_path_i.cubic_bezier_to( self, &p, &c1, &c2 );
		return *this;
	}

	Path &addFromSimplifiedSvg( char const *svg ) {
		le_path::le_path_i.add_from_simplified_svg( self, svg );
		return *this;
	}

	void close() {
		le_path::le_path_i.close( self );
	}

	void trace( size_t resolution = 12 ) {
		le_path::le_path_i.trace( self, resolution );
	}

	void resample( float interval ) {
		le_path::le_path_i.resample( self, interval );
	}

	size_t getNumPolylines() {
		return le_path::le_path_i.get_num_polylines( self );
	}

	size_t getNumContours() {
		return le_path::le_path_i.get_num_contours( self );
	}

	void getVerticesForPolyline( size_t const &polyline_index, le_path_api::Vertex const **vertices, size_t *numVertices ) {
		le_path::le_path_i.get_vertices_for_polyline( self, polyline_index, vertices, numVertices );
	}

	void getTangentsForPolyline( size_t const &polyline_index, le_path_api::Vertex const **tangents, size_t *numTangents ) {
		le_path::le_path_i.get_tangents_for_polyline( self, polyline_index, tangents, numTangents );
	}

	void getPolylineAtPos( size_t const &polylineIndex, float normalizedPos, le_path_api::Vertex &vertex ) {
		le_path::le_path_i.get_polyline_at_pos_interpolated( self, polylineIndex, normalizedPos, vertex );
	}

	void clear() {
		le_path::le_path_i.clear( self );
	}

	operator auto() {
		return self;
	}
};
} // end namespace le

#endif // __cplusplus

#endif

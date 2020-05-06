#ifndef GUARD_le_path_H
#define GUARD_le_path_H

/* le_path
 *
 * 2D vector graphics, with minimal useful support for SVG-style commands.
 *
*/

#include "le_core/le_core.h"
#include <glm/fwd.hpp> // TODO: get rid of glm as this is a cpp header.

struct le_path_o;

// clang-format off
struct le_path_api {


	struct stroke_attribute_t {
	
		enum LineJoinType : uint32_t { // names for these follow svg standard: https://developer.mozilla.org/en-US/docs/Web/SVG/Attribute/stroke-linejoin
			eLineJoinMiter = 0,
			eLineJoinBevel,
			eLineJoinRound,
		};
		enum LineCapType : uint32_t { // names for these follow SVG standard: https://developer.mozilla.org/en-US/docs/Web/SVG/Attribute/stroke-linecap
			eLineCapButt = 0,
			eLineCapRound,
			eLineCapSquare,
		};
	
		float        tolerance;      // Maximum allowed distance from curve segment to straight line approximating the segment, in pixels.
		float        width;          // Stroke width
		LineJoinType line_join_type; // Type of connection between line segments
		LineCapType  line_cap_type;
	};

    typedef void contour_vertex_cb (void *user_data, glm::vec2 const& p);
    typedef void contour_quad_bezier_cb(void *user_data, glm::vec2 const& p0, glm::vec2 const& p1, glm::vec2 const& c);

	struct le_path_interface_t {

		le_path_o *	( * create                   ) ( );
		void        ( * destroy                  ) ( le_path_o* self );
        void        ( * clear                    ) ( le_path_o* self );

		void        (* move_to                   ) ( le_path_o* self, glm::vec2 const* p );
		void        (* line_to                   ) ( le_path_o* self, glm::vec2 const* p );
		void        (* quad_bezier_to            ) ( le_path_o* self, glm::vec2 const* p, glm::vec2 const * c1 );
		void        (* cubic_bezier_to           ) ( le_path_o* self, glm::vec2 const* p, glm::vec2 const * c1, glm::vec2 const * c2 );
		void        (* arc_to                    ) ( le_path_o* self, glm::vec2 const* p, glm::vec2 const * radii, float phi, bool large_arc, bool sweep);
		void        (* close                     ) ( le_path_o* self);

        // Apply hobby algorithm onto path - any instructions apart from `moveto` and
        // `close` will be turned into cubic bezier instructions. 
        void        (* hobby                     ) ( le_path_o* self );

		// Macro - style commands which resolve to a series of subcommands from above
		void        (* ellipse                   ) ( le_path_o* self, glm::vec2 const* centre, float r_x, float r_y );

		void        (* add_from_simplified_svg   ) ( le_path_o* self, char const* svg );

        // Generate and cache polylines for each contour per path
		void        (* trace                     ) ( le_path_o* self, size_t resolution );
		void        (* flatten                   ) ( le_path_o* self, float tolerance);
		void        (* resample                  ) ( le_path_o* self, float interval);

        // Always updates `max_count_outline_[l|r] with the number of used vertices for l and r outline.
        // Returns false if either given `max_count_outline_[l|r]` was less than the number of vertices needed
        // Returns true if max_count_outline_[l|r] was sufficient to hold vertices for l and r outline: also
        // writes vertex data to `outline_l_` and `outline_r_`.
	    bool        (* generate_offset_outline_for_contour )(le_path_o *self, size_t contour_index, float line_weight, float tolerance, glm::vec2 *outline_l_, size_t *max_count_outline_l, glm::vec2 *outline_r_, size_t *max_count_outline_r );

		/// Returns `false` if num_vertices was smaller than needed number of vertices.
		/// Note: Upon return, `*num_vertices` will contain number of vertices needed to describe tessellated contour triangles.
		bool        (* tessellate_thick_contour)(le_path_o* self, size_t contour_index, struct stroke_attribute_t const * stroke_attributes, glm::vec2* vertices, size_t* num_vertices);

        size_t      (* get_num_contours          ) ( le_path_o* self );
		size_t      (* get_num_polylines         ) ( le_path_o* self );

        // Always updates `numVertices` with number of vertices in polyline at `polyline_index`
        // If `numVertices` < number of vertices in polyline at `polyline_index`:
        //      + Returns true
        //      + Updates `vertices` with vertex data from polyline at `polyline_index`
        // Else
        //      + Returns false
		bool        (* get_vertices_for_polyline ) ( le_path_o* self, size_t const &polyline_index, glm::vec2 *vertices, size_t * numVertices );
		bool        (* get_tangents_for_polyline ) ( le_path_o* self, size_t const &polyline_index, glm::vec2 *tangents, size_t * numTangents );

		void        (* get_polyline_at_pos_interpolated ) ( le_path_o* self, size_t const &polyline_index, float normPos, glm::vec2* result);

        void        (* iterate_vertices_for_contour)(le_path_o* self, size_t const & contour_index, contour_vertex_cb callback, void* user_data);
        void        (* iterate_quad_beziers_for_contour)(le_path_o* self, size_t const & contour_index, contour_quad_bezier_cb callback, void* user_data);
		
	};

	le_path_interface_t       le_path_i;
};
// clang-format on

LE_MODULE( le_path );
LE_MODULE_LOAD_DEFAULT( le_path );

#ifdef __cplusplus

namespace le_path {
static const auto &api       = le_path_api_i;
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

	Path &moveTo( glm::vec2 const &p ) {
		le_path::le_path_i.move_to( self, &p );
		return *this;
	}

	Path &lineTo( glm::vec2 const &p ) {
		le_path::le_path_i.line_to( self, &p );
		return *this;
	}

	Path &quadBezierTo( glm::vec2 const &p, glm::vec2 const &c1 ) {
		le_path::le_path_i.quad_bezier_to( self, &p, &c1 );
		return *this;
	}

	Path &cubicBezierTo( glm::vec2 const &p, glm::vec2 const &c1, glm::vec2 const &c2 ) {
		le_path::le_path_i.cubic_bezier_to( self, &p, &c1, &c2 );
		return *this;
	}

	Path &arcTo( glm::vec2 const &p, glm::vec2 const &radii, float phi, bool large_arc, bool sweep ) {
		le_path::le_path_i.arc_to( self, &p, &radii, phi, large_arc, sweep );
		return *this;
	}

	Path &ellipse( glm::vec2 const &centre, float radiusX, float radiusY ) {
		le_path::le_path_i.ellipse( self, &centre, radiusX, radiusY );
		return *this;
	}

	Path &circle( glm::vec2 const &centre, float radius ) {
		le_path::le_path_i.ellipse( self, &centre, radius, radius );
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

	void flatten( float tolerance = 0.25f ) {
		le_path::le_path_i.flatten( self, tolerance );
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

	bool getVerticesForPolyline( size_t const &polyline_index, glm::vec2 *vertices, size_t *numVertices ) {
		return le_path::le_path_i.get_vertices_for_polyline( self, polyline_index, vertices, numVertices );
	}

	bool getTangentsForPolyline( size_t const &polyline_index, glm::vec2 *tangents, size_t *numTangents ) {
		return le_path::le_path_i.get_tangents_for_polyline( self, polyline_index, tangents, numTangents );
	}

	void getPolylineAtPos( size_t const &polylineIndex, float normalizedPos, glm::vec2 *vertex ) {
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

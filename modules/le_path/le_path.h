#ifndef GUARD_le_path_H
#define GUARD_le_path_H

/* le_path
 *
 * 2D vector graphics, with minimal useful support for SVG-style commands.
 *
 */

#include "le_core.h"

struct le_path_o;

#ifdef __cplusplus
#	include "3rdparty/src/glm/glm/fwd.hpp"
using float2 = glm::vec2;
#else
struct float2 {
	float x;
	float y;
};
#endif

#include "shared/interfaces/le_path_operations_interface.h"

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

	typedef void contour_vertex_cb( void* user_data, float2 const* p );
	typedef void contour_quad_bezier_cb( void* user_data, float2 const* p0, float2 const* p1, float2 const* c );



	struct le_path_interface_t {

		le_path_o* ( *create 	)();
		le_path_o* ( *clone  	)( le_path_o const* self );
		void ( *destroy 		)( le_path_o* self );
		void ( *clear   		)( le_path_o* self );

		// Apply hobby algorithm onto path - any instructions apart from `moveto` and
		// `close` will be turned into cubic bezier instructions.
		void ( *hobby )( le_path_o* self );

		// Apply hobby algorithm onto path - any instructions apart from `moveto` and
		// `close` will be turned into cubic bezier instructions.
		void ( *natural_cubic )( le_path_o* self );

		// Macro - style commands which resolve to a series of subcommands from above
		void ( *ellipse )( le_path_o* self, float2 const* centre, float r_x, float r_y );

		// parses path string with internal path operations provided by this module
		void ( *add_from_simplified_svg )( le_path_o* self, char const* svg );

		// parses path string with callback for path operations provided explicitly 
		void ( *parse_simplified_svg )( void* user_data, le_path_operations_interface_t const* cb, char const* svg );
		
		// iterate over full path, subpaths (contours) will begin with `moveto` instructions
		void ( *iterate)(le_path_o* self, void* user_data, le_path_operations_interface_t const* cb);

		// ----------------------------------------------------------------------
		// Traces the path with all its subpaths into a list of polylines.
		// Each subpath will be translated into one polyline.
		// A polyline is a list of vertices which may be thought of being
		// connected by lines.
		//
		// resolution controls into how many straight lines a curve instruction should be translated
		void ( *trace    )( le_path_o* self, size_t resolution );

		// Update a path's polylines by trying to best match given tolerance
		// tolerance controls max distance from straight line to curve (lower is higher fidelity)
		//
		// Note: this updates the polyline representation of the path.
		void ( *flatten  )( le_path_o* self, float tolerance );

		// Update a path's polylines by setting polyline vertices at even intervals
		//
		// Note: this operates only on polylines, and does not touch path instructions.
		// If polylines were not yet generated, this will generate polylines implicitly by tracing the path.
		void ( *resample )( le_path_o* self, float interval );

		// Always updates `max_count_outline_[l|r] with the number of used vertices for l and r outline.
		// Returns false if either given `max_count_outline_[l|r]` was less than the number of vertices needed
		// Returns true if max_count_outline_[l|r] was sufficient to hold vertices for l and r outline: also
		// writes vertex data to `outline_l_` and `outline_r_`.
		bool ( *generate_offset_outline_for_contour )( le_path_o* self, size_t contour_index, float line_weight, float tolerance, float2* outline_l_, size_t* max_count_outline_l, float2* outline_r_, size_t* max_count_outline_r );

		/// Returns `false` if num_vertices was smaller than needed number of vertices.
		/// Note: Upon return, `*num_vertices` will contain number of vertices needed to describe tessellated contour triangles.
		bool ( *tessellate_thick_contour )( le_path_o* self, size_t contour_index, struct stroke_attribute_t const* stroke_attributes, float2* vertices, size_t* num_vertices );

		/// Append copy of contours from the current path to the target path
		/// returns false if not all contour could be copied, or if the index 
		/// of the first contour to copy could not be found in the source path.
		bool (*copy_contours_to)(le_path_o* source, le_path_o* target, uint32_t first_contour_to_copy, uint32_t num_contours_to_copy);

		size_t ( *get_num_contours  )( le_path_o* self );
		size_t ( *get_num_polylines )( le_path_o* self );

		// the total distance of the path for a path that has been processed to polylines
		bool (*get_polylines_total_distance)(le_path_o* self, float *out_distance);

		// return true if distance for the polyline at given index can be found
		// writes distance into out_distance if successful
		bool (*get_polyline_distance)(le_path_o* self, size_t const& polyline_index , float * out_distance);
		
		// Always updates `numVertices` with number of vertices in polyline at `polyline_index`
		// If `numVertices` < number of vertices in polyline at `polyline_index`:
		//      + Returns true
		//      + Updates `vertices` with vertex data from polyline at `polyline_index`
		// Else
		//      + Returns false
		bool ( *get_vertices_for_polyline        )( le_path_o* self, size_t const& polyline_index, float2* vertices, size_t* numVertices );
		bool ( *get_tangents_for_polyline        )( le_path_o* self, size_t const& polyline_index, float2* tangents, size_t* numTangents );

		void ( *get_polyline_tangent_at_pos_interpolated )( le_path_o* self, size_t const& polyline_index, float normPos, float2* result );
		void ( *get_polyline_at_pos_interpolated )( le_path_o* self, size_t const& polyline_index, float normPos, float2* result );

		void ( *iterate_vertices_for_contour     )( le_path_o* self, size_t const& contour_index, contour_vertex_cb callback, void* user_data );
		void ( *iterate_quad_beziers_for_contour )( le_path_o* self, size_t const& contour_index, contour_quad_bezier_cb callback, void* user_data );
	};

	le_path_interface_t le_path_i;

	
	// Default implementations for path operations -- These expect `user_data` to be a `le_path_o*`.
	le_path_operations_interface_t le_path_operations_i;
};
// clang-format on

LE_MODULE( le_path );
LE_MODULE_LOAD_DEFAULT( le_path );

#ifdef __cplusplus

namespace le_path {
static const auto& api       = le_path_api_i;
static const auto& le_path_i            = api->le_path_i;
static const auto& le_path_operations_i = api->le_path_operations_i;
} // namespace le_path

namespace le {

class Path {

	le_path_o* self = nullptr;

  public:
	Path()
	    : self( le_path::le_path_i.create() ) {
	}

	~Path() {
		if ( self ) {
			le_path::le_path_i.destroy( self );
		}
	}

	// -- rule of 5

	Path( const Path& rhs ) { // copy constructor
		this->self = le_path::le_path_i.clone( rhs.self );
	}

	Path& operator=( Path const& rhs ) { // copy assignment constructor
		if ( self ) {
			le_path::le_path_i.destroy( self );
		}
		this->self = le_path::le_path_i.clone( rhs.self );
		return *this;
	}

	Path( Path&& rhs ) noexcept { // move constructor
		if ( self ) {
			le_path::le_path_i.destroy( self );
		}
		this->self = rhs.self;
		rhs.self   = nullptr;
	}

	Path& operator=( Path&& rhs ) noexcept { // move assignment constructor
		if ( self ) {
			le_path::le_path_i.destroy( self );
		}
		this->self = rhs.self;
		rhs.self   = nullptr;
		return *this;
	}

	// --

	Path& moveTo( float2 const& p ) {
		le_path::le_path_operations_i.move_to( self, &p );
		return *this;
	}

	Path& lineTo( float2 const& p ) {
		le_path::le_path_operations_i.line_to( self, &p );
		return *this;
	}

	Path& quadBezierTo( float2 const& c1, float2 const& p ) {
		le_path::le_path_operations_i.quad_bezier_to( self, &c1, &p );
		return *this;
	}

	Path& cubicBezierTo( float2 const& c1, float2 const& c2, float2 const& p ) {
		le_path::le_path_operations_i.cubic_bezier_to( self, &c1, &c2, &p );
		return *this;
	}

	Path& arcTo( float2 const& p, float2 const& radii, float phi, bool large_arc, bool sweep ) {
		le_path::le_path_operations_i.arc_to( self, &p, &radii, phi, large_arc, sweep );
		return *this;
	}

	Path& ellipse( float2 const& centre, float radiusX, float radiusY ) {
		le_path::le_path_i.ellipse( self, &centre, radiusX, radiusY );
		return *this;
	}

	Path& circle( float2 const& centre, float radius ) {
		le_path::le_path_i.ellipse( self, &centre, radius, radius );
		return *this;
	}

	Path& addFromSimplifiedSvg( char const* svg ) {
		le_path::le_path_i.add_from_simplified_svg( self, svg );
		return *this;
	}

	void iterate( void* user_data, le_path_operations_interface_t const* cb ) {
		le_path::le_path_i.iterate( self, user_data, cb );
	}

	void hobby() {
		le_path::le_path_i.hobby( self );
	}

	void natural_cubic() {
		le_path::le_path_i.natural_cubic( self );
	}

	void close() {
		le_path::le_path_operations_i.close( self );
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

	size_t getNumPolylines() const {
		return le_path::le_path_i.get_num_polylines( self );
	}

	size_t getNumContours() const {
		return le_path::le_path_i.get_num_contours( self );
	}

	bool copyContoursTo( le_path_o* target, uint32_t first_contour_idx = 0, uint32_t num_contours_to_copy = 1 ) {
		return le_path::le_path_i.copy_contours_to( self, target, first_contour_idx, num_contours_to_copy );
	}

	bool getVerticesForPolyline( size_t const& polyline_index, float2* vertices, size_t* numVertices ) const {
		return le_path::le_path_i.get_vertices_for_polyline( self, polyline_index, vertices, numVertices );
	}

	bool getTangentsForPolyline( size_t const& polyline_index, float2* tangents, size_t* numTangents ) const {
		return le_path::le_path_i.get_tangents_for_polyline( self, polyline_index, tangents, numTangents );
	}

	void getPolylineAtPos( size_t const& polylineIndex, float normalizedPos, float2* vertex ) const {
		le_path::le_path_i.get_polyline_at_pos_interpolated( self, polylineIndex, normalizedPos, vertex );
	}

	void getPolylineTangentAtPos( size_t const& polylineIndex, float normalizedPos, float2* vertex ) const {
		le_path::le_path_i.get_polyline_tangent_at_pos_interpolated( self, polylineIndex, normalizedPos, vertex );
	}

	bool getPolylinesTotalDistance( float* out_distance ) const {
		return le_path::le_path_i.get_polylines_total_distance( self, out_distance );
	}

	bool getPolylineDistance( size_t const& polylineIndex, float* out_distance ) const {
		return le_path::le_path_i.get_polyline_distance( self, polylineIndex, out_distance );
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

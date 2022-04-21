#include "le_2d.h"
#include "le_core.h"
#include "3rdparty/src/spooky/SpookyV2.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // vulkan clip space is from 0 to 1
#define GLM_FORCE_RIGHT_HANDED      // glTF uses right handed coordinate system, and we're following its lead.
#include "glm/glm.hpp"
#include "glm/gtc/constants.hpp" // for two_pi
#include "glm/gtc/matrix_transform.hpp"

#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <atomic>
#include <string.h> // for memset, memcpy

#include "le_renderer.h"

#include "le_pipeline_builder.h" // for pipeline creation

#include "le_tessellator.h"
#include "le_path.h"

using vec2f          = glm::vec2;
using StrokeCapType  = le_2d_api::StrokeCapType;
using StrokeJoinType = le_2d_api::StrokeJoinType;

// A drawing context, owner of all primitives.
struct le_2d_o {
	le_command_buffer_encoder_o*    encoder = nullptr;
	std::vector<le_2d_primitive_o*> primitives; // owning
};

struct node_data_t {
	// application order: t,r,s
	vec2f translation{ 0 }; // x,y
	vec2f scale{ 1 };
	float rotation_ccw = 0; // rotation in ccw around z axis, around point at translation
};

struct material_data_t {
	StrokeCapType  stroke_cap_type;  // hashed
	StrokeJoinType stroke_join_type; // hashed
	float          stroke_weight;    // hashed
	uint32_t       filled;           // hashed, used as boolean
	uint32_t       color;            // *not* hashed
};

struct circle_data_t {
	float radius;
	float tolerance;
};

struct ellipse_data_t {
	glm::vec2 radii; // radius x, radius y
	float     tolerance;
};

struct arc_data_t {
	vec2f radii; // radius x, radius y
	float angle_start_rad;
	float angle_end_rad;
	float tolerance;
};

struct path_data_t {
	le_path_o* path;
	float      tolerance;
};

struct line_data_t {
	vec2f p0;
	vec2f p1;
};

struct le_2d_primitive_o {

	enum class Type : uint32_t {
		eUndefined,
		eCircle,
		eEllipse,
		eArc,
		eLine,
		ePath,
	};

	Type type;

	union {
		circle_data_t  as_circle;
		ellipse_data_t as_ellipse;
		arc_data_t     as_arc;
		line_data_t    as_line;
		path_data_t    as_path;
		char           as_data[ 24 ];
	} data = {};

	material_data_t material;

	node_data_t node;

	uint64_t hash;
};

void le_2d_primitive_update_hash( le_2d_primitive_o* obj ) {
	// We can hash everything until `material.color` in one go, as
	// the top of the struct is tightly packed.
	//
	// Every primive is zero-initialised, meaning unused bytes in
	// `le_2d_primitive_o.data` are initialised to zero, and the hash is
	// therefore predictable.
	obj->hash = SpookyHash::Hash32( &obj->type, offsetof( le_2d_primitive_o, material.color ), 0 );
}

// ----------------------------------------------------------------------

static le_2d_o* le_2d_create( le_command_buffer_encoder_o* encoder ) {
	auto self     = new le_2d_o();
	self->encoder = encoder;
	self->primitives.reserve( 4096 / 8 );
	//	std::cout << "create 2d ctx: " << std::dec << self->id << std::flush << std::endl;
	return self;
}

// ----------------------------------------------------------------------

// Data as it is laid out in the shader ubo
struct Mvp {
	glm::mat4 mvp; // contains view projection matrix
};

// Data as it is laid out for shader attribute
struct VertexData2D {
	glm::vec2 pos;
	glm::vec2 texCoord;
};

// per-instance data for a primitive
struct PrimitiveInstanceData2D {
	glm::vec2 translation;
	glm::vec2 scale;
	float     rotation_ccw;
	uint32_t  color;
};

// ----------------------------------------------------------------------

static void generate_geometry_line( std::vector<VertexData2D>& geometry, glm::vec2 const& p0, glm::vec2 const& p1, float thickness ) {
	if ( p0 == p1 ) {
		// return empty if line cannot be generated.
		return;
	}

	geometry.reserve( geometry.size() + 6 );

	auto p_vec  = p1 - p0;
	auto p_norm = glm::normalize( p_vec );

	// Line offset: rotate p_norm 90 deg ccw
	glm::vec2 off = { -p_norm.y, p_norm.x };

	// Line thickness will be twice offset, therefore we scale offset by half line thickness

	off *= 0.5f * thickness; // scale line by thickness

	geometry.push_back( { p0 + off, { 0.f, 0.f } } );
	geometry.push_back( { p0 - off, { 0.f, 1.f } } );
	geometry.push_back( { p1 + off, { 1.f, 0.f } } );

	geometry.push_back( { p0 - off, { 0.f, 1.f } } );
	geometry.push_back( { p1 - off, { 1.f, 1.f } } );
	geometry.push_back( { p1 + off, { 1.f, 0.f } } );
}

// ----------------------------------------------------------------------

static void generate_geometry_outline_arc( std::vector<VertexData2D>& geometry, float angle_start_rad, float angle_end_rad, glm::vec2 radii, float thickness, float tolerance ) {

	if ( std::numeric_limits<float>::epsilon() > angle_end_rad - angle_start_rad ) {
		return;
	}

	// ---------| invariant: angle difference is not too close to zero

	float     t = angle_start_rad;
	glm::vec2 n{ cosf( t ), sinf( t ) };

	float const offset = thickness * 0.5f;

	glm::vec2 p1_perp = glm::normalize( glm::vec2{ radii.y, radii.x } * -n );

	glm::vec2 p0_far  = n * radii + p1_perp * offset;
	glm::vec2 p0_near = n * radii - p1_perp * offset;

	for ( uint32_t i = 1; i != 1000; ++i ) {

		// FIXME: angle_offset calculation is currently based on
		// fantasy- matematics. Pin down the correct analytic solution by finding the
		// correct curvature for the ellipse offset segment on the outide.
		//

		float r_length = glm::dot( glm::vec2{ fabsf( n.x ), fabsf( n.y ) }, radii + glm::abs( p1_perp * offset ) );

		float angle_offset = acosf( 1.f - ( tolerance / r_length ) );
		t                  = std::min( t + angle_offset, angle_end_rad );
		n                  = { cosf( t ), sinf( t ) };

		// p1_perp is a normalized vector which is perpendicular to the tangent
		// of the ellipse at point p1.
		//
		// The tangent is the first derivative of the ellipse in parametric notation:
		//
		// e(t) : {r.x * cos(t), r.y * sin(t)}
		// e(t'): {r.x * -sin(t), r.y * cos(t)} // tangent is first derivative
		//
		// now rotate this 90 deg ccw:
		//
		// {-r.y*cos(t), r.x*-sin(t)} // we can invert sign to remove negative if we want
		//
		// `offset` is how far we want to move outwards/inwards at the ellipse point p1,
		// in direction p1_perp. So that p1_perp has unit length, we must normalize it.
		//

		p1_perp = glm::normalize( glm::vec2{ radii.y, radii.x } * -n );

		glm::vec2 p1_far  = n * radii + p1_perp * offset;
		glm::vec2 p1_near = n * radii - p1_perp * offset;

		geometry.push_back( { p0_far, { 0.f, 0.f } } );
		geometry.push_back( { p0_near, { 0.f, 1.f } } );
		geometry.push_back( { p1_far, { 1.f, 0.f } } );

		geometry.push_back( { p0_near, { 0.f, 1.f } } );
		geometry.push_back( { p1_near, { 1.f, 1.f } } );
		geometry.push_back( { p1_far, { 1.f, 0.f } } );

		std::swap( p0_far, p1_far );
		std::swap( p0_near, p1_near );

		if ( t >= angle_end_rad ) {
			break;
		}
	}
}

// ----------------------------------------------------------------------

static void generate_geometry_ellipse( std::vector<VertexData2D>& geometry, float angle_start_rad, float angle_end_rad, glm::vec2 radii, float tolerance ) {

	// --------| invariant: It should be possible to generate circle geometry.

	VertexData2D v_c{};
	v_c.pos      = { 0.f, 0.f };
	v_c.texCoord = { 0.5, 0.5 };

	float     arc_angle = angle_start_rad;
	glm::vec2 n{ cosf( arc_angle ), sinf( arc_angle ) };

	VertexData2D v{};
	v.pos      = radii * n;
	v.texCoord = glm::vec2{ 0.5, 0.5 } + 0.5f * n;

	for ( int i = 0; i != 1000; ++i ) {

		geometry.push_back( v_c ); // centre vertex
		geometry.push_back( v );   // previous vertex

		/* The maths for this are based on the intuition that an ellipse is
		 * a scaled circle.
		 */
		float r_length = glm::dot( glm::vec2{ fabsf( n.x ), fabsf( n.y ) }, radii );

		float angle_offset = acosf( 1.f - ( tolerance / r_length ) );
		arc_angle          = std::min( arc_angle + angle_offset, angle_end_rad );
		n                  = { cosf( arc_angle ), sinf( arc_angle ) };

		v.pos      = radii * n;
		v.texCoord = glm::vec2{ 0.5, 0.5 } + 0.5f * n;

		geometry.push_back( v ); // current vertex

		if ( arc_angle >= angle_end_rad ) {
			break;
		}
	}
}

// clang-format off
le_path_api::stroke_attribute_t::LineJoinType to_path_enum(StrokeJoinType const & t){
	switch(t){
	case (StrokeJoinType::eStrokeJoinMiter) : return le_path_api::stroke_attribute_t::LineJoinType::eLineJoinMiter;
	case (StrokeJoinType::eStrokeJoinBevel) : return le_path_api::stroke_attribute_t::LineJoinType::eLineJoinBevel;
	case (StrokeJoinType::eStrokeJoinRound) : return le_path_api::stroke_attribute_t::LineJoinType::eLineJoinRound;		
	}
assert(false);
return le_path_api::stroke_attribute_t::LineJoinType::eLineJoinRound; // unreachable
}
le_path_api::stroke_attribute_t::LineCapType to_path_enum(StrokeCapType const & t){
	switch(t){
	case (StrokeCapType::eStrokeCapButt)   : return le_path_api::stroke_attribute_t::LineCapType::eLineCapButt;
	case (StrokeCapType::eStrokeCapSquare) : return le_path_api::stroke_attribute_t::LineCapType::eLineCapSquare;
	case (StrokeCapType::eStrokeCapRound)  : return le_path_api::stroke_attribute_t::LineCapType::eLineCapRound;		
	}
assert(false);
return le_path_api::stroke_attribute_t::LineCapType::eLineCapRound; // unreachable
}
// clang-format on

// ----------------------------------------------------------------------

static void generate_geometry_outline_path( std::vector<VertexData2D>& geometry, le_path_o* path, float tolerance, material_data_t const& material ) {

	using namespace le_path;

	float stroke_weight = material.stroke_weight;

	if ( stroke_weight < 2.f ) {

		le_path_i.flatten( path, tolerance );

		size_t                 num_used_vertices = 1024;
		std::vector<glm::vec2> vertices( num_used_vertices );

		size_t const num_polylines = le_path_i.get_num_polylines( path );
		for ( size_t i = 0; i != num_polylines; ++i ) {
			num_used_vertices = vertices.size();
			while ( false == le_path_i.get_vertices_for_polyline( path, i, vertices.data(), &num_used_vertices ) ) {
				vertices.resize( num_used_vertices );
			}
			auto const* p_prev = vertices.data();
			for ( size_t j = 1; j != num_used_vertices; ++j ) {
				glm::vec2 const* p_cur = vertices.data() + j;
				generate_geometry_line( geometry, *p_prev, *p_cur, stroke_weight );
				p_prev = p_cur;
			}
		}
	} else {

		size_t const num_contours = le_path_i.get_num_contours( path );

		size_t WHICH_TESSELLATOR = 3;

		switch ( WHICH_TESSELLATOR ) {

		case 0: {
			std::vector<glm::vec2> vertices_l( 1024 );
			std::vector<glm::vec2> vertices_r( 1024 );

			for ( size_t i = 0; i != num_contours; ++i ) {

				size_t num_vertices_l = vertices_l.size();
				size_t num_vertices_r = vertices_r.size();

				glm::vec2* v_l                   = vertices_l.data();
				glm::vec2* v_r                   = vertices_r.data();
				bool       vertices_large_enough = le_path_i.generate_offset_outline_for_contour( path, i, stroke_weight, tolerance, v_l, &num_vertices_l, v_r, &num_vertices_r );

				if ( !vertices_large_enough ) {
					vertices_l.resize( num_vertices_l + 1 );
					vertices_r.resize( num_vertices_r + 1 );
					le_path_i.generate_offset_outline_for_contour( path, i, stroke_weight, tolerance, vertices_l.data(), &num_vertices_l, vertices_r.data(), &num_vertices_r );
				}

				// reverse elements
				std::reverse( vertices_r.begin(), vertices_r.begin() + int64_t( num_vertices_r ) );

				std::vector<glm::vec2> all_vertices;
				all_vertices.insert( all_vertices.end(), vertices_l.begin(), vertices_l.begin() + int64_t( num_vertices_l ) );
				all_vertices.insert( all_vertices.end(), vertices_r.begin(), vertices_r.begin() + int64_t( num_vertices_r ) );
				all_vertices.push_back( all_vertices.front() );

				auto p_prev = all_vertices.front();

				for ( size_t j = 1; j != all_vertices.size(); ++j ) {
					glm::vec2 const p_cur = all_vertices[ j ];
					generate_geometry_line( geometry, p_prev, p_cur, 2.f );
					p_prev = p_cur;
				}
			}
		} break;
		case 1: {
			using namespace le_tessellator;
			auto tess = le_tessellator_i.create();
			le_tessellator_i.set_options( tess, le_tessellator::Options::eWindingOdd );
			//			le_tessellator_i.set_options( tess, le_tessellator::Options::bitConstrainedDelaunayTriangulation );
			//			le_tessellator_i.set_options( tess, le_tessellator::Options::bitUseEarcutTessellator );

			std::vector<glm::vec2> vertices_l( 1024 );
			std::vector<glm::vec2> vertices_r( 1024 );

			for ( size_t i = 0; i != num_contours; ++i ) {

				size_t num_vertices_l = vertices_l.size();
				size_t num_vertices_r = vertices_r.size();

				glm::vec2* v_l = vertices_l.data();
				glm::vec2* v_r = vertices_r.data();

				bool vertices_large_enough = le_path_i.generate_offset_outline_for_contour( path, i, stroke_weight, tolerance, v_l, &num_vertices_l, v_r, &num_vertices_r );

				if ( !vertices_large_enough ) {
					vertices_l.resize( num_vertices_l + 1 );
					vertices_r.resize( num_vertices_r + 1 );
					le_path_i.generate_offset_outline_for_contour( path, i, stroke_weight, tolerance, vertices_l.data(), &num_vertices_l, vertices_r.data(), &num_vertices_r );
				}

				// reverse elements
				std::reverse( vertices_r.begin(), vertices_r.begin() + int64_t( num_vertices_r ) );

				std::vector<glm::vec2> all_vertices;
				all_vertices.insert( all_vertices.end(), vertices_l.begin(), vertices_l.begin() + int64_t( num_vertices_l ) );
				all_vertices.insert( all_vertices.end(), vertices_r.begin(), vertices_r.begin() + int64_t( num_vertices_r ) );

				if ( !all_vertices.empty() ) {
					all_vertices.push_back( all_vertices.front() );
					le_tessellator_i.add_polyline( tess, all_vertices.data(), all_vertices.size() );
				}
			}

			le_tessellator_i.tessellate( tess );

			le_tessellator_api::IndexType const* indices;
			size_t                               num_indices = 0;
			glm::vec2 const*                     vertices;
			size_t                               num_vertices = 0;

			le_tessellator_i.get_indices( tess, &indices, &num_indices );
			le_tessellator_i.get_vertices( tess, &vertices, &num_vertices );

			// TODO: what do we want to set for tex coordinate?

			for ( size_t i = 0; i + 2 < num_indices; ) {
				geometry.push_back( { vertices[ indices[ i++ ] ], { 1, 0 } } );
				geometry.push_back( { vertices[ indices[ i++ ] ], { 0, 1 } } );
				geometry.push_back( { vertices[ indices[ i++ ] ], { 1, 1 } } );
			}

			le_tessellator_i.destroy( tess );
		} break;
		case 2: {
			std::vector<glm::vec2> vertices_l( 1024 );
			std::vector<glm::vec2> vertices_r( 1024 );

			for ( size_t i = 0; i != num_contours; ++i ) {

				size_t num_vertices_l = vertices_l.size();
				size_t num_vertices_r = vertices_r.size();

				glm::vec2* v_l = vertices_l.data();
				glm::vec2* v_r = vertices_r.data();

				bool vertices_large_enough = le_path_i.generate_offset_outline_for_contour( path, i, stroke_weight, tolerance, v_l, &num_vertices_l, v_r, &num_vertices_r );

				if ( !vertices_large_enough ) {
					vertices_l.resize( num_vertices_l + 1 );
					vertices_r.resize( num_vertices_r + 1 );
					v_l = vertices_l.data();
					v_r = vertices_r.data();
					le_path_i.generate_offset_outline_for_contour( path, i, stroke_weight, tolerance, v_l, &num_vertices_l, v_r, &num_vertices_r );
				}

				glm::vec2 const* l_prev = v_l;
				glm::vec2 const* r_prev = v_r;

				glm::vec2 const* l = l_prev + 1;
				glm::vec2 const* r = r_prev + 1;

				glm::vec2 const* const l_end = vertices_l.data() + num_vertices_l;
				glm::vec2 const* const r_end = vertices_r.data() + num_vertices_r;

				for ( ; ( l != l_end || r != r_end ); ) {

					if ( r != r_end ) {

						geometry.push_back( { *l_prev, { 1, 0 } } );
						geometry.push_back( { *r_prev, { 0, 1 } } );
						geometry.push_back( { *r, { 1, 1 } } );

						r_prev = r;
						r++;
					}

					if ( l != l_end ) {

						geometry.push_back( { *l_prev, { 1, 0 } } );
						geometry.push_back( { *r_prev, { 0, 1 } } );
						geometry.push_back( { *l, { 1, 1 } } );

						l_prev = l;
						l++;
					}
				}
			}
		} break;
		case 3: {
			std::vector<glm::vec2> vertices( 1024 );

			for ( size_t i = 0; i != num_contours; ++i ) {

				size_t num_vertices = vertices.size();

				glm::vec2* v_data = vertices.data();

				le_path_api::stroke_attribute_t stroke_attribs{};
				stroke_attribs.width          = stroke_weight;
				stroke_attribs.tolerance      = tolerance;
				stroke_attribs.line_join_type = to_path_enum( material.stroke_join_type );
				stroke_attribs.line_cap_type  = to_path_enum( material.stroke_cap_type );

				while ( false == le_path_i.tessellate_thick_contour( path, i, &stroke_attribs, v_data, &num_vertices ) ) {
					vertices.resize( num_vertices );
					v_data = vertices.data();
				}

				glm::vec2 const*       v     = v_data;
				glm::vec2 const* const v_end = v_data + num_vertices;

				assert( num_vertices % 3 == 0 ); // vertices count must be divisible by 3

				for ( ; ( v != v_end ); ) {
					geometry.push_back( { *v++, { 1, 0 } } );
					geometry.push_back( { *v++, { 0, 1 } } );
					geometry.push_back( { *v++, { 1, 1 } } );
				}
			}
		} break;
		}
	}
}

// Generates triangles by tessellating what's contained within path
static void generate_geometry_path( std::vector<VertexData2D>& geometry, le_path_o* path, float tolerance ) {

	using namespace le_path;
	using namespace le_tessellator;

	le_path_i.flatten( path, tolerance );

	size_t const num_polylines = le_path_i.get_num_polylines( path );

	auto tess = le_tessellator_i.create();
	le_tessellator_i.set_options( tess, le_tessellator::Options::eWindingOdd );
	// le_tessellator_i.set_options( tess, le_tessellator::Options::bitConstrainedDelaunayTriangulation );
	// le_tessellator_i.set_options( tess, le_tessellator::Options::bitUseEarcutTessellator );

	size_t                 num_used_vertices = 1024;
	std::vector<glm::vec2> line_vertices( num_used_vertices );

	for ( size_t i = 0; i != num_polylines; ++i ) {

		num_used_vertices = line_vertices.size();

		while ( false == le_path_i.get_vertices_for_polyline( path, i, line_vertices.data(), &num_used_vertices ) ) {
			line_vertices.resize( num_used_vertices );
		}

		le_tessellator_i.add_polyline( tess, line_vertices.data(), num_used_vertices );
	}

	le_tessellator_i.tessellate( tess );

	le_tessellator_api::IndexType const* indices;
	size_t                               num_indices = 0;
	glm::vec2 const*                     vertices;
	size_t                               num_vertices = 0;

	le_tessellator_i.get_indices( tess, &indices, &num_indices );
	le_tessellator_i.get_vertices( tess, &vertices, &num_vertices );

	// TODO: what do we want to set for tex coordinate?

	for ( size_t i = 0; i + 2 < num_indices; ) {
		geometry.push_back( { vertices[ indices[ i++ ] ], { 0, 0 } } );
		geometry.push_back( { vertices[ indices[ i++ ] ], { 0, 0 } } );
		geometry.push_back( { vertices[ indices[ i++ ] ], { 0, 0 } } );
	}

	le_tessellator_i.destroy( tess );
}

// ----------------------------------------------------------------------

static void generate_geometry_for_primitive( le_2d_primitive_o* p, std::vector<VertexData2D>& geometry ) {

	switch ( p->type ) {
	case le_2d_primitive_o::Type::eLine: {
		// generate geometry for line
		auto const& line = p->data.as_line;

		generate_geometry_line( geometry, line.p0, line.p1, p->material.stroke_weight );

	} break;
	case le_2d_primitive_o::Type::eCircle: {

		auto const& circle = p->data.as_circle;

		if ( p->material.filled ) {
			generate_geometry_ellipse( geometry, 0, glm::two_pi<float>(), { circle.radius, circle.radius }, circle.tolerance );
		} else {
			generate_geometry_outline_arc( geometry, 0, glm::two_pi<float>(), { circle.radius, circle.radius }, p->material.stroke_weight, circle.tolerance );
		}

	} break;
	case le_2d_primitive_o::Type::eEllipse: {
		auto const& ellipse = p->data.as_ellipse;
		if ( p->material.filled ) {
			generate_geometry_ellipse( geometry, 0, glm::two_pi<float>(), ellipse.radii, ellipse.tolerance );
		} else {
			generate_geometry_outline_arc( geometry, 0, glm::two_pi<float>(), ellipse.radii, p->material.stroke_weight, ellipse.tolerance );
		}
	} break;
	case le_2d_primitive_o::Type::eArc: {
		auto const& arc = p->data.as_arc;
		if ( p->material.filled ) {
			generate_geometry_ellipse( geometry, arc.angle_start_rad, arc.angle_end_rad, arc.radii, arc.tolerance );
		} else {
			generate_geometry_outline_arc( geometry, arc.angle_start_rad, arc.angle_end_rad, arc.radii, p->material.stroke_weight, arc.tolerance );
		}
	} break;
	case le_2d_primitive_o::Type::ePath: {
		auto const& path = p->data.as_path;
		if ( p->material.filled ) {
			generate_geometry_path( geometry, path.path, path.tolerance );
		} else {
			generate_geometry_outline_path( geometry, path.path, path.tolerance, p->material );
		}
	} break;
	case le_2d_primitive_o::Type::eUndefined:
		// noop
		break;
	}
}

// ----------------------------------------------------------------------
// internal method, only triggered if le_2d is destroyed.
static void le_2d_draw_primitives( le_2d_o* self ) {

	/* We might want to do some sorting, and optimising here
	 * Sort by pipeline for example. Also, issue draw commands
	 * as instanced draws if more than three of the same prims
	 * are issued.
	 */

	le::Encoder encoder{ self->encoder };
	auto*       pm = encoder.getPipelineManager();

	static auto vert = LeShaderModuleBuilder( pm ).setSourceFilePath( "./resources/shaders/2d_primitives.vert" ).setShaderStage( le::ShaderStage::eVertex ).setHandle( LE_SHADER_MODULE_HANDLE( "2d_primitives_shader_vert" ) ).build();
	static auto frag = LeShaderModuleBuilder( pm ).setSourceFilePath( "./resources/shaders/2d_primitives.frag" ).setShaderStage( le::ShaderStage::eFragment ).setHandle( LE_SHADER_MODULE_HANDLE( "2d_primitives_shader_frag" ) ).build();

	// clang-format off
	static auto pipeline =
	    LeGraphicsPipelineBuilder( pm )
	        .addShaderStage( vert )
	        .addShaderStage( frag )
	        .withAttributeBindingState()
	            .addBinding( sizeof( VertexData2D ) )
	                .setInputRate( le_vertex_input_rate::ePerVertex )
	                .addAttribute( offsetof( VertexData2D, pos ), le_num_type::eF32, 2 )
	                .addAttribute( offsetof( VertexData2D, texCoord ), le_num_type::eF32, 2 )
	            .end()
                .addBinding(sizeof(PrimitiveInstanceData2D))
                    .setInputRate(le_vertex_input_rate::ePerInstance)
                    .addAttribute( offsetof( PrimitiveInstanceData2D, translation), le_num_type::eF32, 2)
                    .addAttribute( offsetof( PrimitiveInstanceData2D, scale), le_num_type::eF32, 2)
                    .addAttribute( offsetof( PrimitiveInstanceData2D, rotation_ccw), le_num_type::eF32, 1 )
                    .addAttribute( offsetof( PrimitiveInstanceData2D, color), le_num_type::eU32, 1 )
                .end()
	        .end()
			.withRasterizationState()
//				.setPolygonMode(le::PolygonMode::eLine)
//				.setCullMode(le::CullModeFlagBits::eBack)
//				.setFrontFace(le::FrontFace::eCounterClockwise)
			.end()
	        .build();
	// clang-format on

	// Note: we can use DepthCompareOp::NotEqual to prevent overdraw for individual paths.
	// This is useful for paths which self-overlap. If we want to draw such paths with
	// transparency or blend them onto the screen, we would not like to see the self-overlap.
	//
	// We must then make sure though to monotonously increase a depth uniform for each path (layer)
	// drawn, otherwise no overlap at all will be drawn.

	encoder
	    .bindGraphicsPipeline( pipeline )
	    .setLineWidth( 1.0f );

	// Calculate view projection matrix
	// for 2D, this will be a simple orthographic projection, which means that the view matrix
	// (camera matrix) will be the identity, and does not need to be factored in.

	auto extents          = encoder.getRenderpassExtent();
	auto ortho_projection = glm::ortho( 0.f, float( extents.width ), 0.f, float( extents.height ) );

	{
		// set a negative height for viewport so that +Y goes up, rather than down.

		le::Viewport viewports[ 2 ] = {
		    { 0.f, float( extents.height ), float( extents.width ), -float( extents.height ), 0.f, 1.f },
		    { 0.f, 0, float( extents.width ), float( extents.height ), 0.f, 1.f },
		};

		encoder.setViewports( 0, 1, viewports + 1 );
	}

	encoder
	    .setArgumentData( LE_ARGUMENT_NAME( "Mvp" ), &ortho_projection, sizeof( glm::mat4 ) );

	// Update sort key for all primitives

	for ( auto& p : self->primitives ) {
		le_2d_primitive_update_hash( p );
	}

	// Now, we do essentially run-length encoding.

	std::vector<std::vector<VertexData2D>> geometry_data;
	std::vector<PrimitiveInstanceData2D>   per_instance_data;
	per_instance_data.reserve( self->primitives.size() );

	struct InstancedDraw {
		uint32_t geometry_data_index; // which geometry
		uint32_t instance_data_index; // first index for instance data
		uint32_t instance_count;      // number of instances with same geometry
	};

	std::vector<InstancedDraw> instanced_draws;

	uint64_t previous_hash = 0;

	for ( auto const& p : self->primitives ) {

		if ( instanced_draws.empty() ) {

			std::vector<VertexData2D> geometry;

			generate_geometry_for_primitive( p, geometry );
			geometry_data.emplace_back( geometry );

			PrimitiveInstanceData2D instance_data{};
			instance_data.color        = p->material.color;
			instance_data.rotation_ccw = p->node.rotation_ccw;
			instance_data.scale        = p->node.scale;
			instance_data.translation  = p->node.translation;

			per_instance_data.emplace_back( instance_data );

			instanced_draws.push_back( { 0, 0, 1 } );
			previous_hash = p->hash;
			continue;
		}

		uint32_t hash = p->hash;

		if ( hash != previous_hash ) {

			instanced_draws.push_back( { uint32_t( geometry_data.size() ),
			                             uint32_t( per_instance_data.size() ),
			                             1 } );
			// geometry has changed.

			std::vector<VertexData2D> geometry;

			generate_geometry_for_primitive( p, geometry );
			geometry_data.emplace_back( geometry );

			PrimitiveInstanceData2D instance_data{};
			instance_data.color        = p->material.color;
			instance_data.rotation_ccw = p->node.rotation_ccw;
			instance_data.scale        = p->node.scale;
			instance_data.translation  = p->node.translation;

			per_instance_data.emplace_back( instance_data );

			previous_hash = hash;
		} else {

			PrimitiveInstanceData2D instance_data{};
			instance_data.color        = p->material.color;
			instance_data.rotation_ccw = p->node.rotation_ccw;
			instance_data.scale        = p->node.scale;
			instance_data.translation  = p->node.translation;

			per_instance_data.emplace_back( instance_data );

			instanced_draws.back().instance_count++;
		}
	}

	for ( auto& d : instanced_draws ) {

		auto& geom = geometry_data[ d.geometry_data_index ];

		encoder
		    .setVertexData( geom.data(), sizeof( VertexData2D ) * geom.size(), 0 )
		    .setVertexData( per_instance_data.data() + d.instance_data_index, d.instance_count * sizeof( PrimitiveInstanceData2D ), 1 )
		    .draw( uint32_t( geom.size() ), d.instance_count );
	}
}

// ----------------------------------------------------------------------

static void le_2d_destroy( le_2d_o* self ) {

	// We draw all primtives which have been attached to this 2d context.

	le_2d_draw_primitives( self );

	// Clean up

	for ( auto& p : self->primitives ) {

		// Most primitives are POD types, but some might own
		// their own heap-allocated objects which we must clean up.

		switch ( p->type ) {
		case ( le_2d_primitive_o::Type::ePath ):
			if ( p->data.as_path.path ) {
				le_path::le_path_i.destroy( p->data.as_path.path );
			}
			break;
		default:
			break;
		}

		delete p;
	}

	delete self;
}

// ----------------------------------------------------------------------

static le_2d_primitive_o* le_2d_allocate_primitive( le_2d_o* self ) {
	le_2d_primitive_o* p = new le_2d_primitive_o();

	p->hash              = 0;
	p->node.scale        = vec2f{ 1 };
	p->node.translation  = vec2f{ 0 };
	p->node.rotation_ccw = 0;

	p->material.color            = 0xffffffff;
	p->material.stroke_weight    = 1.f;
	p->material.filled           = false;
	p->material.stroke_cap_type  = StrokeCapType::eStrokeCapRound;
	p->material.stroke_join_type = StrokeJoinType::eStrokeJoinRound;

	memset( p->data.as_data, 0, sizeof( p->data ) );

	self->primitives.push_back( p );

	return p;
}

// ----------------------------------------------------------------------

static le_2d_primitive_o* le_2d_primitive_create_circle( le_2d_o* context ) {
	auto p = le_2d_allocate_primitive( context );

	p->type   = le_2d_primitive_o::Type::eCircle;
	auto& obj = p->data.as_circle;

	obj.radius    = 100.f;
	obj.tolerance = 0.5f;

	return p;
}

// ----------------------------------------------------------------------

static le_2d_primitive_o* le_2d_primitive_create_ellipse( le_2d_o* context ) {
	auto p = le_2d_allocate_primitive( context );

	p->type = le_2d_primitive_o::Type::eEllipse;

	auto& obj = p->data.as_ellipse;

	obj.radii     = { 0.f, 0.f };
	obj.tolerance = 0.5;

	return p;
}

// ----------------------------------------------------------------------

static le_2d_primitive_o* le_2d_primitive_create_arc( le_2d_o* context ) {
	auto p = le_2d_allocate_primitive( context );

	p->type = le_2d_primitive_o::Type::eArc;

	auto& obj = p->data.as_arc;

	obj.radii           = { 0.f, 0.f };
	obj.tolerance       = 0.5;
	obj.angle_start_rad = 0;
	obj.angle_end_rad   = glm::two_pi<float>();

	p->material.stroke_weight = 1.f;

	return p;
}

// ----------------------------------------------------------------------

static le_2d_primitive_o* le_2d_primitive_create_line( le_2d_o* context ) {
	auto p = le_2d_allocate_primitive( context );

	p->type   = le_2d_primitive_o::Type::eLine;
	auto& obj = p->data.as_line;

	obj.p0                    = {};
	obj.p1                    = {};
	p->material.stroke_weight = 1.f;

	return p;
}

// ----------------------------------------------------------------------

static le_2d_primitive_o* le_2d_primitive_create_path( le_2d_o* context ) {
	auto p = le_2d_allocate_primitive( context );

	p->type   = le_2d_primitive_o::Type::ePath;
	auto& obj = p->data.as_path;

	obj.path      = le_path::le_path_i.create();
	obj.tolerance = 0.5f;

	p->material.stroke_weight = 1.f;
	return p;
}

// ----------------------------------------------------------------------

static void le_2d_primitive_path_move_to( le_2d_primitive_o* p, vec2f const* pos ) {
	assert( p->type == le_2d_primitive_o::Type::ePath );
	auto& obj = p->data.as_path;
	le_path::le_path_i.move_to( obj.path, pos );
}

// ----------------------------------------------------------------------

static void le_2d_primitive_path_line_to( le_2d_primitive_o* p, vec2f const* pos ) {
	assert( p->type == le_2d_primitive_o::Type::ePath );
	auto& obj = p->data.as_path;
	le_path::le_path_i.line_to( obj.path, pos );
}

// ----------------------------------------------------------------------

static void le_2d_primitive_path_close( le_2d_primitive_o* p ) {
	assert( p->type == le_2d_primitive_o::Type::ePath );
	auto& obj = p->data.as_path;
	le_path::le_path_i.close( obj.path );
}
// ----------------------------------------------------------------------

static void le_2d_primitive_path_cubic_bezier_to( le_2d_primitive_o* p, vec2f const* pos, vec2f const* c1, vec2f const* c2 ) {
	assert( p->type == le_2d_primitive_o::Type::ePath );
	auto& obj = p->data.as_path;
	le_path::le_path_i.cubic_bezier_to( obj.path, pos, c1, c2 );
}

// ----------------------------------------------------------------------

static void le_2d_primitive_path_quad_bezier_to( le_2d_primitive_o* p, vec2f const* pos, vec2f const* c1 ) {
	assert( p->type == le_2d_primitive_o::Type::ePath );
	auto& obj = p->data.as_path;
	le_path::le_path_i.quad_bezier_to( obj.path, pos, c1 );
}

// ----------------------------------------------------------------------

static void le_2d_primitive_path_arc_to( le_2d_primitive_o* p, vec2f const* pos, vec2f const* radii, float phi, bool large_arc, bool sweep ) {
	assert( p->type == le_2d_primitive_o::Type::ePath );
	auto& obj = p->data.as_path;
	le_path::le_path_i.arc_to( obj.path, pos, radii, phi, large_arc, sweep );
}

// ----------------------------------------------------------------------

static void le_2d_primitive_path_hobby( le_2d_primitive_o* p ) {
	assert( p->type == le_2d_primitive_o::Type::ePath );
	auto& obj = p->data.as_path;
	le_path::le_path_i.hobby( obj.path );
}

// ----------------------------------------------------------------------

static void le_2d_primitive_path_ellipse( le_2d_primitive_o* p, vec2f const* centre, float r_x, float r_y ) {
	assert( p->type == le_2d_primitive_o::Type::ePath );
	auto& obj = p->data.as_path;
	le_path::le_path_i.ellipse( obj.path, centre, r_x, r_y );
}

// ----------------------------------------------------------------------

static void le_2d_primitive_path_add_from_simplified_svg( le_2d_primitive_o* p, char const* svg ) {
	assert( p->type == le_2d_primitive_o::Type::ePath );
	auto& obj = p->data.as_path;
	le_path::le_path_i.add_from_simplified_svg( obj.path, svg );
}

// ----------------------------------------------------------------------

static void le_2d_primitive_path_set_tolerance( le_2d_primitive_o* p, float tolerance ) {
	assert( p->type == le_2d_primitive_o::Type::ePath );
	auto& obj     = p->data.as_path;
	obj.tolerance = tolerance;
}

// ----------------------------------------------------------------------

static void le_2d_primitive_set_node_position( le_2d_primitive_o* p, vec2f const* pos ) {
	p->node.translation = *pos;
}

static void le_2d_primitive_set_stroke_weight( le_2d_primitive_o* p, float weight ) {
	p->material.stroke_weight = weight;
}

static void le_2d_primitive_set_stroke_cap_type( le_2d_primitive_o* p, StrokeCapType cap_type ) {
	p->material.stroke_cap_type = cap_type;
}

static void le_2d_primitive_set_stroke_join_type( le_2d_primitive_o* p, StrokeJoinType join_type ) {
	p->material.stroke_join_type = join_type;
}

static void le_2d_primitive_set_filled( le_2d_primitive_o* p, bool filled ) {
	p->material.filled = filled;
}

static void le_2d_primitive_set_color( le_2d_primitive_o* p, uint32_t r8g8b8a8_color ) {
	p->material.color = r8g8b8a8_color;
}

#define SETTER_IMPLEMENT( prim_type, field_type, field_name )                                                   \
	static void le_2d_primitive_##prim_type##_set_##field_name( le_2d_primitive_o* p, field_type field_name ) { \
		p->data.as_##prim_type.field_name = field_name;                                                         \
	}

#define SETTER_IMPLEMENT_CPY( prim_type, field_type, field_name )                                               \
	static void le_2d_primitive_##prim_type##_set_##field_name( le_2d_primitive_o* p, field_type field_name ) { \
		p->data.as_##prim_type.field_name = *field_name;                                                        \
	}

SETTER_IMPLEMENT( circle, float, radius );
SETTER_IMPLEMENT( circle, float, tolerance );

SETTER_IMPLEMENT_CPY( ellipse, vec2f const*, radii );
SETTER_IMPLEMENT( ellipse, float, tolerance );

SETTER_IMPLEMENT_CPY( arc, vec2f const*, radii );
SETTER_IMPLEMENT( arc, float, tolerance );

SETTER_IMPLEMENT( arc, float, angle_start_rad );
SETTER_IMPLEMENT( arc, float, angle_end_rad );

SETTER_IMPLEMENT_CPY( line, vec2f const*, p0 );
SETTER_IMPLEMENT_CPY( line, vec2f const*, p1 );

#undef SETTER_IMPLEMENT

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_2d, api ) {
	auto& le_2d_i = static_cast<le_2d_api*>( api )->le_2d_i;

	le_2d_i.create  = le_2d_create;
	le_2d_i.destroy = le_2d_destroy;

	auto& le_2d_primitive_i = static_cast<le_2d_api*>( api )->le_2d_primitive_i;

#define SET_PRIMITIVE_FPTR( prim_type, field_name ) \
	le_2d_primitive_i.prim_type##_set_##field_name = le_2d_primitive_##prim_type##_set_##field_name

	SET_PRIMITIVE_FPTR( circle, radius );
	SET_PRIMITIVE_FPTR( circle, tolerance );

	SET_PRIMITIVE_FPTR( ellipse, radii );
	SET_PRIMITIVE_FPTR( ellipse, tolerance );

	SET_PRIMITIVE_FPTR( arc, radii );
	SET_PRIMITIVE_FPTR( arc, tolerance );
	SET_PRIMITIVE_FPTR( arc, angle_start_rad );
	SET_PRIMITIVE_FPTR( arc, angle_end_rad );

	SET_PRIMITIVE_FPTR( line, p0 );
	SET_PRIMITIVE_FPTR( line, p1 );

#undef SET_PRIMITIVE_FPTR

	le_2d_primitive_i.path_move_to                 = le_2d_primitive_path_move_to;
	le_2d_primitive_i.path_line_to                 = le_2d_primitive_path_line_to;
	le_2d_primitive_i.path_quad_bezier_to          = le_2d_primitive_path_quad_bezier_to;
	le_2d_primitive_i.path_cubic_bezier_to         = le_2d_primitive_path_cubic_bezier_to;
	le_2d_primitive_i.path_arc_to                  = le_2d_primitive_path_arc_to;
	le_2d_primitive_i.path_ellipse                 = le_2d_primitive_path_ellipse;
	le_2d_primitive_i.path_add_from_simplified_svg = le_2d_primitive_path_add_from_simplified_svg;
	le_2d_primitive_i.path_set_tolerance           = le_2d_primitive_path_set_tolerance;
	le_2d_primitive_i.path_close                   = le_2d_primitive_path_close;
	le_2d_primitive_i.path_hobby                   = le_2d_primitive_path_hobby;
	le_2d_primitive_i.create_path                  = le_2d_primitive_create_path;

	le_2d_primitive_i.create_arc     = le_2d_primitive_create_arc;
	le_2d_primitive_i.create_ellipse = le_2d_primitive_create_ellipse;
	le_2d_primitive_i.create_circle  = le_2d_primitive_create_circle;
	le_2d_primitive_i.create_line    = le_2d_primitive_create_line;

	le_2d_primitive_i.set_node_position    = le_2d_primitive_set_node_position;
	le_2d_primitive_i.set_stroke_weight    = le_2d_primitive_set_stroke_weight;
	le_2d_primitive_i.set_stroke_cap_type  = le_2d_primitive_set_stroke_cap_type;
	le_2d_primitive_i.set_stroke_join_type = le_2d_primitive_set_stroke_join_type;

	le_2d_primitive_i.set_filled = le_2d_primitive_set_filled;
	le_2d_primitive_i.set_color  = le_2d_primitive_set_color;
}

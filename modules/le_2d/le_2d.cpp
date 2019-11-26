#include "le_2d.h"
#include "pal_api_loader/ApiRegistry.hpp"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // vulkan clip space is from 0 to 1
#define GLM_FORCE_RIGHT_HANDED      // glTF uses right handed coordinate system, and we're following its lead.
#include "glm.hpp"
#include "glm/gtc/constants.hpp" // for two_pi
#include "glm/gtc/matrix_transform.hpp"

#include <iostream>
#include <iomanip>
#include <vector>

#include "le_renderer/le_renderer.h"

#include "le_backend_vk/le_backend_vk.h"             // for shader module creation
#include "le_pipeline_builder/le_pipeline_builder.h" // for pipeline creation

#include "le_tessellator/le_tessellator.h"
#include "le_path/le_path.h"

using vec2f = glm::vec2;

// A drawing context, owner of all primitives.
struct le_2d_o {
	le_command_buffer_encoder_o *    encoder = nullptr;
	std::vector<le_2d_primitive_o *> primitives; // owning
};

struct node_data_t {
	vec2f origin;           //x,y
	float ccw_rotation = 0; // rotation in ccw around z axis, anchored at position
	float scale{1};
};

struct material_data_t {
	uint32_t color;
	float    stroke_weight;
	bool     filled;
};

struct circle_data_t {
	float    radius;
	uint32_t subdivisions;
};

struct ellipse_data_t {
	glm::vec2 radii; // radius x, radius y
	uint32_t  subdivisions;
};

struct arc_data_t {
	glm::vec2 radii; // radius x, radius y
	float     angle_start_rad;
	float     angle_end_rad;
	uint32_t  subdivisions;
};

struct path_data_t {
	le_path_o *path;
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

	Type            type;
	node_data_t     node;
	material_data_t material;

	union {
		circle_data_t  as_circle;
		ellipse_data_t as_ellipse;
		arc_data_t     as_arc;
		line_data_t    as_line;
		path_data_t    as_path;

	} data;
};

// ----------------------------------------------------------------------

static le_2d_o *le_2d_create( le_command_buffer_encoder_o *encoder ) {
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
	uint32_t  color;
};

// ----------------------------------------------------------------------

static void generate_geometry_line( std::vector<VertexData2D> &geometry, glm::vec2 const &p0, glm::vec2 const &p1, float thickness, uint32_t colour ) {
	if ( p0 == p1 ) {
		// return empty if line cannot be generated.
		return;
	}

	geometry.reserve( geometry.size() + 6 );

	auto p_vec  = p1 - p0;
	auto p_norm = glm::normalize( p_vec );

	// Line offset: rotate p_norm 90 deg ccw
	glm::vec2 off = {-p_norm.y, p_norm.x};

	// Line thickness will be twice offset, therefore we scale offset by half line thickness

	off *= 0.5f * thickness; // scale line by thickness

	geometry.push_back( {p0 + off, {0.f, 0.f}, colour} );
	geometry.push_back( {p0 - off, {0.f, 1.f}, colour} );
	geometry.push_back( {p1 + off, {1.f, 0.f}, colour} );

	geometry.push_back( {p0 - off, {0.f, 1.f}, colour} );
	geometry.push_back( {p1 - off, {1.f, 1.f}, colour} );
	geometry.push_back( {p1 + off, {1.f, 0.f}, colour} );
}

// ----------------------------------------------------------------------

static void generate_geometry_outline_arc( std::vector<VertexData2D> &geometry, float angle_start_rad, float angle_end_rad, glm::vec2 radii, float thickness, uint32_t subdivisions, uint32_t colour ) {

	if ( std::numeric_limits<float>::epsilon() > angle_end_rad - angle_start_rad ) {
		return;
	}

	// ---------| invariant: angle difference is not too close to zero

	glm::vec2 p0_far  = ( radii + 0.5f * thickness ) * glm::vec2{cosf( angle_start_rad ), sinf( angle_start_rad )};
	glm::vec2 p0_near = ( radii - +0.5f * thickness ) * glm::vec2{cosf( angle_start_rad ), sinf( angle_start_rad )};

	float angle_delta = ( angle_end_rad - angle_start_rad ) / float( subdivisions );

	for ( uint32_t i = 1; i <= subdivisions; ++i ) {
		float angle = angle_start_rad + angle_delta * i;

		glm::vec2 p1_far  = ( radii + 0.5f * thickness ) * glm::vec2{cosf( angle ), sinf( angle )};
		glm::vec2 p1_near = ( radii - +0.5f * thickness ) * glm::vec2{cosf( angle ), sinf( angle )};

		geometry.push_back( {p0_far, {0.f, 0.f}, colour} );
		geometry.push_back( {p0_near, {0.f, 1.f}, colour} );
		geometry.push_back( {p1_far, {1.f, 0.f}, colour} );

		geometry.push_back( {p0_near, {0.f, 1.f}, colour} );
		geometry.push_back( {p1_near, {1.f, 1.f}, colour} );
		geometry.push_back( {p1_far, {1.f, 0.f}, colour} );

		std::swap( p0_far, p1_far );
		std::swap( p0_near, p1_near );
	}
}

// ----------------------------------------------------------------------

static void generate_geometry_ellipse( std::vector<VertexData2D> &geometry, float angle_start_rad, float angle_end_rad, glm::vec2 radii, uint32_t subdivisions, uint32_t color ) {

	if ( subdivisions < 3 || std::numeric_limits<float>::epsilon() >= ( radii.x * radii.y ) ) {
		// Return empty if geometry cannot be generated.
		// This is either because we are not allowed enough subdivisions,
		// or our radius is too close to zero.
		return;
	}

	// --------| invariant: It should be possible to generate circle geometry.

	geometry.reserve( 3 * ( subdivisions + 1 ) );

	float arc_segment = ( angle_end_rad - angle_start_rad ) / float( subdivisions ); // arc segment given in radians

	VertexData2D v_c{};
	v_c.pos      = {0.f, 0.f};
	v_c.texCoord = {0.5, 0.5};
	v_c.color    = color;

	float     arc_angle = angle_start_rad;
	glm::vec2 n{cosf( arc_angle ), sinf( arc_angle )};

	VertexData2D v{};
	v.pos      = radii * n;
	v.texCoord = glm::vec2{0.5, 0.5} + 0.5f * n;
	v.color    = color;

	for ( uint32_t i = 1; i <= subdivisions; ++i ) {
		// one triangle per subdivision, we re-use last vertex if available

		geometry.push_back( v_c );
		geometry.push_back( v );

		float     arc_angle = angle_start_rad + i * arc_segment;
		glm::vec2 n{cosf( arc_angle ), sinf( arc_angle )};

		v.pos      = radii * n;
		v.texCoord = glm::vec2{0.5, 0.5} + 0.5f * n;

		geometry.push_back( v );
	}
}
// ----------------------------------------------------------------------

static void generate_geometry_outline_path( std::vector<VertexData2D> &geometry, le_path_o *path, float stroke_weight, float tolerance, uint32_t color ) {

	using namespace le_path;

	// le_path_i.trace( path, subdivisions );
	if ( stroke_weight < 2.f ) {

		le_path_i.flatten( path, tolerance );

		size_t const num_polylines = le_path_i.get_num_polylines( path );
		for ( size_t i = 0; i != num_polylines; ++i ) {
			glm::vec2 const *line_vertices = nullptr;
			size_t           num_vertices;
			le_path_i.get_vertices_for_polyline( path, i, &line_vertices, &num_vertices );
			auto *p_prev = line_vertices + 0;
			for ( size_t j = 1; j != num_vertices; ++j ) {
				glm::vec2 const *p_cur = line_vertices + j;
				generate_geometry_line( geometry, *p_prev, *p_cur, stroke_weight, color );
				p_prev = p_cur;
			}
		}
	} else {

		size_t const num_contours = le_path_i.get_num_contours( path );

		if ( false ) {
			std::vector<glm::vec2> vertices_l( 1024 );
			std::vector<glm::vec2> vertices_r( 1024 );

			for ( size_t i = 0; i != num_contours; ++i ) {

				size_t num_vertices_l = vertices_l.size();
				size_t num_vertices_r = vertices_r.size();

				glm::vec2 *v_l                   = vertices_l.data();
				glm::vec2 *v_r                   = vertices_r.data();
				bool       vertices_large_enough = le_path_i.generate_offset_outline_for_contour( path, i, stroke_weight, tolerance, v_l, &num_vertices_l, v_r, &num_vertices_r );

				if ( !vertices_large_enough ) {
					vertices_l.resize( num_vertices_l + 1 );
					vertices_r.resize( num_vertices_r + 1 );
					le_path_i.generate_offset_outline_for_contour( path, i, stroke_weight, tolerance, vertices_l.data(), &num_vertices_l, vertices_r.data(), &num_vertices_r );
				}

				// reverse elements
				std::reverse( vertices_r.begin(), vertices_r.begin() + num_vertices_r );

				std::vector<glm::vec2> all_vertices;
				all_vertices.insert( all_vertices.end(), vertices_l.begin(), vertices_l.begin() + num_vertices_l );
				all_vertices.insert( all_vertices.end(), vertices_r.begin(), vertices_r.begin() + num_vertices_r );
				all_vertices.push_back( all_vertices.front() );

				auto p_prev = all_vertices.front();

				for ( size_t j = 1; j != all_vertices.size(); ++j ) {
					glm::vec2 const p_cur = all_vertices[ j ];
					generate_geometry_line( geometry, p_prev, p_cur, 1, color );
					p_prev = p_cur;
				}
			}
		} else {

			using namespace le_tessellator;
			auto tess = le_tessellator_i.create();
			le_tessellator_i.set_options( tess, le_tessellator::Options::eWindingNonzero );
			//			le_tessellator_i.set_options( tess, le_tessellator::Options::bitConstrainedDelaunayTriangulation );
			//			le_tessellator_i.set_options( tess, le_tessellator::Options::bitUseEarcutTessellator );

			std::vector<glm::vec2> vertices_l( 1024 );
			std::vector<glm::vec2> vertices_r( 1024 );

			for ( size_t i = 0; i != num_contours; ++i ) {

				size_t num_vertices_l = vertices_l.size();
				size_t num_vertices_r = vertices_r.size();

				glm::vec2 *v_l = vertices_l.data();
				glm::vec2 *v_r = vertices_r.data();

				bool vertices_large_enough = le_path_i.generate_offset_outline_for_contour( path, i, stroke_weight, tolerance, v_l, &num_vertices_l, v_r, &num_vertices_r );

				if ( !vertices_large_enough ) {
					vertices_l.resize( num_vertices_l + 1 );
					vertices_r.resize( num_vertices_r + 1 );
					le_path_i.generate_offset_outline_for_contour( path, i, stroke_weight, tolerance, vertices_l.data(), &num_vertices_l, vertices_r.data(), &num_vertices_r );
				}

				// reverse elements
				std::reverse( vertices_r.begin(), vertices_r.begin() + num_vertices_r );

				std::vector<glm::vec2> all_vertices;
				all_vertices.insert( all_vertices.end(), vertices_l.begin(), vertices_l.begin() + num_vertices_l );
				all_vertices.insert( all_vertices.end(), vertices_r.begin(), vertices_r.begin() + num_vertices_r );

				if ( !all_vertices.empty() ) {
					all_vertices.push_back( all_vertices.front() );
					le_tessellator_i.add_polyline( tess, all_vertices.data(), all_vertices.size() );
				}
			}

			le_tessellator_i.tessellate( tess );

			le_tessellator_api::IndexType const *indices;
			size_t                               num_indices = 0;
			glm::vec2 const *                    vertices;
			size_t                               num_vertices = 0;

			le_tessellator_i.get_indices( tess, &indices, &num_indices );
			le_tessellator_i.get_vertices( tess, &vertices, &num_vertices );

			// TODO: what do we want to set for tex coordinate?

			for ( size_t i = 0; i + 2 < num_indices; ) {
				geometry.push_back( {vertices[ indices[ i++ ] ], {1, 0}, color} );
				geometry.push_back( {vertices[ indices[ i++ ] ], {0, 1}, color} );
				geometry.push_back( {vertices[ indices[ i++ ] ], {1, 1}, color} );
			}

			le_tessellator_i.destroy( tess );
		}
	}
}

// Generates triangles by tessellating what's contained within path
static void generate_geometry_path( std::vector<VertexData2D> &geometry, le_path_o *path, float stroke_weight, float tolerance, uint32_t color ) {

	using namespace le_path;
	using namespace le_tessellator;

	le_path_i.flatten( path, tolerance );

	size_t const num_polylines = le_path_i.get_num_polylines( path );

	auto tess = le_tessellator_i.create();
	// le_tessellator_i.set_options( tess, le_tessellator::Options::bitConstrainedDelaunayTriangulation );
	le_tessellator_i.set_options( tess, le_tessellator::Options::bitUseEarcutTessellator );

	for ( size_t i = 0; i != num_polylines; ++i ) {
		glm::vec2 const *line_vertices = nullptr;
		size_t           num_vertices;
		le_path_i.get_vertices_for_polyline( path, i, &line_vertices, &num_vertices );
		le_tessellator_i.add_polyline( tess, line_vertices, num_vertices );
	}

	le_tessellator_i.tessellate( tess );

	le_tessellator_api::IndexType const *indices;
	size_t                               num_indices = 0;
	glm::vec2 const *                    vertices;
	size_t                               num_vertices = 0;

	le_tessellator_i.get_indices( tess, &indices, &num_indices );
	le_tessellator_i.get_vertices( tess, &vertices, &num_vertices );

	// TODO: what do we want to set for tex coordinate?

	for ( size_t i = 0; i + 2 < num_indices; ) {
		geometry.push_back( {vertices[ indices[ i++ ] ], {0, 0}, color} );
		geometry.push_back( {vertices[ indices[ i++ ] ], {0, 0}, color} );
		geometry.push_back( {vertices[ indices[ i++ ] ], {0, 0}, color} );
	}

	le_tessellator_i.destroy( tess );
}

// ----------------------------------------------------------------------

static void generate_geometry_for_primitive( le_2d_primitive_o *p, std::vector<VertexData2D> &geometry ) {

	switch ( p->type ) {
	case le_2d_primitive_o::Type::eLine: {
		// generate geometry for line
		auto const &line = p->data.as_line;

		generate_geometry_line( geometry, line.p0, line.p1, p->material.stroke_weight, p->material.color );

	} break;
	case le_2d_primitive_o::Type::eCircle: {

		auto const &circle = p->data.as_circle;

		if ( p->material.filled ) {
			generate_geometry_ellipse( geometry, 0, glm::two_pi<float>(), {circle.radius, circle.radius}, circle.subdivisions, p->material.color );
		} else {
			generate_geometry_outline_arc( geometry, 0, glm::two_pi<float>(), {circle.radius, circle.radius}, p->material.stroke_weight, circle.subdivisions, p->material.color );
		}

	} break;
	case le_2d_primitive_o::Type::eEllipse: {
		auto const &ellipse = p->data.as_ellipse;
		if ( p->material.filled ) {
			generate_geometry_ellipse( geometry, 0, glm::two_pi<float>(), ellipse.radii, ellipse.subdivisions, p->material.color );
		} else {
			generate_geometry_outline_arc( geometry, 0, glm::two_pi<float>(), ellipse.radii, p->material.stroke_weight, ellipse.subdivisions, p->material.color );
		}
	} break;
	case le_2d_primitive_o::Type::eArc: {
		auto const &arc = p->data.as_arc;
		if ( p->material.filled ) {
			generate_geometry_ellipse( geometry, arc.angle_start_rad, arc.angle_end_rad, arc.radii, arc.subdivisions, p->material.color );
		} else {
			generate_geometry_outline_arc( geometry, arc.angle_start_rad, arc.angle_end_rad, arc.radii, p->material.stroke_weight, arc.subdivisions, p->material.color );
		}
	} break;
	case le_2d_primitive_o::Type::ePath: {
		auto const &path = p->data.as_path;
		if ( p->material.filled ) {
			generate_geometry_path( geometry, path.path, p->material.stroke_weight, path.tolerance, p->material.color );
		} else {
			generate_geometry_outline_path( geometry, path.path, p->material.stroke_weight, path.tolerance, p->material.color );
		}
	} break;
	case le_2d_primitive_o::Type::eUndefined:
		// noop
		break;
	}
}

static void le_2d_draw_primitive( le_command_buffer_encoder_o *encoder_, le_2d_primitive_o *p, glm::mat4 const &v_p_matrix ) {

	std::vector<VertexData2D> geometry;

	generate_geometry_for_primitive( p, geometry );

	if ( geometry.empty() ) {
		return;
	}

	// --------| Invariant: there is geometry to draw

	le::Encoder encoder{encoder_};

	// We must apply the primitive node's transform.

	glm::mat4 local_transform{1}; // identity matrix

	local_transform = glm::translate( local_transform, {p->node.origin.x, p->node.origin.y, 0.f} );
	local_transform = v_p_matrix * local_transform;

	encoder
	    .setArgumentData( LE_ARGUMENT_NAME( "Mvp" ), &local_transform, sizeof( glm::mat4 ) )
	    .setVertexData( geometry.data(), sizeof( VertexData2D ) * geometry.size(), 0 )
	    .draw( uint32_t( geometry.size() ) );
}

// ----------------------------------------------------------------------

static void le_2d_draw_primitives( le_2d_o const *self ) {

	/* We might want to do some sorting, and optimising here
	 * Sort by pipeline for example. Also, issue draw commands
	 * as instanced draws if more than three of the same prims
	 * are issued.
	 */

	le::Encoder encoder{self->encoder};
	auto *      pm = encoder.getPipelineManager();

	static le_shader_module_o *vert = le_backend_vk::le_pipeline_manager_i.create_shader_module( pm, "./resources/shaders/2d_primitives.vert", {le::ShaderStage::eVertex}, "" );
	static le_shader_module_o *frag = le_backend_vk::le_pipeline_manager_i.create_shader_module( pm, "./resources/shaders/2d_primitives.frag", {le::ShaderStage::eFragment}, "" );

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
	                .addAttribute( offsetof( VertexData2D, color ), le_num_type::eU8, 4, true )
	            .end()
	        .end()
			.withRasterizationState()
//				.setPolygonMode(le::PolygonMode::eLine)
			.end()
	        .build();
	// clang-format on

	encoder
	    .bindGraphicsPipeline( pipeline );

	// Calculate view projection matrix
	// for 2D, this will be a simple orthographic projection, which means that the view matrix
	// (camera matrix) will be the identity, and does not need to be factored in.

	auto extents          = encoder.getRenderpassExtent();
	auto ortho_projection = glm::ortho( 0.f, float( extents.width ), 0.f, float( extents.height ) );

	{
		// set a negative height for viewport so that +Y goes up, rather than down.

		le::Viewport viewports[ 2 ] = {
		    {0.f, float( extents.height ), float( extents.width ), -float( extents.height ), 0.f, 1.f},
		    {0.f, 0, float( extents.width ), float( extents.height ), 0.f, 1.f},
		};

		encoder.setViewports( 0, 1, viewports + 1 );
	}

	for ( auto &p : self->primitives ) {
		le_2d_draw_primitive( self->encoder, p, ortho_projection );
	}
}

// ----------------------------------------------------------------------

static void le_2d_destroy( le_2d_o *self ) {
	//	std::cout << "destroy 2d ctx: " << std::dec << self->id << std::flush << std::endl;

	le_2d_draw_primitives( self );

	for ( auto &p : self->primitives ) {

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

static le_2d_primitive_o *le_2d_allocate_primitive( le_2d_o *self ) {
	le_2d_primitive_o *p = new le_2d_primitive_o();

	p->node.scale        = 1;
	p->node.origin       = {};
	p->node.ccw_rotation = 0;

	p->material.color         = 0xffffffff;
	p->material.stroke_weight = 0.f;
	p->material.filled        = true;

	self->primitives.push_back( p );

	return p;
}

// ----------------------------------------------------------------------

static le_2d_primitive_o *le_2d_primitive_create_circle( le_2d_o *context ) {
	auto p = le_2d_allocate_primitive( context );

	p->type   = le_2d_primitive_o::Type::eCircle;
	auto &obj = p->data.as_circle;

	obj.radius       = 0.f;
	obj.subdivisions = 36;

	return p;
}

// ----------------------------------------------------------------------

static le_2d_primitive_o *le_2d_primitive_create_ellipse( le_2d_o *context ) {
	auto p = le_2d_allocate_primitive( context );

	p->type = le_2d_primitive_o::Type::eEllipse;

	auto &obj = p->data.as_ellipse;

	obj.radii        = {0.f, 0.f};
	obj.subdivisions = 36;

	return p;
}

// ----------------------------------------------------------------------

static le_2d_primitive_o *le_2d_primitive_create_arc( le_2d_o *context ) {
	auto p = le_2d_allocate_primitive( context );

	p->type = le_2d_primitive_o::Type::eArc;

	auto &obj = p->data.as_arc;

	obj.radii           = {0.f, 0.f};
	obj.subdivisions    = 36;
	obj.angle_start_rad = 0;
	obj.angle_end_rad   = glm::two_pi<float>();

	p->material.stroke_weight = 1.f;

	return p;
}

// ----------------------------------------------------------------------

static le_2d_primitive_o *le_2d_primitive_create_line( le_2d_o *context ) {
	auto p = le_2d_allocate_primitive( context );

	p->type   = le_2d_primitive_o::Type::eLine;
	auto &obj = p->data.as_line;

	obj.p0                    = {};
	obj.p1                    = {};
	p->material.stroke_weight = 1.f;

	return p;
}

// ----------------------------------------------------------------------

static le_2d_primitive_o *le_2d_primitive_create_path( le_2d_o *context ) {
	auto p = le_2d_allocate_primitive( context );

	p->type   = le_2d_primitive_o::Type::ePath;
	auto &obj = p->data.as_path;

	obj.path      = le_path::le_path_i.create();
	obj.tolerance = 0.5f;

	p->material.stroke_weight = 1.f;
	return p;
}

// ----------------------------------------------------------------------

static void le_2d_primitive_path_move_to( le_2d_primitive_o *p, vec2f const *pos ) {
	assert( p->type == le_2d_primitive_o::Type::ePath );
	auto &obj = p->data.as_path;
	le_path::le_path_i.move_to( obj.path, pos );
}

// ----------------------------------------------------------------------

static void le_2d_primitive_path_line_to( le_2d_primitive_o *p, vec2f const *pos ) {
	assert( p->type == le_2d_primitive_o::Type::ePath );
	auto &obj = p->data.as_path;
	le_path::le_path_i.line_to( obj.path, pos );
}

// ----------------------------------------------------------------------

static void le_2d_primitive_path_close( le_2d_primitive_o *p ) {
	assert( p->type == le_2d_primitive_o::Type::ePath );
	auto &obj = p->data.as_path;
	le_path::le_path_i.close( obj.path );
}
// ----------------------------------------------------------------------

static void le_2d_primitive_path_cubic_bezier_to( le_2d_primitive_o *p, vec2f const *pos, vec2f const *c1, vec2f const *c2 ) {
	assert( p->type == le_2d_primitive_o::Type::ePath );
	auto &obj = p->data.as_path;
	le_path::le_path_i.cubic_bezier_to( obj.path, pos, c1, c2 );
}

// ----------------------------------------------------------------------

static void le_2d_primitive_path_quad_bezier_to( le_2d_primitive_o *p, vec2f const *pos, vec2f const *c1 ) {
	assert( p->type == le_2d_primitive_o::Type::ePath );
	auto &obj = p->data.as_path;
	le_path::le_path_i.quad_bezier_to( obj.path, pos, c1 );
}

// ----------------------------------------------------------------------

static void le_2d_primitive_path_add_from_simplified_svg( le_2d_primitive_o *p, char const *svg ) {
	assert( p->type == le_2d_primitive_o::Type::ePath );
	auto &obj = p->data.as_path;
	le_path::le_path_i.add_from_simplified_svg( obj.path, svg );
}

// ----------------------------------------------------------------------

static void le_2d_primitive_path_set_tolerance( le_2d_primitive_o *p, float tolerance ) {
	assert( p->type == le_2d_primitive_o::Type::ePath );
	auto &obj     = p->data.as_path;
	obj.tolerance = tolerance;
}

// ----------------------------------------------------------------------

static void le_2d_primitive_set_node_position( le_2d_primitive_o *p, vec2f const *pos ) {
	p->node.origin = *pos;
}

static void le_2d_primitive_set_stroke_weight( le_2d_primitive_o *p, float weight ) {
	p->material.stroke_weight = weight;
}

static void le_2d_primitive_set_filled( le_2d_primitive_o *p, bool filled ) {
	p->material.filled = filled;
}

static void le_2d_primitive_set_color( le_2d_primitive_o *p, uint32_t r8g8b8a8_color ) {
	p->material.color = r8g8b8a8_color;
}

#define SETTER_IMPLEMENT( prim_type, field_type, field_name )                                                   \
	static void le_2d_primitive_##prim_type##_set_##field_name( le_2d_primitive_o *p, field_type field_name ) { \
		p->data.as_##prim_type.field_name = field_name;                                                         \
	}

#define SETTER_IMPLEMENT_CPY( prim_type, field_type, field_name )                                               \
	static void le_2d_primitive_##prim_type##_set_##field_name( le_2d_primitive_o *p, field_type field_name ) { \
		p->data.as_##prim_type.field_name = *field_name;                                                        \
	}

SETTER_IMPLEMENT( circle, float, radius );
SETTER_IMPLEMENT( circle, uint32_t, subdivisions );

SETTER_IMPLEMENT_CPY( ellipse, vec2f const *, radii );
SETTER_IMPLEMENT( ellipse, uint32_t, subdivisions );

SETTER_IMPLEMENT_CPY( arc, vec2f const *, radii );
SETTER_IMPLEMENT( arc, uint32_t, subdivisions );

SETTER_IMPLEMENT( arc, float, angle_start_rad );
SETTER_IMPLEMENT( arc, float, angle_end_rad );

SETTER_IMPLEMENT_CPY( line, vec2f const *, p0 );
SETTER_IMPLEMENT_CPY( line, vec2f const *, p1 );

#undef SETTER_IMPLEMENT

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_2d_api( void *api ) {
	auto &le_2d_i = static_cast<le_2d_api *>( api )->le_2d_i;

	le_2d_i.create  = le_2d_create;
	le_2d_i.destroy = le_2d_destroy;

	auto &le_2d_primitive_i = static_cast<le_2d_api *>( api )->le_2d_primitive_i;

#define SET_PRIMITIVE_FPTR( prim_type, field_name ) \
	le_2d_primitive_i.prim_type##_set_##field_name = le_2d_primitive_##prim_type##_set_##field_name

	SET_PRIMITIVE_FPTR( circle, radius );
	SET_PRIMITIVE_FPTR( circle, subdivisions );

	SET_PRIMITIVE_FPTR( ellipse, radii );
	SET_PRIMITIVE_FPTR( ellipse, subdivisions );

	SET_PRIMITIVE_FPTR( arc, radii );
	SET_PRIMITIVE_FPTR( arc, subdivisions );
	SET_PRIMITIVE_FPTR( arc, angle_start_rad );
	SET_PRIMITIVE_FPTR( arc, angle_end_rad );

	SET_PRIMITIVE_FPTR( line, p0 );
	SET_PRIMITIVE_FPTR( line, p1 );

#undef SET_PRIMITIVE_FPTR

	le_2d_primitive_i.path_move_to                 = le_2d_primitive_path_move_to;
	le_2d_primitive_i.path_line_to                 = le_2d_primitive_path_line_to;
	le_2d_primitive_i.path_quad_bezier_to          = le_2d_primitive_path_quad_bezier_to;
	le_2d_primitive_i.path_cubic_bezier_to         = le_2d_primitive_path_cubic_bezier_to;
	le_2d_primitive_i.path_add_from_simplified_svg = le_2d_primitive_path_add_from_simplified_svg;
	le_2d_primitive_i.path_set_tolerance           = le_2d_primitive_path_set_tolerance;
	le_2d_primitive_i.path_close                   = le_2d_primitive_path_close;
	le_2d_primitive_i.create_path                  = le_2d_primitive_create_path;

	le_2d_primitive_i.create_arc     = le_2d_primitive_create_arc;
	le_2d_primitive_i.create_ellipse = le_2d_primitive_create_ellipse;
	le_2d_primitive_i.create_circle  = le_2d_primitive_create_circle;
	le_2d_primitive_i.create_line    = le_2d_primitive_create_line;

	le_2d_primitive_i.set_node_position = le_2d_primitive_set_node_position;
	le_2d_primitive_i.set_stroke_weight = le_2d_primitive_set_stroke_weight;
	le_2d_primitive_i.set_filled        = le_2d_primitive_set_filled;
	le_2d_primitive_i.set_color         = le_2d_primitive_set_color;
}

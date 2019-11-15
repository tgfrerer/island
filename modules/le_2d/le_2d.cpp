#include "le_2d.h"
#include "pal_api_loader/ApiRegistry.hpp"

#include <iostream>
#include <iomanip>
#include <vector>

#include <cstring> // for memset

#include "le_renderer/le_renderer.h"

static size_t unique_num = 0;
typedef float vec2f[ 2 ];

// A drawing context, owner of all primitives.
struct le_2d_o {
	le_command_buffer_encoder_o *    encoder = nullptr;
	size_t                           id      = 0;
	std::vector<le_2d_primitive_o *> primitives; // owning
};

struct node_data_t {
	float position[ 2 ];    //x,y
	float ccw_rotation = 0; // rotation in ccw around z axis, anchored at position
	float scale{1};
};

struct circle_data_t {
	float    radius;
	uint32_t subdivisions;
	bool     filled;
};

struct line_data_t {
	float p0[ 2 ];
	float p1[ 2 ];
};

struct le_2d_primitive_o {

	enum class Type : uint32_t {
		eUndefined,
		eCircle,
		eLine,
	};

	Type        type;
	node_data_t node;

	union {
		circle_data_t as_circle;
		line_data_t   as_line;

	} data;
};

// ----------------------------------------------------------------------

static le_2d_o *le_2d_create( le_command_buffer_encoder_o *encoder ) {
	auto self     = new le_2d_o();
	self->id      = unique_num++;
	self->encoder = encoder;
	self->primitives.reserve( 4096 / 8 );
	std::cout << "create 2d ctx: " << std::dec << self->id << std::flush << std::endl;
	return self;
}

// ----------------------------------------------------------------------

static void le_2d_destroy( le_2d_o *self ) {
	std::cout << "destroy 2d ctx: " << std::dec << self->id << std::flush << std::endl;

	for ( auto &p : self->primitives ) {
		delete p;
	}

	delete self;
}

static le_2d_primitive_o *le_2d_allocate_primitive( le_2d_o *self ) {
	le_2d_primitive_o *p = new le_2d_primitive_o();
	self->primitives.push_back( p );
	return p;
}

static le_2d_primitive_o *le_2d_primitive_create_circle( le_2d_o *context ) {
	auto p = le_2d_allocate_primitive( context );

	p->type = le_2d_primitive_o::Type::eCircle;

	p->data.as_circle.filled       = true;
	p->data.as_circle.radius       = 0.f;
	p->data.as_circle.subdivisions = 12;

	return p;
}

static le_2d_primitive_o *le_2d_primitive_create_line( le_2d_o *context ) {
	auto p = le_2d_allocate_primitive( context );

	p->type = le_2d_primitive_o::Type::eLine;

	p->data.as_line.p0[ 0 ] = 0;
	p->data.as_line.p0[ 0 ] = 0;
	p->data.as_line.p1[ 1 ] = 0;
	p->data.as_line.p1[ 1 ] = 0;

	return p;
}

static void le_2d_primitive_set_node_position( le_2d_primitive_o *p, vec2f const pos ) {
	memcpy( &p->node.position, pos, sizeof( vec2f ) );
}

#define SETTER_IMPLEMENT( prim_type, field_type, field_name )                                                   \
	static void le_2d_primitive_##prim_type##_set_##field_name( le_2d_primitive_o *p, field_type field_name ) { \
		p->data.as_##prim_type.field_name = field_name;                                                         \
	}

#define SETTER_IMPLEMENT_MEMCPY( prim_type, field_type, field_name )                                            \
	static void le_2d_primitive_##prim_type##_set_##field_name( le_2d_primitive_o *p, field_type field_name ) { \
		memcpy( &p->data.as_##prim_type.field_name, field_name, sizeof( field_type ) );                         \
	}

SETTER_IMPLEMENT( circle, float, radius );
SETTER_IMPLEMENT( circle, uint32_t, subdivisions );
SETTER_IMPLEMENT( circle, bool, filled );

SETTER_IMPLEMENT_MEMCPY( line, vec2f, p0 );
SETTER_IMPLEMENT_MEMCPY( line, vec2f, p1 );

#undef SETTER_IMPLEMENT

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_2d_api( void *api ) {
	auto &le_2d_i = static_cast<le_2d_api *>( api )->le_2d_i;

	le_2d_i.create  = le_2d_create;
	le_2d_i.destroy = le_2d_destroy;

	auto &le_2d_primitive_i = static_cast<le_2d_api *>( api )->le_2d_primitive_i;

#define SET_PRIMITIVE_FPTR( prim_type, field_name ) \
	le_2d_primitive_i.prim_type##_set_##field_name = le_2d_primitive_##prim_type##_set_##field_name

	SET_PRIMITIVE_FPTR( circle, filled );
	SET_PRIMITIVE_FPTR( circle, radius );
	SET_PRIMITIVE_FPTR( circle, subdivisions );

	SET_PRIMITIVE_FPTR( line, p0 );
	SET_PRIMITIVE_FPTR( line, p1 );

#undef SET_PRIMITIVE_FPTR

	le_2d_primitive_i.create_circle     = le_2d_primitive_create_circle;
	le_2d_primitive_i.create_line       = le_2d_primitive_create_line;
	le_2d_primitive_i.set_node_position = le_2d_primitive_set_node_position;
}

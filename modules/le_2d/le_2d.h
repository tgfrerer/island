#ifndef GUARD_le_2d_H
#define GUARD_le_2d_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#ifdef __cplusplus

#	include <glm/fwd.hpp>

extern "C" {
#endif

struct le_2d_o;

struct le_renderer_o;
struct le_render_module_o;
struct le_renderpass_o;
struct le_rendergraph_o;
struct le_command_buffer_encoder_o;
struct le_backend_o;
struct le_shader_module_o; ///< shader module, 1:1 relationship with a shader source file
struct le_pipeline_manager_o;
struct le_2d_primitive_o;

void register_le_2d_api( void *api );

// clang-format off
struct le_2d_api {
	static constexpr auto id      = "le_2d";
	static constexpr auto pRegFun = register_le_2d_api;

	struct le_2d_primitive_interface_t{

		void ( *set_node_position) ( le_2d_primitive_o* p, glm::vec2 const * pos );
		void ( *set_stroke_weight) ( le_2d_primitive_o* p, float stroke_weight ); // thickness of any outlines, and lines, defaults to 0
		void ( *set_filled) ( le_2d_primitive_o* p, bool filled); // thickness of any outlines, and lines, defaults to 0

		void ( *set_color)( le_2d_primitive_o* p, uint32_t r8g8b8a8_color ); // color defaults to white

		#define SETTER_DECLARE( prim_type, field_type, field_name ) \
		void (  *prim_type##_set_##field_name)(le_2d_primitive_o* p, field_type field_name)\

		le_2d_primitive_o* ( *create_circle         ) ( le_2d_o* context);
		SETTER_DECLARE( circle, float   , radius);
		SETTER_DECLARE( circle, uint32_t, subdivisions );
		SETTER_DECLARE( circle, bool    , filled );

		le_2d_primitive_o* ( *create_ellipse         ) ( le_2d_o* context);
		SETTER_DECLARE( ellipse, glm::vec2 const *, radii);
		SETTER_DECLARE( ellipse, uint32_t, subdivisions );
		SETTER_DECLARE( ellipse, bool    , filled );

		le_2d_primitive_o* ( *create_arc         ) ( le_2d_o* context);
		SETTER_DECLARE( arc, glm::vec2 const *, radii);
		SETTER_DECLARE( arc, float, angle_start_rad);
		SETTER_DECLARE( arc, float, angle_end_rad);
		SETTER_DECLARE( arc, uint32_t, subdivisions );
		SETTER_DECLARE( arc, bool    , filled );

		le_2d_primitive_o* ( *create_line           ) ( le_2d_o* context);
		SETTER_DECLARE( line, glm::vec2 const *, p0);
		SETTER_DECLARE( line, glm::vec2 const *, p1);

		le_2d_primitive_o* (*create_path)(le_2d_o* context);

		void (*path_move_to)(le_2d_primitive_o* p, glm::vec2 const * pos);
		void (*path_line_to)(le_2d_primitive_o* p, glm::vec2 const * pos);
		void (*path_quad_bezier_to)(le_2d_primitive_o* p, glm::vec2 const * p1, glm::vec2 const * c1);
		void (*path_cubic_bezier_to)(le_2d_primitive_o* p, glm::vec2 const * p1, glm::vec2 const * c1, glm::vec2 const * c2);
		void (*path_add_from_simplified_svg)(le_2d_primitive_o* p, char const * svg);

		#undef SETTER_DECLARE
	};

	struct le_2d_interface_t {

		le_2d_o *    ( * create                   ) ( le_command_buffer_encoder_o* encoder);
		void         ( * destroy                  ) ( le_2d_o* self );

	};

	le_2d_interface_t			le_2d_i;
	le_2d_primitive_interface_t le_2d_primitive_i;
};
// clang-format on

#ifdef __cplusplus
} // extern c

namespace le_2d {
#	ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<le_2d_api>( true );
#	else
const auto api = Registry::addApiStatic<le_2d_api>();
#	endif

static const auto &le_2d_i      = api -> le_2d_i;
static const auto &le_2d_prim_i = api -> le_2d_primitive_i;

} // namespace le_2d

class Le2D : NoCopy, NoMove {

#	define BUILDER_IMPLEMENT_VEC( builder_type, obj_name, field_type, field_name ) \
		builder_type &set_##field_name( field_type field_name ) {                   \
			le_2d::le_2d_prim_i.obj_name##_set_##field_name( self, &field_name );   \
			return *this;                                                           \
		}
#	define BUILDER_IMPLEMENT( builder_type, obj_name, field_type, field_name )  \
		builder_type &set_##field_name( field_type field_name ) {                \
			le_2d::le_2d_prim_i.obj_name##_set_##field_name( self, field_name ); \
			return *this;                                                        \
		}

	le_2d_o *self;

  public:
	Le2D( le_command_buffer_encoder_o *encoder_ )
	    : self( le_2d::le_2d_i.create( encoder_ ) ) {
	}

	~Le2D() {
		le_2d::le_2d_i.destroy( self );
	}

	// ---

	class CircleBuilder {
		Le2D &             parent;
		le_2d_primitive_o *self;

	  public:
		CircleBuilder( Le2D &parent_ )
		    : parent( parent_ ) {
		}

		CircleBuilder &create() {
			self = le_2d::le_2d_prim_i.create_circle( parent.self );
			return *this;
		}

		BUILDER_IMPLEMENT( CircleBuilder, circle, float, radius )
		BUILDER_IMPLEMENT( CircleBuilder, circle, uint32_t, subdivisions )

		CircleBuilder &set_node_position( glm::vec2 const &pos ) {
			le_2d::le_2d_prim_i.set_node_position( self, &pos );
			return *this;
		}

		CircleBuilder &set_filled( bool filled ) {
			le_2d::le_2d_prim_i.set_filled( self, filled );
			return *this;
		}

		CircleBuilder &set_stroke_weight( float weight ) {
			le_2d::le_2d_prim_i.set_stroke_weight( self, weight );
			return *this;
		}

		CircleBuilder &set_color( uint32_t r8g8b8a8_color ) {
			le_2d::le_2d_prim_i.set_color( self, r8g8b8a8_color );
			return *this;
		}

		CircleBuilder &set_color( uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255 ) {
			uint32_t r8g8b8a8_color = uint32_t( r << 24 | g << 16 | b << 8 | a );
			le_2d::le_2d_prim_i.set_color( self, r8g8b8a8_color );
			return *this;
		}

		Le2D &draw() {
			return parent;
		}
	};

	CircleBuilder mCircleBuilder{*this};

	CircleBuilder &circle() {
		return mCircleBuilder.create();
	}

	// ---

	class EllipseBuilder {
		Le2D &             parent;
		le_2d_primitive_o *self;

	  public:
		EllipseBuilder( Le2D &parent_ )
		    : parent( parent_ ) {
		}

		EllipseBuilder &create() {
			self = le_2d::le_2d_prim_i.create_ellipse( parent.self );
			return *this;
		}

		BUILDER_IMPLEMENT_VEC( EllipseBuilder, ellipse, glm::vec2 const &, radii )
		BUILDER_IMPLEMENT( EllipseBuilder, ellipse, uint32_t, subdivisions )

		EllipseBuilder &set_node_position( glm::vec2 const &pos ) {
			le_2d::le_2d_prim_i.set_node_position( self, &pos );
			return *this;
		}

		EllipseBuilder &set_filled( bool filled ) {
			le_2d::le_2d_prim_i.set_filled( self, filled );
			return *this;
		}

		EllipseBuilder &set_stroke_weight( float weight ) {
			le_2d::le_2d_prim_i.set_stroke_weight( self, weight );
			return *this;
		}

		EllipseBuilder &set_color( uint32_t r8g8b8a8_color ) {
			le_2d::le_2d_prim_i.set_color( self, r8g8b8a8_color );
			return *this;
		}

		EllipseBuilder &set_color( uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255 ) {
			uint32_t r8g8b8a8_color = uint32_t( r << 24 | g << 16 | b << 8 | a );
			le_2d::le_2d_prim_i.set_color( self, r8g8b8a8_color );
			return *this;
		}

		Le2D &draw() {
			return parent;
		}
	};

	EllipseBuilder mEllipseBuilder{*this};

	EllipseBuilder &ellipse() {
		return mEllipseBuilder.create();
	}

	// ---

	class ArcBuilder {
		Le2D &             parent;
		le_2d_primitive_o *self;

	  public:
		ArcBuilder( Le2D &parent_ )
		    : parent( parent_ ) {
		}

		ArcBuilder &create() {
			self = le_2d::le_2d_prim_i.create_arc( parent.self );
			return *this;
		}

		BUILDER_IMPLEMENT_VEC( ArcBuilder, arc, glm::vec2 const &, radii )
		BUILDER_IMPLEMENT( ArcBuilder, arc, uint32_t, subdivisions )
		BUILDER_IMPLEMENT( ArcBuilder, arc, float, angle_start_rad )
		BUILDER_IMPLEMENT( ArcBuilder, arc, float, angle_end_rad )

		ArcBuilder &set_node_position( glm::vec2 const &pos ) {
			le_2d::le_2d_prim_i.set_node_position( self, &pos );
			return *this;
		}

		ArcBuilder &set_filled( bool filled ) {
			le_2d::le_2d_prim_i.set_filled( self, filled );
			return *this;
		}

		ArcBuilder &set_stroke_weight( float weight ) {
			le_2d::le_2d_prim_i.set_stroke_weight( self, weight );
			return *this;
		}

		ArcBuilder &set_color( uint32_t r8g8b8a8_color ) {
			le_2d::le_2d_prim_i.set_color( self, r8g8b8a8_color );
			return *this;
		}

		ArcBuilder &set_color( uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255 ) {
			uint32_t r8g8b8a8_color = uint32_t( r << 24 | g << 16 | b << 8 | a );
			le_2d::le_2d_prim_i.set_color( self, r8g8b8a8_color );
			return *this;
		}

		Le2D &draw() {
			return parent;
		}
	};

	ArcBuilder mArcBuilder{*this};

	ArcBuilder &arc() {
		return mArcBuilder.create();
	}

	// ---

	class LineBuilder {
		Le2D &             parent;
		le_2d_primitive_o *self;

	  public:
		LineBuilder( Le2D &parent_ )
		    : parent( parent_ ) {
		}

		LineBuilder &create() {
			self = le_2d::le_2d_prim_i.create_line( parent.self );
			return *this;
		}

		BUILDER_IMPLEMENT_VEC( LineBuilder, line, glm::vec2 const &, p0 )
		BUILDER_IMPLEMENT_VEC( LineBuilder, line, glm::vec2 const &, p1 )

		LineBuilder &set_node_position( glm::vec2 const &pos ) {
			le_2d::le_2d_prim_i.set_node_position( self, &pos );
			return *this;
		}

		LineBuilder &set_stroke_weight( float weight ) {
			le_2d::le_2d_prim_i.set_stroke_weight( self, weight );
			return *this;
		}

		LineBuilder &set_color( uint32_t r8g8b8a8_color ) {
			le_2d::le_2d_prim_i.set_color( self, r8g8b8a8_color );
			return *this;
		}

		LineBuilder &set_color( uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255 ) {
			uint32_t r8g8b8a8_color = uint32_t( r << 24 | g << 16 | b << 8 | a );
			le_2d::le_2d_prim_i.set_color( self, r8g8b8a8_color );
			return *this;
		}

		Le2D &draw() {
			return parent;
		}
	};

	LineBuilder mLineBuilder{*this};

	LineBuilder &line() {
		return mLineBuilder.create();
	}

	// ---

	class PathBuilder {
		Le2D &             parent;
		le_2d_primitive_o *self;

	  public:
		PathBuilder( Le2D &parent_ )
		    : parent( parent_ ) {
		}

		PathBuilder &create() {
			self = le_2d::le_2d_prim_i.create_path( parent.self );
			return *this;
		}

		PathBuilder &move_to( glm::vec2 const &pos ) {
			le_2d::le_2d_prim_i.path_move_to( self, &pos );
			return *this;
		}

		PathBuilder &line_to( glm::vec2 const &pos ) {
			le_2d::le_2d_prim_i.path_line_to( self, &pos );
			return *this;
		}

		PathBuilder &quad_bezier_to( glm::vec2 const &p, glm::vec2 const &c1 ) {
			le_2d::le_2d_prim_i.path_quad_bezier_to( self, &p, &c1 );
			return *this;
		}

		PathBuilder &cubic_bezier_to( glm::vec2 const &p, glm::vec2 const &c1, glm::vec2 const &c2 ) {
			le_2d::le_2d_prim_i.path_cubic_bezier_to( self, &p, &c1, &c2 );
			return *this;
		}

		PathBuilder &add_from_simplified_svg( char const *svg ) {
			le_2d::le_2d_prim_i.path_add_from_simplified_svg( self, svg );
			return *this;
		}

		PathBuilder &set_node_position( glm::vec2 const &pos ) {
			le_2d::le_2d_prim_i.set_node_position( self, &pos );
			return *this;
		}

		PathBuilder &set_stroke_weight( float weight ) {
			le_2d::le_2d_prim_i.set_stroke_weight( self, weight );
			return *this;
		}

		PathBuilder &set_filled( bool filled ) {
			le_2d::le_2d_prim_i.set_filled( self, filled );
			return *this;
		}

		PathBuilder &set_color( uint32_t r8g8b8a8_color ) {
			le_2d::le_2d_prim_i.set_color( self, r8g8b8a8_color );
			return *this;
		}

		PathBuilder &set_color( uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255 ) {
			uint32_t r8g8b8a8_color = uint32_t( r << 24 | g << 16 | b << 8 | a );
			le_2d::le_2d_prim_i.set_color( self, r8g8b8a8_color );
			return *this;
		}

		Le2D &draw() {
			return parent;
		}
	};

	PathBuilder mPathBuilder{*this};

	PathBuilder &path() {
		return mPathBuilder.create();
	}
#	undef BUILDER_IMPLEMENT
#	undef BUILDER_IMPLEMENT_VEC
};

#endif // __cplusplus

#endif

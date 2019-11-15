#ifndef GUARD_le_2d_H
#define GUARD_le_2d_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#ifdef __cplusplus
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

	typedef float vec2f[2];

	struct le_2d_primitive_interface_t{

		void               ( *set_node_position) ( le_2d_primitive_o* p, vec2f const pos );

		#define SETTER_DECLARE( prim_type, field_type, field_name ) \
		void (  *prim_type##_set_##field_name)(le_2d_primitive_o* p, field_type field_name)\

		le_2d_primitive_o* ( *create_circle         ) ( le_2d_o* context);
		
		
		SETTER_DECLARE( circle, float, radius);
		SETTER_DECLARE( circle, uint32_t, subdivisions );
		SETTER_DECLARE( circle, bool, filled );

		le_2d_primitive_o* ( *create_line           ) ( le_2d_o* context);

		SETTER_DECLARE( line, vec2f, p0);
		SETTER_DECLARE( line, vec2f, p1);
		

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

using vec2f = le_2d_api::vec2f;

} // namespace le_2d

class Le2D : NoCopy, NoMove {

	le_2d_o *self;

  public:
	Le2D( le_command_buffer_encoder_o *encoder_ )
	    : self( le_2d::le_2d_i.create( encoder_ ) ) {
	}

	~Le2D() {
		le_2d::le_2d_i.destroy( self );
	}

	class CircleBuilder {
		Le2D &             parent;
		le_2d_primitive_o *self;

	  public:
		CircleBuilder( Le2D &parent_ )
		    : parent( parent_ )
		    , self( le_2d::le_2d_prim_i.create_circle( parent_.self ) ) {
		}

#	define BUILDER_IMPLEMENT( field_type, field_name )                      \
		CircleBuilder &set_##field_name( field_type field_name ) {           \
			le_2d::le_2d_prim_i.circle_set_##field_name( self, field_name ); \
			return *this;                                                    \
		}

		BUILDER_IMPLEMENT( float, radius )
		BUILDER_IMPLEMENT( uint32_t, subdivisions )
		BUILDER_IMPLEMENT( bool, filled )

#	undef BUILDER_IMPLEMENT

		CircleBuilder &set_node_position( le_2d_api::vec2f const &pos ) {
			le_2d::le_2d_prim_i.set_node_position( self, pos );
			return *this;
		}

		Le2D &draw() {
			return parent;
		}
	};

	CircleBuilder mCircleBuilder{*this};

	CircleBuilder &circle() {
		return mCircleBuilder;
	}

	class LineBuilder {
		Le2D &             parent;
		le_2d_primitive_o *self;

	  public:
		LineBuilder( Le2D &parent_ )
		    : parent( parent_ )
		    , self( le_2d::le_2d_prim_i.create_line( parent_.self ) ) {
		}

#	define BUILDER_IMPLEMENT( field_type, field_name )                    \
		LineBuilder &set_##field_name( field_type field_name ) {           \
			le_2d::le_2d_prim_i.line_set_##field_name( self, field_name ); \
			return *this;                                                  \
		}

		BUILDER_IMPLEMENT( le_2d_api::vec2f, p0 )
		BUILDER_IMPLEMENT( le_2d_api::vec2f, p1 )

#	undef BUILDER_IMPLEMENT

		LineBuilder &set_node_position( le_2d_api::vec2f const &pos ) {
			le_2d::le_2d_prim_i.set_node_position( self, pos );
			return *this;
		}

		Le2D &draw() {
			return parent;
		}
	};

	LineBuilder mLineBuilder{*this};

	LineBuilder &line() {
		return mLineBuilder;
	}
};

#endif // __cplusplus

#endif

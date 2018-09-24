#ifndef GUARD_le_camera_H
#define GUARD_le_camera_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#ifdef __cplusplus
extern "C" {
#endif

struct le_camera_o;
struct le_camera_controller_o;
struct le_mouse_event_data_o;

namespace le {
struct Viewport;
}

void register_le_camera_api( void *api );

// clang-format off
struct le_camera_api {
	static constexpr auto id      = "le_camera";
	static constexpr auto pRegFun = register_le_camera_api;

	struct le_camera_interface_t {

		le_camera_o *    ( * create                   ) ( );
		void             ( * destroy                  ) ( le_camera_o* self );
		void             ( * update                   ) ( le_camera_o* self );
		void             ( * set_view_matrix          ) ( le_camera_o* self, float const * view_matrix);
		void             ( * set_viewport             ) ( le_camera_o* self, le::Viewport const & viewport);
		void             ( * set_fov_radians          ) ( le_camera_o* self, float fov_radians);
		float            ( * get_fov_radians          ) ( le_camera_o* self );
		float const *    ( * get_view_matrix          ) ( le_camera_o* self);
		float const *    ( * get_projection_matrix    ) ( le_camera_o* self );
		float            ( * get_unit_distance        ) ( le_camera_o* self );
	};

	struct le_camera_controller_interface_t {

		le_camera_controller_o* ( * create          )( );
		void                    ( * destroy         )( le_camera_controller_o* self);
		void                    ( * update_camera   )( le_camera_controller_o* self, le_camera_o* camera, le_mouse_event_data_o const * mouse_event);
		void                    ( * set_control_rect )( le_camera_controller_o *self, float x, float y, float w, float h );
	};

	le_camera_interface_t       le_camera_i;
	le_camera_controller_interface_t le_camera_controller_i;
};
// clang-format on

#ifdef __cplusplus
} // extern c

namespace le_camera {
#	ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<le_camera_api>( true );
#	else
const auto api = Registry::addApiStatic<le_camera_api>();
#	endif

static const auto &le_camera_i            = api -> le_camera_i;
static const auto &le_camera_controller_i = api -> le_camera_controller_i;

} // namespace le_camera

class LeCamera : NoCopy, NoMove {

	le_camera_o *self;

  public:
	LeCamera()
	    : self( le_camera::le_camera_i.create() ) {
	}

	~LeCamera() {
		le_camera::le_camera_i.destroy( self );
	}

	float const *getViewMatrix() {
		return le_camera::le_camera_i.get_view_matrix( self );
	}

	float const *getProjectionMatrix() {
		return le_camera::le_camera_i.get_projection_matrix( self );
	}

	void setViewMatrix( float const *viewMatrix ) {
		le_camera::le_camera_i.set_view_matrix( self, viewMatrix );
	}

	float getUnitDistance() {
		return le_camera::le_camera_i.get_unit_distance( self );
	}

	void setViewport( le::Viewport const &viewport ) {
		le_camera::le_camera_i.set_viewport( self, viewport );
	}

	void setFovRadians( float fov_radians ) {
		le_camera::le_camera_i.set_fov_radians( self, fov_radians );
	}

	float getFovRadians() {
		le_camera::le_camera_i.get_fov_radians( self );
	}

	operator auto() {
		return self;
	}
};

class LeCameraController : NoCopy, NoMove {
	le_camera_controller_o *self;

  public:
	LeCameraController()
	    : self( le_camera::le_camera_controller_i.create() ) {
	}

	~LeCameraController() {
		le_camera::le_camera_controller_i.destroy( self );
	}

	void updateCamera( le_camera_o *camera, le_mouse_event_data_o *mouseEvent ) {
		le_camera::le_camera_controller_i.update_camera( self, camera, mouseEvent );
	}

	void setControlRect( float x, float y, float w, float h ) {
		le_camera::le_camera_controller_i.set_control_rect( self, x, y, w, h );
	}
};

#endif // __cplusplus

#endif

#include "le_camera.h"
#include "pal_api_loader/ApiRegistry.hpp"

#include "le_renderer/le_renderer.h" // for le::Viewport

#include <array>

#define GLM_ENABLE_EXPERIMENTAL
#include "gtc/matrix_transform.hpp"
#include <glm.hpp>

struct le_mouse_event_data_o {
	uint8_t   buttonState{};
	glm::vec2 cursor_pos;
};

struct le_camera_o {
	glm::mat4    matrix{}; // camera position in world space
	glm::mat4    projectionMatrix{};
	float        fovRadians{glm::radians( 60.f )}; // field of view angle (in radians)
	le::Viewport viewport{};                       // current camera viewport
	float        nearClip              = 10.f;
	float        farClip               = 10000.f;
	bool         projectionMatrixDirty = true; // whenever fovRadians changes, or viewport changes, this means that the projection matrix needs to be recalculated.
};

struct le_camera_controller_o {

	glm::mat4 matrix{}; // initial transform

	enum Mode {
		eNeutral = 0,
		eRotXY   = 1,
		eRotZ,
		eTranslateXY,
		eTranslateZ,
	};

	Mode                 mode{};
	std::array<float, 4> controlRect{}; // active rectangle for mouse inputs

	glm::vec2 mouse_pos_initial{}; // initial position of mouse on mouse_down
};

// ----------------------------------------------------------------------

static float const *camera_get_view_matrix( le_camera_o *self ) {
	return reinterpret_cast<float const *>( &self->matrix );
}

// ----------------------------------------------------------------------

static void camera_set_view_matrix( le_camera_o *self, float const *viewMatrix ) {
	self->matrix = *reinterpret_cast<glm::mat4 const *>( viewMatrix );
}

// ----------------------------------------------------------------------

static void camera_get_clip_distances( le_camera_o *self, float *nearClip, float *farClip ) {
	*nearClip = self->nearClip;
	*farClip  = self->farClip;
}

// ----------------------------------------------------------------------

static void camera_set_clip_distances( le_camera_o *self, float nearClip, float farClip ) {
	self->nearClip              = nearClip;
	self->farClip               = farClip;
	self->projectionMatrixDirty = true;
}

// ----------------------------------------------------------------------

static float const *camera_get_projection_matrix( le_camera_o *self ) {
	if ( self->projectionMatrixDirty ) {
		// cache projection matrix calculation
		self->projectionMatrix      = glm::perspective( self->fovRadians, float( self->viewport.width ) / float( self->viewport.height ), self->nearClip, self->farClip );
		self->projectionMatrixDirty = false;
	}
	return reinterpret_cast<float const *>( &self->projectionMatrix );
}

// ----------------------------------------------------------------------

static float camera_get_unit_distance( le_camera_o *self ) {
	return self->viewport.height / ( 2.f * tanf( self->fovRadians * 0.5f ) );
}

// ----------------------------------------------------------------------

/// rect is defined as x,y,w,h
static bool is_inside_rect( const glm::vec2 &pt, std::array<float, 4> const &rect ) noexcept {
	return ( pt.x >= rect[ 0 ] && pt.x <= ( rect[ 0 ] + rect[ 2 ] ) && pt.y >= rect[ 1 ] && pt.y <= ( rect[ 1 ] + rect[ 3 ] ) );
}

// ----------------------------------------------------------------------

static void camera_set_viewport( le_camera_o *self, le::Viewport const &viewport ) {
	self->viewport              = viewport;
	self->projectionMatrixDirty = true;
}

// ----------------------------------------------------------------------

static void camera_set_fov_radians( le_camera_o *self, float fov_radians ) {
	if ( fabsf( fov_radians - self->fovRadians ) > std::numeric_limits<float>().epsilon() ) {
		self->projectionMatrixDirty = true;
		self->fovRadians            = fov_radians;
	}
}

// ----------------------------------------------------------------------

static float camera_get_fov_radians( le_camera_o *self ) {
	return self->fovRadians;
}

// ----------------------------------------------------------------------

static void camera_controller_set_contol_rect( le_camera_controller_o *self, float x, float y, float w, float h ) {
	self->controlRect = {x, y, w, h};
}

// ----------------------------------------------------------------------

static void camera_controller_update_camera( le_camera_controller_o *controller, le_camera_o *camera, le_mouse_event_data_o const *mouse_event ) {

	// centre point of the mouse control rectangle
	glm::vec2 controlRectCentre{0.5f * ( controller->controlRect[ 0 ] + controller->controlRect[ 2 ] ),
		                        0.5f * ( controller->controlRect[ 1 ] + controller->controlRect[ 3 ] )};

	// distance 1/3 of small edge of control rectangle
	float controlCircleRadius = std::min( controller->controlRect[ 2 ], controller->controlRect[ 3 ] ) / 3.f;

	switch ( controller->mode ) {
	case le_camera_controller_o::eNeutral: {

		if ( false == is_inside_rect( mouse_event->cursor_pos, controller->controlRect ) ) {
			// if camera is outside the control rect, we don't care.
			return;
		};

		if ( mouse_event->buttonState & ( 0b111 ) ) {
			// A relevant mouse button has been pressed.
			// we must store the initial state of the camera.
			controller->matrix            = camera->matrix;
			controller->mouse_pos_initial = mouse_event->cursor_pos;
		}

		if ( mouse_event->buttonState & ( 1 << 0 ) ) {
			// -- change controller mode to either xy or z

			if ( glm::distance( mouse_event->cursor_pos, controlRectCentre ) < controlCircleRadius ) {
				// -- if mouse inside inner circle, control rotation XY
				controller->mode = le_camera_controller_o::eRotXY;
			} else {
				// -- if mouse outside inner circle, control rotation Z
				controller->mode = le_camera_controller_o::eRotZ;
			}

		} else if ( mouse_event->buttonState & ( 1 << 1 ) ) {
			// -- change mode to translate z
			controller->mode = le_camera_controller_o::eTranslateZ;
		} else if ( mouse_event->buttonState & ( 1 << 2 ) ) {
			// -- change mode ot translate xy
			controller->mode = le_camera_controller_o::eTranslateXY;
		}

	} break;
	case le_camera_controller_o::eRotXY: {
		if ( 0 == ( mouse_event->buttonState & ( 1 << 0 ) ) ) {
			// apply transforms, exit mode
			controller->mode = le_camera_controller_o::eNeutral;
		}

		// process normal logic for cursor position
		float normalised_distance_x = -( mouse_event->cursor_pos.x - controller->mouse_pos_initial.x ) / ( controlCircleRadius * 3.f ); // map to -1..1
		float normalised_distance_y = ( mouse_event->cursor_pos.y - controller->mouse_pos_initial.y ) / ( controlCircleRadius * 3.f );  // map to -1..1

		// build a quaternion based on rotation around x, rotation around y

		// first we must transform into the pivot point
		// the pivot point is a point which is at normdistance from the camera in negative z

		auto const &cam_to_world = controller->matrix;
		auto        world_to_cam = glm::inverse( cam_to_world );

		float normDistance = camera_get_unit_distance( camera );

		auto pivot   = glm::translate( world_to_cam, glm::vec3{0, 0, -normDistance} );
		pivot        = glm::rotate( pivot, glm::two_pi<float>() * normalised_distance_x, glm::vec3{0, 1, 0} );
		pivot        = glm::rotate( pivot, glm::two_pi<float>() * normalised_distance_y, glm::vec3{1, 0, 0} );
		world_to_cam = glm::translate( pivot, glm::vec3{0, 0, normDistance} );

		camera->matrix = glm::inverse( world_to_cam );

	} break;
	case le_camera_controller_o::eRotZ: {
		if ( 0 == ( mouse_event->buttonState & ( 1 << 0 ) ) ) {
			// -- apply transforms, exit mode
			controller->mode = le_camera_controller_o::eNeutral;
		}

		auto  mouseInitial      = controller->mouse_pos_initial - controlRectCentre;
		float mouseInitialAngle = glm::two_pi<float>() - fmodf( glm::two_pi<float>() + atan2f( mouseInitial.y, mouseInitial.x ), glm::two_pi<float>() ); // Range is expected to be 0..2pi, ccw

		auto mouseDelta = mouse_event->cursor_pos - controlRectCentre;

		float cameraAngle = glm::two_pi<float>() - fmodf( mouseInitialAngle + glm::two_pi<float>() + atan2f( mouseDelta.y, mouseDelta.x ), glm::two_pi<float>() ); // Range is expected to 0..2pi, ccw

		// first we must transform into the pivot point
		// the pivot point is a point which is at normdistance from the camera in negative z

		auto const &cam_to_world = controller->matrix;
		auto        world_to_cam = glm::inverse( cam_to_world );

		float normDistance = camera_get_unit_distance( camera );

		auto pivot = glm::translate( world_to_cam, glm::vec3{0, 0, -normDistance} );

		pivot        = glm::rotate( pivot, cameraAngle, glm::vec3{0, 0, 1} );
		world_to_cam = glm::translate( pivot, glm::vec3{0, 0, normDistance} );

		camera->matrix = glm::inverse( world_to_cam );

	} break;
	case le_camera_controller_o::eTranslateXY: {
		if ( 0 == ( mouse_event->buttonState & ( 1 << 1 ) ) ) {
			// -- apply transforms, exit mode
			controller->mode = le_camera_controller_o::eNeutral;
		}

		float normalised_distance_x = -( mouse_event->cursor_pos.x - controller->mouse_pos_initial.x ) / ( controlCircleRadius * 1.f ); // map to -1..1
		float normalised_distance_y = -( mouse_event->cursor_pos.y - controller->mouse_pos_initial.y ) / ( controlCircleRadius * 1.f ); // map to -1..1

		float movement_speed = 100;

		auto const &cam_to_world = controller->matrix;
		auto        world_to_cam = glm::inverse( cam_to_world );

		float normDistance = camera_get_unit_distance( camera );

		auto pivot     = glm::translate( world_to_cam, glm::vec3{0, 0, -normDistance} );
		pivot          = glm::translate( pivot, movement_speed * glm::vec3{normalised_distance_x, normalised_distance_y, 0} );
		world_to_cam   = glm::translate( pivot, glm::vec3{0, 0, normDistance} );
		camera->matrix = glm::inverse( world_to_cam );

	} break;
	case le_camera_controller_o::eTranslateZ: {
		if ( 0 == ( mouse_event->buttonState & ( 1 << 2 ) ) ) {
			// -- apply transforms, exit mode
			controller->mode = le_camera_controller_o::eNeutral;
		}

		float normalised_distance_y = ( mouse_event->cursor_pos.y - controller->mouse_pos_initial.y ) / ( controlCircleRadius * 1.f ); // map to -1..1

		float movement_speed = 100;

		auto const &cam_to_world = controller->matrix;
		auto        world_to_cam = glm::inverse( cam_to_world );

		float normDistance = camera_get_unit_distance( camera );

		auto pivot     = glm::translate( world_to_cam, glm::vec3{0, 0, -normDistance} );
		pivot          = glm::translate( pivot, movement_speed * glm::vec3{0, 0, normalised_distance_y} );
		world_to_cam   = glm::translate( pivot, glm::vec3{0, 0, normDistance} );
		camera->matrix = glm::inverse( world_to_cam );

	} break;
	} // end switch controller->mode
}

// ----------------------------------------------------------------------

static le_camera_o *le_camera_create() {
	auto self = new le_camera_o();
	return self;
}

// ----------------------------------------------------------------------

static void le_camera_destroy( le_camera_o *self ) {
	delete self;
}

// ----------------------------------------------------------------------

static le_camera_controller_o *camera_controller_create() {
	auto self = new le_camera_controller_o();
	return self;
}

// ----------------------------------------------------------------------

static void camera_controller_destroy( le_camera_controller_o *self ) {
	delete self;
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_camera_api( void *api ) {
	auto &le_camera_i = static_cast<le_camera_api *>( api )->le_camera_i;

	le_camera_i.create                = le_camera_create;
	le_camera_i.destroy               = le_camera_destroy;
	le_camera_i.get_projection_matrix = camera_get_projection_matrix;
	le_camera_i.get_unit_distance     = camera_get_unit_distance;
	le_camera_i.get_view_matrix       = camera_get_view_matrix;
	le_camera_i.get_projection_matrix = camera_get_projection_matrix;
	le_camera_i.set_viewport          = camera_set_viewport;
	le_camera_i.set_view_matrix       = camera_set_view_matrix;
	le_camera_i.set_fov_radians       = camera_set_fov_radians;
	le_camera_i.get_fov_radians       = camera_get_fov_radians;
	le_camera_i.get_clip_distances    = camera_get_clip_distances;
	le_camera_i.set_clip_distances    = camera_set_clip_distances;

	auto &le_camera_controller_i = static_cast<le_camera_api *>( api )->le_camera_controller_i;

	le_camera_controller_i.create           = camera_controller_create;
	le_camera_controller_i.destroy          = camera_controller_destroy;
	le_camera_controller_i.update_camera    = camera_controller_update_camera;
	le_camera_controller_i.set_control_rect = camera_controller_set_contol_rect;
}

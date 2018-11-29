#include "le_camera.h"
#include "pal_api_loader/ApiRegistry.hpp"

#include "le_renderer/private/le_renderer_types.h" // for le::Viewport
#include "le_ui_event/le_ui_event.h"

#include <array>

#define GLM_ENABLE_EXPERIMENTAL
#include "gtc/matrix_transform.hpp"
#include <glm.hpp>

#include <vector>

struct le_mouse_event_data_o {
	enum ModKeyFlag : uint8_t {
		MOD_KEY_FLAG_LEFT_SHIFT   = 1 << 0,
		MOD_KEY_FLAG_LEFT_CONTROL = 1 << 1,
	};
	uint8_t   buttonState{};
	uint8_t   modKeyMask{}; // keyboard modifiers for mouse, made up of ModFeyFlags
	glm::vec2 cursor_pos;
};

struct le_camera_o {
	glm::mat4                matrix{}; // camera position in world space
	glm::mat4                projectionMatrix{};
	float                    fovRadians{glm::radians( 60.f )}; // field of view angle (in radians)
	le::Viewport             viewport{};                       // current camera viewport
	float                    nearClip = 10.f;
	float                    farClip  = 10000.f;
	std::array<glm::vec4, 6> frustumPlane;                 // right,top,far,left,bottom,near
	bool                     projectionMatrixDirty = true; // whenever fovRadians changes, or viewport changes, this means that the projection matrix needs to be recalculated.
	bool                     frustumPlanesDirty    = true; // whenever projection matrix changes frustum planes must be re-calculated
};

struct le_camera_controller_o {

	glm::mat4 world_to_cam; // current camera node (== inverse camera view matrix) read this right-to-left, (in multiplication order: "cam to world")

	float pivotDistance    = 0;     // if we set pivotdistance to 0 this means that the camera rotates around its own axes, other values make the camera rotate around a pivot point
	bool  pivotDistanceSet = false; // if not set, will initialsise by distance (camera -> world origin) on first update

	float movement_speed = 10000; //

	enum Mode {
		eNeutral = 0,
		eRotXY   = 1,
		eRotZ,
		eTranslateXY,
		eTranslateZ,
	};

	Mode                 mode{};
	std::array<float, 4> controlRect{}; // active rectangle for mouse inputs

	le_mouse_event_data_o mouse_state;         // current mouse state
	glm::vec2             mouse_pos_initial{}; // initial position of mouse on mouse_down
};

// ----------------------------------------------------------------------

static float const *camera_get_projection_matrix( le_camera_o *self ); // ffdecl.

// ----------------------------------------------------------------------

static void update_frustum_planes( le_camera_o *self ) {

	if ( false == self->frustumPlanesDirty ) {
		return;
	}

	// invariant : frustum planes are dirty and must be re-calculated.

	if ( self->projectionMatrixDirty ) {
		// Force recalculation of projection matrix if dirty
		camera_get_projection_matrix( self );
	}

	glm::mat4 pM = self->projectionMatrix;

	auto fP = std::array<glm::vec4, 6>{};

	fP[ 0 ] = ( pM[ 3 ] - pM[ 0 ] ); // right
	fP[ 1 ] = ( pM[ 3 ] - pM[ 1 ] ); // top
	fP[ 2 ] = ( pM[ 3 ] - pM[ 2 ] ); // far
	fP[ 3 ] = ( pM[ 3 ] + pM[ 0 ] ); // left
	fP[ 4 ] = ( pM[ 3 ] + pM[ 1 ] ); // bottom
	fP[ 5 ] = ( pM[ 3 ] + pM[ 2 ] ); // near

	float fPL[ 6 ]{};
	for ( size_t i = 0; i != 6; i++ ) {
		// get the length (= magnitude of the .xyz part of the row), so that we can normalize later
		fPL[ i ] = glm::vec3( fP[ i ].x, fP[ i ].y, fP[ i ].z ).length();
	}

	for ( size_t i = 0; i < 6; i++ ) {
		// normalize by dividing each plane by its xyz length
		fP[ i ] = fP[ i ] / fPL[ i ];
	}

	// Frustum planes are now represented in their Hessian normal form, that is, each plane is
	// represented as a normal vector (xyz components) and a distance to origin (w component)

	// apply frustum plane equation to cache.
	std::swap( self->frustumPlane, fP );

	self->frustumPlanesDirty = false;
}

// ----------------------------------------------------------------------
// Calculates whether a sphere (given centre in camera space, and radius) is contained
// within the frustum. The calculation is conservative, meaning a sphere intersecting the
// frustum partially will pass the test.
static bool camera_get_sphere_in_frustum( le_camera_o *self, float const *pSphereCentreInCameraSpaceFloat3, float sphereRadius_ ) {
	bool inFrustum = true;

	update_frustum_planes( self );

	glm::vec4 sphereCentreInCameraSpace = {
	    pSphereCentreInCameraSpaceFloat3[ 0 ],
	    pSphereCentreInCameraSpaceFloat3[ 1 ],
	    pSphereCentreInCameraSpaceFloat3[ 2 ],
	    1,
	};

	for ( size_t i = 0; i != self->frustumPlane.size(); i++ ) {
		float signedDistance = glm::dot( self->frustumPlane[ i ], ( sphereCentreInCameraSpace ) );
		inFrustum &= ( signedDistance >= -sphereRadius_ );
	}

	return inFrustum;
}

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
	self->frustumPlanesDirty    = true;
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
		self->frustumPlanesDirty    = true;
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

// Orbits a camera around xy axis
// based on signed normalised xy
void camera_orbit_xy( le_camera_o *camera, glm::mat4 const &world_to_cam_start, glm::vec3 const &signedAnglesRad, float pivotDistance ) {
	// process normal logic for cursor position

	// build a quaternion based on rotation around x, rotation around y

	// first we must transform into the pivot point
	// the pivot point is a point which is at normdistance from the camera in negative z

	auto pivot        = glm::translate( world_to_cam_start, glm::vec3{0, 0, -pivotDistance} );
	pivot             = glm::rotate( pivot, signedAnglesRad.x, glm::vec3{0, 1, 0} );
	pivot             = glm::rotate( pivot, signedAnglesRad.y, glm::vec3{1, 0, 0} );
	auto world_to_cam = glm::translate( pivot, glm::vec3{0, 0, pivotDistance} );

	camera->matrix = glm::inverse( world_to_cam );
}

// ----------------------------------------------------------------------

void camera_orbit_z( le_camera_o *camera, glm::mat4 const &world_to_cam_start, glm::vec3 const &cameraAngleRad, float pivotDistance ) {
	// first we must transform into the pivot point
	// the pivot point is a point which is at normdistance from the camera in negative z

	auto pivot        = glm::translate( world_to_cam_start, glm::vec3{0, 0, -pivotDistance} );
	pivot             = glm::rotate( pivot, cameraAngleRad.z, glm::vec3{0, 0, 1} );
	auto world_to_cam = glm::translate( pivot, glm::vec3{0, 0, pivotDistance} );

	camera->matrix = glm::inverse( world_to_cam );
}

// ----------------------------------------------------------------------

void camera_translate_xy( le_camera_o *camera, glm::mat4 const &world_to_cam_start, glm::vec3 const &signedNorm, float movement_speed, float pivotDistance ) {

	auto pivot        = glm::translate( world_to_cam_start, glm::vec3{0, 0, -pivotDistance} );
	pivot             = glm::translate( pivot, movement_speed * glm::vec3{signedNorm.x, signedNorm.y, 0} );
	auto world_to_cam = glm::translate( pivot, glm::vec3{0, 0, pivotDistance} );

	camera->matrix = glm::inverse( world_to_cam );
}

// ----------------------------------------------------------------------

void camera_translate_z( le_camera_o *camera, glm::mat4 const &world_to_cam_start, glm::vec3 const &signedNorm, float movement_speed, float pivotDistance ) {

	auto pivot        = glm::translate( world_to_cam_start, glm::vec3{0, 0, -pivotDistance} );
	pivot             = glm::translate( pivot, movement_speed * glm::vec3{0, 0, signedNorm.z} );
	auto world_to_cam = glm::translate( pivot, glm::vec3{0, 0, pivotDistance} );
	camera->matrix    = glm::inverse( world_to_cam );
}

// ----------------------------------------------------------------------

static void camera_controller_update_camera( le_camera_controller_o *controller, le_camera_o *camera, const std::vector<LeUiEvent const *> &events ) {

	// Centre point of the mouse control rectangle
	glm::vec2 controlRectCentre{0.5f * ( controller->controlRect[ 0 ] + controller->controlRect[ 2 ] ),
		                        0.5f * ( controller->controlRect[ 1 ] + controller->controlRect[ 3 ] )};

	// Distance 1/3 of small edge of control rectangle
	float controlCircleRadius = std::min( controller->controlRect[ 2 ], controller->controlRect[ 3 ] ) / 3.f;

	le_mouse_event_data_o mouse_state = controller->mouse_state; // gather mouse state from previous

	if ( false == controller->pivotDistanceSet ) {
		glm::vec4 camInWorldPos      = glm::inverse( camera->matrix ) * glm::vec4( 0, 0, 0, 1 );
		controller->pivotDistance    = glm::length( camInWorldPos );
		controller->pivotDistanceSet = true;
	}

	for ( auto const &event : events ) {

		// -- accumulate mouse state

		switch ( event->event ) {
		case ( LeUiEvent::Type::eCursorPosition ): {
			auto &e                = event->cursorPosition;
			mouse_state.cursor_pos = {e.x, e.y};
		} break;
		case ( LeUiEvent::Type::eKey ): {
			auto &e = event->key;
			if ( e.key == LeUiEvent::NamedKey::eLeftShift ) {
				if ( e.action == LeUiEvent::ButtonAction::ePress ) {
					mouse_state.modKeyMask |= le_mouse_event_data_o::ModKeyFlag::MOD_KEY_FLAG_LEFT_SHIFT;
				} else if ( e.action == LeUiEvent::ButtonAction::eRelease ) {
					mouse_state.modKeyMask &= ~( le_mouse_event_data_o::ModKeyFlag::MOD_KEY_FLAG_LEFT_SHIFT );
				}
			} else if ( e.key == LeUiEvent::NamedKey::eLeftControl ) {
				if ( e.action == LeUiEvent::ButtonAction::ePress ) {
					mouse_state.modKeyMask |= le_mouse_event_data_o::ModKeyFlag::MOD_KEY_FLAG_LEFT_CONTROL;
				} else if ( e.action == LeUiEvent::ButtonAction::eRelease ) {
					mouse_state.modKeyMask &= ~( le_mouse_event_data_o::ModKeyFlag::MOD_KEY_FLAG_LEFT_CONTROL );
				}
			}
		} break;
		case ( LeUiEvent::Type::eMouseButton ): {
			auto &e = event->mouseButton;
			if ( e.action == LeUiEvent::ButtonAction::ePress ) {
				// set appropriate button flag
				mouse_state.buttonState |= ( 1 << e.button );

			} else if ( e.action == LeUiEvent::ButtonAction::eRelease ) {
				// null appropriate button flag
				mouse_state.buttonState &= ~( 1 << e.button );
				// set camera controller into neutral state if any button was released.
				controller->mode = le_camera_controller_o::eNeutral;
			}
		} break;
		default:
		    break;
		}

		glm::vec3 rotationDelta;
		glm::vec3 translationDelta;
		{
			auto  mouseInitial      = controller->mouse_pos_initial - controlRectCentre;
			float mouseInitialAngle = glm::two_pi<float>() - fmodf( glm::two_pi<float>() + atan2f( mouseInitial.y, mouseInitial.x ), glm::two_pi<float>() ); // Range is expected to be 0..2pi, ccw

			auto mouseDelta = mouse_state.cursor_pos - controlRectCentre;

			rotationDelta.x = glm::two_pi<float>() * -( mouse_state.cursor_pos.x - controller->mouse_pos_initial.x ) / ( controlCircleRadius * 3.f );                // map to -1..1
			rotationDelta.y = glm::two_pi<float>() * ( mouse_state.cursor_pos.y - controller->mouse_pos_initial.y ) / ( controlCircleRadius * 3.f );                 // map to -1..1
			rotationDelta.z = glm::two_pi<float>() - fmodf( mouseInitialAngle + glm::two_pi<float>() + atan2f( mouseDelta.y, mouseDelta.x ), glm::two_pi<float>() ); // Range is expected to 0..2pi, ccw

			translationDelta.x = -( mouse_state.cursor_pos.x - controller->mouse_pos_initial.x ) / ( controlCircleRadius * 1.f ); // map to -1..1
			translationDelta.y = -( mouse_state.cursor_pos.y - controller->mouse_pos_initial.y ) / ( controlCircleRadius * 1.f ); // map to -1..1
			translationDelta.z = ( mouse_state.cursor_pos.y - controller->mouse_pos_initial.y ) / ( controlCircleRadius * 1.f );  // map to -1..1
		}

		// -- update controller state machine based on accumulated mouse_state

		switch ( controller->mode ) {
		case le_camera_controller_o::eNeutral: {

			if ( false == is_inside_rect( mouse_state.cursor_pos, controller->controlRect ) ) {
				// if camera is outside the control rect, we don't care.
				continue;
			};

			if ( mouse_state.buttonState & ( 0b111 ) ) {
				// A relevant mouse button has been pressed.
				// we must store the initial state of the camera.
				controller->world_to_cam      = glm::inverse( camera->matrix );
				controller->mouse_pos_initial = mouse_state.cursor_pos;
			}

			if ( mouse_state.buttonState & ( 1 << 0 ) ) {
				// Left mouse button down
				if ( mouse_state.modKeyMask == 0 ) {
					// no modifier keys pressed.
					// -- change controller mode to either xy or z
					( glm::distance( mouse_state.cursor_pos, controlRectCentre ) < controlCircleRadius )
					    ? controller->mode = le_camera_controller_o::eRotXY // -- if mouse inside  inner circle, control rotation XY
					    : controller->mode = le_camera_controller_o::eRotZ  // -- if mouse outside inner circle, control rotation Z
					    ;
				} else if ( mouse_state.modKeyMask & le_mouse_event_data_o::MOD_KEY_FLAG_LEFT_SHIFT ) {
					// left shift key held down - this is equivalent to right mouse button action
					controller->mode = le_camera_controller_o::eTranslateZ;
				} else if ( mouse_state.modKeyMask & le_mouse_event_data_o::MOD_KEY_FLAG_LEFT_CONTROL ) {
					// left control key held down - this is equivalent to middle mouse button action
					controller->mode = le_camera_controller_o::eTranslateXY;
				}

			} else if ( mouse_state.buttonState & ( 1 << 1 ) ) {
				// -- change mode to translate z
				controller->mode = le_camera_controller_o::eTranslateZ;
			} else if ( mouse_state.buttonState & ( 1 << 2 ) ) {
				// -- change mode ot translate xy
				controller->mode = le_camera_controller_o::eTranslateXY;
			}

		} break;
		case le_camera_controller_o::eRotXY: {
			camera_orbit_xy( camera, controller->world_to_cam, rotationDelta, controller->pivotDistance );
		} break;
		case le_camera_controller_o::eRotZ: {
			camera_orbit_z( camera, controller->world_to_cam, rotationDelta, controller->pivotDistance );
		} break;
		case le_camera_controller_o::eTranslateXY: {
			float movement_speed = 100;
			camera_translate_xy( camera, controller->world_to_cam, translationDelta, controller->movement_speed, controller->pivotDistance );
		} break;
		case le_camera_controller_o::eTranslateZ: {
			float movement_speed = 100;
			camera_translate_z( camera, controller->world_to_cam, translationDelta, controller->movement_speed, controller->pivotDistance );
		} break;
		} // end switch controller->mode
	}

	controller->mouse_state = mouse_state; // store current mouse state
}

// ----------------------------------------------------------------------

static void camera_controller_process_events( le_camera_controller_o *controller, le_camera_o *camera, LeUiEvent const *events, size_t numEvents ) {

	LeUiEvent const *const events_end = events + numEvents;

	std::vector<LeUiEvent const *> filtered_events;

	filtered_events.reserve( numEvents );

	for ( auto event = events; event != events_end; event++ ) {
		if ( event->event == LeUiEvent::Type::eCursorPosition || event->event == LeUiEvent::Type::eMouseButton || event->event == LeUiEvent::Type::eKey ) {
			filtered_events.emplace_back( event );
		}
	}

	camera_controller_update_camera( controller, camera, filtered_events );
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

static void camera_controller_set_pivot_distance( le_camera_controller_o *self, float pivotDistance ) {
	self->pivotDistanceSet = true;
	self->pivotDistance    = pivotDistance;
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
	le_camera_i.get_sphere_in_frustum = camera_get_sphere_in_frustum;

	auto &le_camera_controller_i = static_cast<le_camera_api *>( api )->le_camera_controller_i;

	le_camera_controller_i.create             = camera_controller_create;
	le_camera_controller_i.destroy            = camera_controller_destroy;
	le_camera_controller_i.process_events     = camera_controller_process_events;
	le_camera_controller_i.set_control_rect   = camera_controller_set_contol_rect;
	le_camera_controller_i.set_pivot_distance = camera_controller_set_pivot_distance;
}

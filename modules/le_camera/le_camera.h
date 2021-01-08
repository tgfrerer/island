#ifndef GUARD_le_camera_H
#define GUARD_le_camera_H

#include "le_core/le_core.h"

#ifdef __cplusplus

#	ifndef ISL_ALLOW_GLM_TYPES
#		define ISL_ALLOW_GLM_TYPES 1
#	endif

// Life is terrible without 3d type primitives, so let's include some glm forward declarations

#	if ( ISL_ALLOW_GLM_TYPES == 1 )
#		include <glm/fwd.hpp>
#	endif

#endif

struct le_camera_o;
struct le_camera_controller_o;
struct le_mouse_event_data_o;

struct LeUiEvent; // defined in le_ui_event

namespace le {
struct Viewport;
}

// clang-format off
struct le_camera_api {

	struct le_camera_interface_t {

		le_camera_o *    ( * create                   ) ( );
		void             ( * destroy                  ) ( le_camera_o* self );
        le_camera_o *    ( * clone                    ) ( le_camera_o const * self );

		void             ( * update                   ) ( le_camera_o* self );
		void             ( * set_view_matrix          ) ( le_camera_o* self, float const * view_matrix);
		void             ( * set_viewport             ) ( le_camera_o* self, le::Viewport const & viewport);
		le::Viewport const & ( * get_viewport         ) ( le_camera_o* self);
		void             ( * set_fov_radians          ) ( le_camera_o* self, float fov_radians);
		float            ( * get_fov_radians          ) ( le_camera_o* self );
		void             ( * get_view_matrix          ) ( le_camera_o* self, float * p_matrix_4x4 );
        void             ( * set_is_orthographic      ) ( le_camera_o* self, bool is_ortographic);

#if ( ISL_ALLOW_GLM_TYPES == 1 )
		void              (* set_view_matrix_glm       )( le_camera_o* self, glm::mat4 const & view_matrix);
		glm::mat4 const & (* get_view_matrix_glm       )( le_camera_o* self);
		glm::mat4 const & (* get_projection_matrix_glm )( le_camera_o* self);
#endif

		void             ( * get_projection_matrix    ) ( le_camera_o* self, float * p_matrix_4x4 );
		float            ( * get_unit_distance        ) ( le_camera_o* self );
		void             ( * set_clip_distances       ) ( le_camera_o* self, float nearClip, float farClip);
		void             ( * get_clip_distances       ) ( le_camera_o* self, float *nearClip, float *farClip);
		bool             ( * get_sphere_in_frustum    ) ( le_camera_o *self, float const *pSphereCentreInCameraSpaceFloat3, float sphereRadius );
	};

	struct le_camera_controller_interface_t {

		le_camera_controller_o* ( *create             )( );
		void                    ( *destroy            )( le_camera_controller_o* self);
		void                    ( *process_events     )( le_camera_controller_o* self, le_camera_o* camera, LeUiEvent const * events, size_t numEvents);
		void                    ( *set_control_rect   )( le_camera_controller_o *self, float x, float y, float w, float h );
		void                    ( *set_pivot_distance )( le_camera_controller_o* self, float pivotDistance);
	};

	le_camera_interface_t       le_camera_i;
	le_camera_controller_interface_t le_camera_controller_i;
};
// clang-format on

#ifdef __cplusplus

LE_MODULE( le_camera );
LE_MODULE_LOAD_DEFAULT( le_camera );

namespace le_camera {
static const auto &api = le_camera_api_i;

static const auto &le_camera_i            = api -> le_camera_i;
static const auto &le_camera_controller_i = api -> le_camera_controller_i;

} // namespace le_camera

class LeCamera : NoMove {

	le_camera_o *self;

  public:
	LeCamera()
	    : self( le_camera::le_camera_i.create() ) {
	}

	~LeCamera() {
		le_camera::le_camera_i.destroy( self );
	}

	LeCamera( const LeCamera &rhs )
	    : self( le_camera::le_camera_i.clone( rhs.self ) ) {
	}

	LeCamera &operator=( const LeCamera &rhs ) = delete;

#	if ( ISL_ALLOW_GLM_TYPES == 1 )

	glm::mat4 const &getViewMatrixGlm() const {
		return le_camera::le_camera_i.get_view_matrix_glm( self );
	}

	glm::mat4 const &getProjectionMatrixGlm() const {
		return le_camera::le_camera_i.get_projection_matrix_glm( self );
	}

	void setViewMatrixGlm( glm::mat4 const &viewMatrix ) {
		le_camera::le_camera_i.set_view_matrix_glm( self, viewMatrix );
	}

#	endif

	void getViewMatrix( float *p_matrix_4x4 ) const {
		return le_camera::le_camera_i.get_view_matrix( self, p_matrix_4x4 );
	}

	void getProjectionMatrix( float *p_matrix_4x4 ) const {
		return le_camera::le_camera_i.get_projection_matrix( self, p_matrix_4x4 );
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

	le::Viewport const &getViewport() {
		return le_camera::le_camera_i.get_viewport( self );
	}

	void setFovRadians( float fov_radians ) {
		le_camera::le_camera_i.set_fov_radians( self, fov_radians );
	}

	void setIsOrthographic( bool is_orthographic ) {
		le_camera::le_camera_i.set_is_orthographic( self, is_orthographic );
	}
	float getFovRadians() {
		return le_camera::le_camera_i.get_fov_radians( self );
	}

	void getClipDistances( float *nearClip, float *farClip ) {
		le_camera::le_camera_i.get_clip_distances( self, nearClip, farClip );
	}

	void setClipDistances( float nearClip, float farClip ) {
		le_camera::le_camera_i.set_clip_distances( self, nearClip, farClip );
	}

	bool getSphereCentreInFrustum( float const *pSphereCentreInCameraSpaceFloat3, float sphereRadius_ ) {
		return le_camera::le_camera_i.get_sphere_in_frustum( self, pSphereCentreInCameraSpaceFloat3, sphereRadius_ );
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

	void processEvents( le_camera_o *camera, LeUiEvent const *events, size_t numEvents ) {
		le_camera::le_camera_controller_i.process_events( self, camera, events, numEvents );
	}

	void setControlRect( float x, float y, float w, float h ) {
		le_camera::le_camera_controller_i.set_control_rect( self, x, y, w, h );
	}

	/// Sets distance for pivot around which camera rotates
	///
	/// Setting a distance of 0 means that camera will rotate aound its own axis, greater
	/// distances make it orbit around a point at distance on the camera's negative z-axis.
	///
	/// By default, the pivotDistance is initialised on first updating a camera to the
	/// camera's global distance to world origin. Specifying a distance explicitly using
	/// this method disables this default behaviour.
	///
	void setPivotDistance( float pivotDistance ) {
		le_camera::le_camera_controller_i.set_pivot_distance( self, pivotDistance );
	}
};

#endif // __cplusplus

#endif

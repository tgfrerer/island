#include "asterisks_app.h"

#include "le_window.h"
#include "le_renderer.hpp"
#include "le_pipeline_builder.h"
#include "le_camera.h"
#include "le_ui_event.h"
#include "hershey.h"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/random.hpp"

#include "le_ecs.h"

#include <iostream>
#include <memory>
#include <sstream>
#include <string.h> // for memcpy
#include <stdio.h>  // for sprintf
#include <vector>
#include <algorithm>

constexpr float ASTERISK_SCALE = 22.f / 3.f;

struct user_input_o {
	int16_t  left_right_count; // accumulated left-or right impulses per frame
	int16_t  up_down_count;    // accumulated up-or down impulses per frame
	uint32_t shoot_count;      // accumulated shoot impulses per frame
	bool     left_key_down;
	bool     right_key_down;
	bool     up_key_down;
	bool     down_key_down;
};

enum class GameState {
	eInitial,
	ePlaying,
	eGameOver,
	eNextLevel,
};

struct asterisks_app_o {
	le::Window   window;
	le::Renderer renderer;
	uint64_t     frame_counter = 0;

	LeCamera           camera;
	LeCameraController cameraController;

	LeEcs ecs;

	LeEcsSystemId sysPrintPosAndName;
	LeEcsSystemId sysPhysics;
	LeEcsSystemId sysControlSpaceship;
	LeEcsSystemId sysDrawSpaceShip;
	LeEcsSystemId sysDrawProjectiles;
	LeEcsSystemId sysDrawAsterisks;
	LeEcsSystemId sysUpdateTimeLimited;
	LeEcsSystemId sysFetchAsterisks;
	LeEcsSystemId sysFetchProjectiles;
	LeEcsSystemId sysFetchSpaceships;
	LeEcsSystemId sysCollide;
	LeEcsSystemId sysUpdateExplosions;
	LeEcsSystemId sysDrawExplosions;

	GameState game_state = GameState::eInitial;
	uint32_t  state_age  = 0;
	uint32_t  level      = 0;
	int32_t   score      = 0;

	user_input_o input = {}; // accumulated user input over a frame
};

// clang-format off

LE_ECS_FLAG_COMPONENT(ExplosionComponent);
LE_ECS_FLAG_COMPONENT(ProjectileComponent);

LE_ECS_COMPONENT( ColliderComponent);
    float radius;
};

LE_ECS_COMPONENT( SpaceShipComponent );
    enum class State: uint32_t{
        eNeutral = 0,
        eRocketBurning,
    };
    State state;
};

LE_ECS_COMPONENT( TimeLimitedComponent);
    uint32_t age = 60; // once age is at 0, element will get killed
};

LE_ECS_COMPONENT( PositionOrientationComponent );
	glm::vec2 pos;
    float orientation;
};

LE_ECS_COMPONENT( VelocityComponent );
	glm::vec2 vel;
};

LE_ECS_COMPONENT(AsteriskComponent);
	int size = 3;
};

// clang-format on

// We have a virtual screen of 640x480 pixels.
//
// it will scale automatically when the window is scaled.
// the coordinate centre is centre screen,
// and +x goes to the right,
// and +y goes up.
//
static const glm::vec3 bg_vertices[] = {
    { -320, -240, 0 },
    { 320, 240, 0 },
    { -320, 240, 0 },
    { -320, -240, 0 },
    { 320, -240, 0 },
    { 320, 240, 0 },
};

static std::vector<glm::vec4> bg_colors( 6, glm::vec4( 0.1f, 0.1f, 0.1f, 1.f ) );

// clang-format off
// 8 vertices: spaceship
std::vector<glm::vec2> spaceship_vertices{
    { 10.000004, 0.000000 },
    { -4.965590, 5.000000 },
    { -2.867356, 1.942849 },
    { -2.524932, 0.010322 },
    { -2.907713, -2.100657 },
    { -4.999997, -5.000000 },
    { -5.003279, -2.930996 },
    { -4.965897, 2.920907 },
};
// 8 edges: spaceship
std::vector<uint16_t> spaceship_indices{
    5, 6,
    0, 1,
    0, 5,
    2, 3,
    3, 4,
    2, 7,
    4, 6,
    1, 7,
};

// Draw asterisks

// base radius: 1
// 15 vertices: asterisk
std::vector<glm::vec2> asterisk_vertices{
    { 0.211471, 0.964967 },
    { -0.216892, 0.964967 },
    { -0.422984, 0.519001 },
    { -0.878416, 0.466220 },
    { -1.003129, 0.075896 },
    { -0.645051, -0.221064 },
    { -0.759124, -0.710173 },
    { -0.428363, -0.959546 },
    { -0.010574, -0.665103 },
    { 0.444630, -0.964968 },
    { 0.775392, -0.715593 },
    { 0.645052, -0.221064 },
    { 1.003129, 0.075896 },
    { 0.878416, 0.466220 },
    { 0.412409, 0.529573 },
};
// 15 edges: asterisk
std::vector<uint16_t> asterisk_indices{
    0, 1,
    1, 2,
    2, 3,
    3, 4,
    4, 5,
    5, 6,
    6, 7,
    7, 8,
    8, 9,
    9, 10,
    10, 11,
    11, 12,
    12, 13,
    13, 14,
    0, 14,
};

std::vector<glm::vec2> explosion_vertices = {
    { 0.424264, 0.912497 },
    { -0.091548, 0.647235 },
    { -0.807759, 0.711388 },
    { -0.382871, 0.262172 },
    { -1.038108, 0.262453 },
    { -0.478898, -0.037421 },
    { -1.012726, -0.788877 },
    { -0.413788, -0.452342 },
    { -0.130088, -1.090687 },
    { 0.068600, -0.224566 },
    { 0.775391, -0.715593 },
    { 0.266106, -0.069486 },
    { 0.985639, 0.020511 },
    { 0.878416, 0.480794 },
    { 0.295811, 0.322610 },
};
// clang-format on

typedef asterisks_app_o app_o;
static void             app_reset_camera( app_o* self ); // ffdecl.

// ----------------------------------------------------------------------

// Returns the largest rectangle given an aspect radio, eg. 4:3,
// fitting into the frame rect. If it doesn't fit perfectly, it
// will get centered.
le::Rect2D le_rect_2d_fit_into( float const width_over_height, le::Rect2D const* frame_rect ) {

	le::Rect2D result{};
	float      target_aspect_ratio = float( frame_rect->width ) / float( frame_rect->height );

	// We can return early if the aspect ratios match: In that case, just return the frame.

	if ( target_aspect_ratio == width_over_height ) {
		result = *frame_rect;
		return result;
	}

	// ---------| invariant: aspect ratios don't match

	// Scale our source to frame.

	float width  = width_over_height * frame_rect->width;
	float height = 1.f * frame_rect->height;

	// Check if source fits inside the frame. If not, we must repeat the adjustment
	// before we can apply it.
	while ( height > frame_rect->height || width > frame_rect->width ) {
		if ( height > frame_rect->height ) {
			// if height does not fit
			// we must scale down height, and width until height == frame_rect->height
			height = frame_rect->height;
			width  = width_over_height * height;
		} else if ( width > frame_rect->width ) {
			width  = frame_rect->width;
			height = width / width_over_height;
		}
	}

	result.height = height;
	result.width  = width;

	// Center in x, and y
	result.x = float( frame_rect->width - width ) * 0.5f;
	result.y = float( frame_rect->height - height ) * 0.5f;

	// ----------| Post-conditon: result.width / result.height == width_over_height

	return result;
}

// ----------------------------------------------------------------------

static void app_initialize() {
	le::Window::init();
};

// ----------------------------------------------------------------------

static void app_terminate() {
	le::Window::terminate();
};

// ----------------------------------------------------------------------

static void app_remove_asterisks( app_o* self ) {
	// first we must remove any asterisks which are left on screen.
	std::vector<EntityId> asterisk_entities;

	self->ecs.system_set_method(
	    self->sysFetchAsterisks,
	    []( LE_ECS_READ_ONLY_PARAMS, void* user_data ) {
		    auto& asterisks = *static_cast<std::vector<EntityId>*>( user_data );
		    asterisks.push_back( entity );
	    } );

	self->ecs.update_system( self->sysFetchAsterisks, &asterisk_entities );

	for ( auto& e : asterisk_entities ) {
		self->ecs.remove_entity( e );
	}
}

// ----------------------------------------------------------------------

static void app_spawn_spaceship( app_o* app ) {

	// ---------| gamestate is either initial or gameover - we can issue a spaceship

	app->ecs.entity()
	    .add_component( SpaceShipComponent{ SpaceShipComponent::State::eNeutral } )
	    .add_component( PositionOrientationComponent() )
	    .add_component( VelocityComponent{ { 0.5f, 0.0f } } )
	    .add_component( ColliderComponent{ 5.f } );
}

// ----------------------------------------------------------------------

static void app_spawn_asterisks( app_o* app ) {

	// Todo: spawn asterisks in border region...

	size_t num_asterisks = roundf( ( app->level + 5.f ) * 1.33f );

	for ( size_t i = 0; i != num_asterisks; i++ ) {

		float x;
		float y;

		bool on_x = rand() % 2;
		if ( on_x ) {
			// on x - axis:
			x = glm::linearRand( -320, 320 );
			y = fmodf( glm::linearRand( -50, 50 ) + 480.f, 480.f ) - 240.f;
		} else {
			// on y - axis
			x = fmodf( glm::linearRand( -50, 50 ) + 640.f, 640.f ) - 320.f;
			y = glm::linearRand( -240, 240 );
		}

		int asterisk_size = ( abs( rand() ) % 4 + 1 );
		app->ecs.entity()
		    .add_component( AsteriskComponent{ asterisk_size } )
		    .add_component( PositionOrientationComponent{
		        { x, y },
		        0.34f } )
		    .add_component( VelocityComponent{ glm::circularRand( 0.5f ) } )
		    .add_component( ColliderComponent{ asterisk_size * ASTERISK_SCALE } );
	}
}

// ----------------------------------------------------------------------

static void app_start_game( app_o* app ) {

	app->level = 0;

	app->input = {};

	// setup spaceship entity
	app_spawn_spaceship( app );

	// add asterisks
	app_spawn_asterisks( app );
}

// ----------------------------------------------------------------------

static app_o* app_create() {
	auto app = new ( app_o );

	srand( 15 );

	le::Window::Settings settings;
	settings
	    .setWidth( 640 )
	    .setHeight( 480 )
	    .setTitle( "Island // AsterisksApp" );

	// create a new window
	app->window.setup( settings );

	app->renderer.setup( app->window );

	// Set up the camera
	app_reset_camera( app );

	// setup systems -
	// each of these is comparable to a method declaration.
	app->sysControlSpaceship =
	    app->ecs.system()
	        .add_write_components<PositionOrientationComponent>()
	        .add_write_components<VelocityComponent>()
	        .add_write_components<SpaceShipComponent>()
	        .build();

	app->sysPhysics =
	    app->ecs.system()
	        .add_write_components<PositionOrientationComponent>()
	        .add_read_components<VelocityComponent>()
	        .build();

	app->sysDrawAsterisks =
	    app->ecs.system()
	        .add_read_components<PositionOrientationComponent>()
	        .add_read_components<AsteriskComponent>()
	        .build();

	app->sysFetchAsterisks =
	    app->ecs.system()
	        .add_read_components<AsteriskComponent>()
	        .build();

	app->sysDrawProjectiles =
	    app->ecs.system()
	        .add_read_components<PositionOrientationComponent>()
	        .add_read_components<VelocityComponent>()
	        .add_read_components<ProjectileComponent>()
	        .build();

	app->sysUpdateTimeLimited =
	    app->ecs.system()
	        .add_write_components<TimeLimitedComponent>()
	        .build();

	app->sysDrawSpaceShip =
	    app->ecs.system()
	        .add_read_components<PositionOrientationComponent>()
	        .add_read_components<SpaceShipComponent>()
	        .build();

	app->sysFetchProjectiles =
	    app->ecs.system()
	        .add_read_components<PositionOrientationComponent>()
	        .add_read_components<ColliderComponent>()
	        .add_read_components<ProjectileComponent>()
	        .build();

	app->sysCollide =
	    app->ecs.system()
	        .add_write_components<PositionOrientationComponent>()
	        .add_write_components<VelocityComponent>()
	        .add_write_components<ColliderComponent>()
	        .add_write_components<AsteriskComponent>()
	        .build();

	app->sysFetchSpaceships =
	    app->ecs.system()
	        .add_read_components<PositionOrientationComponent>()
	        .add_read_components<ColliderComponent>()
	        .add_read_components<SpaceShipComponent>()
	        .build();

	app->sysDrawExplosions =
	    app->ecs.system()
	        .add_read_components<PositionOrientationComponent>()
	        .add_read_components<VelocityComponent>()
	        .add_read_components<TimeLimitedComponent>()
	        .add_read_components<ExplosionComponent>()
	        .build();

	app->input = {};

	app_start_game( app );
	app->game_state = GameState::ePlaying;

	return app;
}

// ----------------------------------------------------------------------

static void app_destroy( app_o* self ) {
	delete ( self );
}

// ----------------------------------------------------------------------

static void app_reset_camera( app_o* self ) {
	le::Extent2D extents{};
	self->renderer.getSwapchainExtent( &extents.width, &extents.height );
	self->camera.setViewport( { 0, 0, 640, 480, 0.f, 1.f } );
	self->camera.setFovRadians( glm::radians( 60.f ) ); // glm::radians converts degrees to radians
	glm::mat4 view_matrix = glm::lookAt( glm::vec3{ 0, 0, self->camera.getUnitDistance() }, glm::vec3{ 0 }, glm::vec3{ 0, 1, 0 } );
	self->camera.setViewMatrix( ( float* )( &view_matrix ) );
}

// ----------------------------------------------------------------------

static void render_pass_main_exec( le_command_buffer_encoder_o* encoder_, void* user_data ) {
	auto        app = static_cast<app_o*>( user_data );
	le::GraphicsEncoder encoder{ encoder_ };

	// -- Set up pipelines

	static auto pipeline_background =
	    LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
	        .addShaderStage(
	            LeShaderModuleBuilder( encoder.getPipelineManager() )
	                .setShaderStage( le::ShaderStage::eVertex )
	                .setSourceFilePath( "./local_resources/shaders/default.vert" )
	                .build() )
	        .addShaderStage(
	            LeShaderModuleBuilder( encoder.getPipelineManager() )
	                .setShaderStage( le::ShaderStage::eFragment )
	                .setSourceFilePath( "./local_resources/shaders/default.frag" )
	                .build() )
	        .withInputAssemblyState()
	        .end()
	        .withRasterizationState()
	        .end()
	        .build();

	static auto pipeline_line_art =
	    LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
	        .addShaderStage(
	            LeShaderModuleBuilder( encoder.getPipelineManager() )
	                .setShaderStage( le::ShaderStage::eVertex )
	                .setSourceFilePath( "./local_resources/shaders/line_art.vert" )
	                .build() )
	        .addShaderStage(
	            LeShaderModuleBuilder( encoder.getPipelineManager() )
	                .setShaderStage( le::ShaderStage::eFragment )
	                .setSourceFilePath( "./local_resources/shaders/line_art.frag" )
	                .build() )
	        .withInputAssemblyState()
	        .setTopology( le::PrimitiveTopology::eLineList )
	        .end()
	        .withRasterizationState()
	        .setPolygonMode( le::PolygonMode::eLine )
	        .end()
	        .build();

	// -- Draw Background

	auto       extents    = encoder.getRenderpassExtent();
	le::Rect2D screenRect = { 0, 0, extents.width, extents.height };
	screenRect            = le_rect_2d_fit_into( 4.f / 3.f, &screenRect ); // We want to fit a 4:3 image into the current viewport

	le::Viewport viewport = {
	    float( screenRect.x ),
	    float( screenRect.y ) + float( screenRect.height ),
	    float( screenRect.width ),
	    -float( screenRect.height ), // Note: We flip the viewport, so that +y is upwards.
	    0.f,
	    1.f,
	};

	app->camera.setViewport( viewport );

	struct MvpUbo {
		glm::mat4 model;
		glm::mat4 view;
		glm::mat4 projection;
	};

	MvpUbo mvp;
	mvp.model = glm::mat4( 1.f ); // identity matrix
	app->camera.getViewMatrix( ( float* )( &mvp.view ) );
	app->camera.getProjectionMatrix( ( float* )( &mvp.projection ) );

	encoder
	    .setViewports( 0, 1, &viewport )
	    .bindGraphicsPipeline( pipeline_background )
	    .setArgumentData( LE_ARGUMENT_NAME( "Mvp" ), &mvp, sizeof( MvpUbo ) )
	    .setVertexData( bg_vertices, sizeof( bg_vertices ), 0 )
	    .setVertexData( bg_colors.data(), bg_colors.size() * sizeof( glm::vec4 ), 1 )
	    .draw( 6 );

	// Draw the foreground - we will use the line_art pipeline for this.

	std::vector<glm::vec3> vertices;
	std::vector<uint16_t>  indices;

	struct DrawCapture {
		std::vector<glm::vec3>* vertices;
		std::vector<uint16_t>*  indices;
		void*                   user_data;
	};

	DrawCapture draw_capture{ &vertices, &indices, &spaceship_vertices };

	// Execute spaceship draw system

	app->ecs.system_set_method(
	    app->sysDrawSpaceShip, []( LE_ECS_READ_ONLY_PARAMS, void* user_data ) {
		    auto pos                = LE_ECS_GET_READ_PARAM( 0, PositionOrientationComponent );
		    auto state              = LE_ECS_GET_READ_PARAM( 1, SpaceShipComponent );
		    auto pCapture           = static_cast<DrawCapture*>( user_data );
		    auto spaceship_vertices = static_cast<std::vector<glm::vec2> const*>( pCapture->user_data );
		    auto vertex_offset      = pCapture->vertices->size();

		    // first apply orientation
		    glm::vec2 x_axis = { cos( pos->orientation ), sin( pos->orientation ) };
		    glm::vec2 y_axis = { -sin( pos->orientation ), cos( pos->orientation ) };
		    glm::mat2 rot    = { x_axis, y_axis };

		    // Then apply translation

		    for ( auto& v : *spaceship_vertices ) {
			    auto v2 = ( rot * v ) + pos->pos;
			    pCapture->vertices->emplace_back( v2.x, v2.y, 0 );
		    }

		    for ( auto const& si : spaceship_indices ) {
			    pCapture->indices->push_back( vertex_offset + si );
		    }

		    vertex_offset = pCapture->vertices->size();

		    if ( state->state == SpaceShipComponent::State::eRocketBurning ) {

			    auto v_0 = ( rot * glm::vec2( -8, 3 ) ) + pos->pos;
			    auto v_1 = ( rot * glm::vec2( -5, 0 ) ) + pos->pos;
			    auto v_2 = ( rot * glm::vec2( -8, -3 ) ) + pos->pos;

			    pCapture->vertices->emplace_back( v_0.x, v_0.y, 0 );
			    pCapture->vertices->emplace_back( v_1.x, v_1.y, 0 );
			    pCapture->vertices->emplace_back( v_2.x, v_2.y, 0 );

			    pCapture->indices->push_back( vertex_offset );
			    pCapture->indices->push_back( vertex_offset + 1 );
			    pCapture->indices->push_back( vertex_offset + 1 );
			    pCapture->indices->push_back( vertex_offset + 2 );
		    }
	    } );

	app->ecs.update_system( app->sysDrawSpaceShip, &draw_capture );

	draw_capture.user_data = nullptr;

	// Draw Projectiles
	app->ecs.system_set_method(
	    app->sysDrawProjectiles, []( LE_ECS_READ_ONLY_PARAMS, void* user_data ) {
		    auto pos = LE_ECS_GET_READ_PARAM( 0, PositionOrientationComponent );
		    // auto vel        = LE_ECS_GET_READ_PARAM( 1, VelocityComponent );
		    // auto projectile = LE_ECS_GET_READ_PARAM( 2, ProjectileComponent );

		    auto pCapture = static_cast<DrawCapture*>( user_data );

		    auto const vertex_offset = pCapture->vertices->size();

		    // projectile does not have an orientation, it takes its heading
		    // from its velocity
		    float vel_orientation = pos->orientation;

		    // first apply orientation
		    glm::vec2 x_axis = { cos( vel_orientation ), sin( vel_orientation ) };
		    glm::vec2 y_axis = { -sin( vel_orientation ), cos( vel_orientation ) };
		    glm::mat2 rot    = { x_axis, y_axis };

		    // Then apply translation

		    pCapture->vertices->emplace_back( pos->pos.x, pos->pos.y, 0 );
		    auto v2 = ( rot * glm::vec2{ 10, 0 } ) + pos->pos;
		    pCapture->vertices->emplace_back( v2.x, v2.y, 0 );

		    pCapture->indices->push_back( vertex_offset + 0 );
		    pCapture->indices->push_back( vertex_offset + 1 );
	    } );

	app->ecs.update_system( app->sysDrawProjectiles, &draw_capture );

	// Draw Asterisks

	draw_capture.user_data = &asterisk_vertices;

	app->ecs.system_set_method(
	    app->sysDrawAsterisks, []( LE_ECS_READ_ONLY_PARAMS, void* user_data ) {
		    auto pos      = LE_ECS_GET_READ_PARAM( 0, PositionOrientationComponent );
		    auto asterisk = LE_ECS_GET_READ_PARAM( 1, AsteriskComponent );

		    auto pCapture = static_cast<DrawCapture*>( user_data );
		    auto verts    = static_cast<std::vector<glm::vec2> const*>( pCapture->user_data );

		    auto const vertex_offset = pCapture->vertices->size();

		    float vel_orientation = pos->orientation;

		    // first apply orientation
		    glm::vec2 x_axis = { cos( vel_orientation ), sin( vel_orientation ) };
		    glm::vec2 y_axis = { -sin( vel_orientation ), cos( vel_orientation ) };
		    glm::mat2 rot    = { x_axis, y_axis };

		    assert( asterisk->size != 0 && "asterisk must not be of size 0" );

		    float scale = ASTERISK_SCALE * asterisk->size;

		    for ( auto& v : *verts ) {
			    auto v2 = scale * ( rot * v ) + pos->pos;
			    pCapture->vertices->emplace_back( v2.x, v2.y, 0 );
		    }

		    for ( auto const& a_i : asterisk_indices ) {
			    pCapture->indices->emplace_back( vertex_offset + a_i );
		    }
	    } );

	app->ecs.update_system( app->sysDrawAsterisks, &draw_capture );

	// Draw Explosions

	draw_capture.user_data = &explosion_vertices;

	app->ecs.system_set_method(
	    app->sysDrawExplosions, []( LE_ECS_READ_ONLY_PARAMS, void* user_data ) {
		    auto const& pos = *LE_ECS_GET_READ_PARAM( 0, PositionOrientationComponent );
		    auto const& vel = *LE_ECS_GET_READ_PARAM( 1, VelocityComponent );
		    auto const& age = *LE_ECS_GET_READ_PARAM( 2, TimeLimitedComponent );

		    auto pCapture = static_cast<DrawCapture*>( user_data );
		    auto verts    = static_cast<std::vector<glm::vec2> const*>( pCapture->user_data );

		    auto const vertex_offset = pCapture->vertices->size();

		    // we draw the explosion around the given position and apply scale based on age.

		    float vel_orientation = pos.orientation;
		    // first apply orientation
		    glm::vec2 x_axis = { cos( vel_orientation ), sin( vel_orientation ) };
		    glm::vec2 y_axis = { -sin( vel_orientation ), cos( vel_orientation ) };
		    glm::mat2 rot    = { x_axis, y_axis };

		    float scale = ( ( 30 - age.age ) / 30.f ) * 30.f;

		    for ( auto& v : *verts ) {
			    auto v2 = scale * ( rot * v ) + pos.pos;
			    pCapture->vertices->emplace_back( v2.x, v2.y, 0 );
			    v2 = ( scale * 1.1f ) * ( rot * v ) + pos.pos;
			    pCapture->vertices->emplace_back( v2.x, v2.y, 0 );
		    }

		    for ( size_t i = 0; i != verts->size(); i++ ) {
			    pCapture->indices->emplace_back( vertex_offset + i * 2 );
			    pCapture->indices->emplace_back( vertex_offset + i * 2 + 1 );
		    }
	    } );

	app->ecs.update_system( app->sysDrawExplosions, &draw_capture );

	{
		// Draw text overlay
		//
		// You find more about how the file format for our vector font works by
		// looking into hershey.h
		//
		// We use this font because it's a nice line-based vector font and
		// brings up fond memories of programming turbo pascal's graphics
		// mode.

		glm::vec2 cursor = glm::vec2{ 0 };

		auto draw_character = [ & ]( char c ) {
			int const* h = hershey_simplex[ c - 32 ];

			int num_verts   = *h++;
			int spacing_hor = *h++;

			uint16_t previous_index;
			bool     has_previous_index = false;

			for ( int i = 0; i != num_verts; i++ ) {
				int v_0 = *h++;
				int v_1 = *h++;

				if ( v_0 == v_1 && v_0 == -1 ) {
					// Lift pen
					has_previous_index = false;
					continue;
				}

				if ( has_previous_index ) {
					indices.push_back( previous_index );
					indices.push_back( ++previous_index );
				} else {
					previous_index = vertices.size();
				}

				glm::vec3 p{ cursor.x + v_0, cursor.y + v_1, 0 };
				vertices.push_back( p );

				has_previous_index = true;
			}

			cursor.x += spacing_hor;
		};

		char score[ 16 ]{};
		snprintf( score, 15, "%06u", app->score );

		cursor.x = 320 - 130;
		cursor.y = 240 - 35;
		for ( char const* c = score; *c != 0; c++ ) {
			draw_character( *c );
		}

		if ( app->game_state == GameState::eGameOver ) {
			char const* game_over_msg = "GAME OVER";
			cursor.x                  = -78;
			cursor.y                  = -15;
			for ( char const* c = game_over_msg; *c != 0; c++ ) {
				draw_character( *c );
			}
		}
	}

	// Actually excecute the draw operations - all draw commands

	encoder.bindGraphicsPipeline( pipeline_line_art )
	    .setLineWidth( 1.f )
	    .setArgumentData( LE_ARGUMENT_NAME( "Mvp" ), &mvp, sizeof( MvpUbo ) )
	    .setVertexData( vertices.data(), vertices.size() * sizeof( glm::vec3 ), 0 )
	    .setIndexData( indices.data(), sizeof( uint16_t ) * indices.size() )
	    .drawIndexed( indices.size(), 5 );
}

// ----------------------------------------------------------------------
static void app_process_ui_events( app_o* self ) {
	using namespace le_window;
	uint32_t         numEvents;
	LeUiEvent const* pEvents;
	window_i.get_ui_event_queue( self->window, &pEvents, &numEvents );

	std::vector<LeUiEvent> events{ pEvents, pEvents + numEvents };

	bool wantsToggle = false;

	for ( auto& event : events ) {
		switch ( event.event ) {
		case ( LeUiEvent::Type::eKey ): {
			auto& e = event.key;
			if ( e.action == LeUiEvent::ButtonAction::ePress ) {
				if ( e.key == LeUiEvent::NamedKey::eUp ) {
					self->input.up_key_down = true;
				} else if ( e.key == LeUiEvent::NamedKey::eDown ) {
					self->input.down_key_down = true;
				} else if ( e.key == LeUiEvent::NamedKey::eLeft ) {
					self->input.left_key_down = true;
				} else if ( e.key == LeUiEvent::NamedKey::eRight ) {
					self->input.right_key_down = true; // right means turn cw, which is negative angle
				}
			} else if ( e.action == LeUiEvent::ButtonAction::eRelease ) {
				if ( e.key == LeUiEvent::NamedKey::eUp ) {
					self->input.up_key_down = false;
				} else if ( e.key == LeUiEvent::NamedKey::eDown ) {
					self->input.down_key_down = false;
				} else if ( e.key == LeUiEvent::NamedKey::eLeft ) {
					self->input.left_key_down = false; // left means turn ccw, which is positive angle
				} else if ( e.key == LeUiEvent::NamedKey::eRight ) {
					self->input.right_key_down = false; // right means turn cw, which is negative angle
				} else if ( e.key == LeUiEvent::NamedKey::eSpace ) {
					self->input.shoot_count++;
				} else if ( e.key == LeUiEvent::NamedKey::eF11 ) {
					wantsToggle ^= true;
				} else if ( e.key == LeUiEvent::NamedKey::eC ) {
					glm::mat4 view_matrix;
					self->camera.getViewMatrix( ( float* )( &view_matrix ) );
					float distance_to_origin = glm::distance( glm::vec4{ 0, 0, 0, 1 }, glm::inverse( view_matrix ) * glm::vec4( 0, 0, 0, 1 ) );
					self->cameraController.setPivotDistance( distance_to_origin );
				} else if ( e.key == LeUiEvent::NamedKey::eX ) {
					self->cameraController.setPivotDistance( 0 );
				} else if ( e.key == LeUiEvent::NamedKey::eZ ) {
					app_reset_camera( self );
					glm::mat4 view_matrix;
					self->camera.getViewMatrix( ( float* )( &view_matrix ) );
					float distance_to_origin = glm::distance( glm::vec4{ 0, 0, 0, 1 }, glm::inverse( view_matrix ) * glm::vec4( 0, 0, 0, 1 ) );
					self->cameraController.setPivotDistance( distance_to_origin );
				}

			} // if ButtonAction == eRelease
			break;
		}
		case ( LeUiEvent::Type::eGamepad ): {

			static auto e_previous = event.gamepad;
			auto&       e          = event.gamepad;

			if ( e.gamepad_id == e_previous.gamepad_id ) {

				// Note that we ignore gamepad id - we assume only one gamepad is connected for this particular application
				e_previous.buttons = e_previous.buttons ^ e.buttons; // Update to only show changed state in previous

				if ( e_previous.get_button_at( le::UiEvent::NamedGamepadButton::eA ) ) {

					// We know that button A has changed - Now if the button was released,
					// this means that the current state for the button must be false,
					// in which case we want the gamepad to trigger a shot.
					if ( false == e.get_button_at( le::UiEvent::NamedGamepadButton::eA ) ) {
						self->input.shoot_count++;
					}
				};

				if ( e.axes[ uint8_t( le::UiEvent::NamedGamepadAxis::eRightTrigger ) ] > 0.5f ) {
					self->input.up_down_count++;
				}

				if ( e.axes[ uint8_t( le::UiEvent::NamedGamepadAxis::eLeftX ) ] > 0.5f ) {
					self->input.left_right_count--;
				} else if ( e.axes[ uint8_t( le::UiEvent::NamedGamepadAxis::eLeftX ) ] < -0.5 ) {
					self->input.left_right_count++;
				}

				e_previous = e;
			}

			break;
		}
		default:
			// do nothing
			break;
		}
	}

	if ( self->input.left_key_down ) {
		self->input.left_right_count++; // left means turn ccw, which is positive angle
	}

	if ( self->input.right_key_down ) {
		self->input.left_right_count--; // right means turn cw, which is negative angle
	}

	if ( self->input.up_key_down ) {
		self->input.up_down_count++;
	}

	if ( self->input.down_key_down ) {
		self->input.up_down_count--;
	}

	auto swapchainExtent = self->renderer.getSwapchainExtent();

	// activate this to enable interactive camera control
	if ( false ) {
		self->cameraController.setControlRect( 0, 0, float( swapchainExtent.width ), float( swapchainExtent.height ) );
		self->cameraController.processEvents( self->camera, events.data(), events.size() );
	}

	if ( wantsToggle ) {
		self->window.toggleFullscreen();
	}
}

// ----------------------------------------------------------------------

static bool app_update( app_o* self ) {

	// Poll all windows for events.
	le::Window::pollEvents();

	if ( self->window.shouldClose() ) {
		return false;
	}

	// Update interactive camera using mouse data
	app_process_ui_events( self );

	struct spaceship_control_io_t {
		app_o*    app;
		bool      shots_fired;
		glm::vec2 position;
		glm::vec2 velocity;
		float     orientation;
	};

	spaceship_control_io_t spaceship_control_io{};
	spaceship_control_io.app = self;

	// Spaceship control: Update spaceship velocity and orientation based on user input
	//
	self->ecs.system_set_method(
	    self->sysControlSpaceship, []( LE_ECS_WRITE_ONLY_PARAMS, void* user_data ) {
		    auto pos   = LE_ECS_GET_WRITE_PARAM( 0, PositionOrientationComponent );
		    auto vel   = LE_ECS_GET_WRITE_PARAM( 1, VelocityComponent );
		    auto state = LE_ECS_GET_WRITE_PARAM( 2, SpaceShipComponent );

		    auto io  = static_cast<spaceship_control_io_t*>( user_data );
		    auto app = io->app;

		    // Update our spaceship orientation based on user input

		    float orientation_change = float( app->input.left_right_count ) * 0.125;
		    pos->orientation += orientation_change;

		    // Update spaceship velocity based on change
		    glm::vec2 vel_change =
		        glm::vec2( cos( pos->orientation ), sin( pos->orientation ) ) *
		        float( app->input.up_down_count ) * 0.125f;

		    vel->vel += vel_change;

		    // State update: change state to burn if up_down_count > 0,

		    if ( state->state == SpaceShipComponent::State::eNeutral ) {
			    if ( app->input.up_down_count > 0 ) {
				    state->state = SpaceShipComponent::State::eRocketBurning;
			    }
		    } else if ( state->state == SpaceShipComponent::State::eRocketBurning ) {
			    if ( app->input.up_down_count <= 0 ) {
				    state->state = SpaceShipComponent::State::eNeutral;
			    }
		    }

		    io->orientation = pos->orientation;
		    io->position    = pos->pos;
		    io->velocity    = vel->vel;
		    io->shots_fired = app->input.shoot_count > 0;
	    } );

	self->ecs.update_system( self->sysControlSpaceship, &spaceship_control_io );

	// Emit new projectile components if shots fired
	if ( spaceship_control_io.shots_fired ) {

		// A shot costs 10 points.

		self->score = std::max( 0, self->score - 10 );

		// We never emit more than one shot per frame, but hey...
		float orientation = spaceship_control_io.orientation;

		glm::vec2 pos = spaceship_control_io.position;
		pos += glm::vec2( cos( orientation ), sin( orientation ) ) * 10.f;

		glm::vec2 velocity = spaceship_control_io.velocity +
		                     glm::vec2( cos( orientation ), sin( orientation ) ) * 7.5f; // 10 is standard velocity we give to projectiles...

		// Add projectile
		self->ecs.entity()
		    .add_component( ProjectileComponent{} )
		    .add_component( TimeLimitedComponent{ 60 } )
		    .add_component( PositionOrientationComponent{ pos, orientation } )
		    .add_component( VelocityComponent{ velocity } )
		    .add_component( ColliderComponent{ 2.f } );
	}

	// Update all elements which are time limited:
	// If they reach zero, they must be removed.

	{
		std::vector<EntityId> entity_kill_list;

		self->ecs.system_set_method(
		    self->sysUpdateTimeLimited, []( LE_ECS_WRITE_ONLY_PARAMS, void* user_data ) {
			    auto p         = LE_ECS_GET_WRITE_PARAM( 0, TimeLimitedComponent );
			    auto kill_list = static_cast<std::vector<EntityId>*>( user_data );
			    if ( p->age < 1 ) {
				    kill_list->push_back( entity );
			    }
			    p->age--;
		    } );

		self->ecs.update_system( self->sysUpdateTimeLimited, &entity_kill_list );

		// remove projectile entities from ecs which have been marked as inactive
		for ( auto& e : entity_kill_list ) {
			self->ecs.remove_entity( e );
		}
	}

	// Update physics system
	//
	self->ecs.system_set_method(
	    self->sysPhysics, []( LE_ECS_READ_WRITE_PARAMS, void* ) {
		    auto pos = LE_ECS_GET_WRITE_PARAM( 0, PositionOrientationComponent );
		    auto vel = LE_ECS_GET_READ_PARAM( 0, VelocityComponent );

		    static constexpr glm::vec2 screen_dims( 640, 480 );
		    pos->pos += vel->vel;
		    pos->pos = glm::mod( pos->pos + glm::vec2( 320, 240 ), screen_dims ) - glm::vec2( 320, 240 );
	    } );

	self->ecs.update_system( self->sysPhysics, self );

	// Apply collision detection

	// First, we get all colliders and collider-relevant data from the
	// ECS and copy it into an array, which we control.

	// Then, we iterate over that data and mark up elements which have collided
	// - how do we take note of different types of elements and how they collide?
	// can we test whether an entity contains a component?
	// we must fetch current values for spaceship position

	struct ProjectileData {
		glm::vec2 pos;
		float     radius;
		EntityId  id;
	};

	struct ExplosionData {
		glm::vec2 pos;
		glm::vec2 vel;
	};

	struct AsteriskData {
		glm::vec2 pos;
		glm::vec2 vel;
		int32_t   size;
	};

	struct SpaceshipCollisionData {
		glm::vec2 pos;
		float     radius;
		EntityId  id;
		bool      was_hit = false;
	};

	struct CollideData {
		std::vector<SpaceshipCollisionData> spaceship_data;
		std::vector<ProjectileData>         projectile_data;
		std::vector<EntityId>               kill_list;
		std::vector<AsteriskData>           new_asterisks; // list of asterisks to be createdd
		std::vector<ExplosionData>          new_explosions;
		uint32_t                            num_asterisks = 0;
		uint32_t                            score_delta   = 0;
	};

	CollideData collide_data{};

	// Fetch spaceships into collide_data
	self->ecs.system_set_method(
	    self->sysFetchSpaceships, []( LE_ECS_READ_ONLY_PARAMS, void* user_data ) {
		    auto  pos      = LE_ECS_GET_READ_PARAM( 0, PositionOrientationComponent );
		    auto  collider = LE_ECS_GET_READ_PARAM( 1, ColliderComponent );
		    auto& data     = *static_cast<CollideData*>( user_data );

		    data.spaceship_data.push_back( { pos->pos, collider->radius, entity, false } );
	    } );
	self->ecs.update_system( self->sysFetchSpaceships, &collide_data );

	// Fetch projectiles into collide_data
	self->ecs.system_set_method(
	    self->sysFetchProjectiles, []( LE_ECS_READ_ONLY_PARAMS, void* user_data ) {
		    auto  pos      = LE_ECS_GET_READ_PARAM( 0, PositionOrientationComponent );
		    auto  collider = LE_ECS_GET_READ_PARAM( 1, ColliderComponent );
		    auto& data     = *static_cast<CollideData*>( user_data );

		    data.projectile_data.push_back( { pos->pos, collider->radius, entity } );
	    } );
	self->ecs.update_system( self->sysFetchProjectiles,
	                         &collide_data );

	// Now, we have data for all projectiles in projectile_data.
	// We now test all projectiles against all asterisks.

	self->ecs.system_set_method(
	    self->sysCollide, []( LE_ECS_WRITE_ONLY_PARAMS, void* user_data ) {
		    auto& pos      = *LE_ECS_GET_WRITE_PARAM( 0, PositionOrientationComponent );
		    auto& vel      = *LE_ECS_GET_WRITE_PARAM( 1, VelocityComponent );
		    auto& collider = *LE_ECS_GET_WRITE_PARAM( 2, ColliderComponent );
		    auto& asterisk = *LE_ECS_GET_WRITE_PARAM( 3, AsteriskComponent );
		    auto& data     = *static_cast<CollideData*>( user_data );

		    data.num_asterisks++;

		    for ( auto const& p : data.projectile_data ) {
			    float radii_sum_sq = ( p.radius + collider.radius ) * ( p.radius + collider.radius ); // summed radii squared
			    float d_sq         = glm::dot( p.pos - pos.pos, p.pos - pos.pos );                    // distance squared
			    if ( d_sq < radii_sum_sq ) {

				    // Boom, we shot an asterisk!

				    ExplosionData explosion;
				    explosion.pos = p.pos;
				    explosion.vel = vel.vel;
				    data.new_explosions.emplace_back( explosion );

				    data.score_delta = std::max( 0, ( 4 - asterisk.size ) * 50 );

				    if ( asterisk.size > 1 ) {
					    asterisk.size--;

					    vel.vel = ( glm::vec2{ -vel.vel.y, vel.vel.x } + // start off with movement orthogonal to original
					                glm::circularRand( 0.25f ) ) *       // add a bit of randomness
					              1.25f;                                 // make smaller bits slightly faster.

					    collider.radius = asterisk.size * ASTERISK_SCALE;

					    AsteriskData twin_asterisk{};
					    twin_asterisk.vel  = -vel.vel; // moves in opposite direction
					    twin_asterisk.size = asterisk.size;

					    twin_asterisk.pos = pos.pos + twin_asterisk.vel * 4.f;
					    pos.pos += vel.vel * 4.f; // push away 2 velocities

					    data.new_asterisks.emplace_back( twin_asterisk );
					    data.num_asterisks++;
				    } else {
					    data.num_asterisks--;
					    data.kill_list.push_back( entity );
				    }
				    data.kill_list.push_back( p.id ); // put current projectile entity onto kill list
			    }
		    }

		    // Test whether an asterisk collides with spaceship
		    {
			    for ( auto& s : data.spaceship_data ) {

				    float radii_sum_sq = ( s.radius + collider.radius ) * ( s.radius + collider.radius ); // summed radii squared
				    float d_sq         = glm::dot( s.pos - pos.pos, s.pos - pos.pos );                    // distance squared
				    if ( d_sq < radii_sum_sq ) {
					    // Mayday - we've been hit!
					    s.was_hit = true;
					    // add spaceship entity to kill list.
					    // add an explosion where the spaceship used to be...
					    data.kill_list.push_back( s.id );
					    ExplosionData explosion;
					    explosion.pos = s.pos;
					    explosion.vel = vel.vel;
					    data.new_explosions.emplace_back( explosion );
				    }
			    }
		    }
	    } );

	self->ecs.update_system( self->sysCollide, &collide_data );

	// Remove entities from ecs which have been marked as inactive
	for ( auto& e : collide_data.kill_list ) {
		self->ecs.remove_entity( e );
	}

	// Spawn new asterisks which have been split off by explosion
	for ( auto const& a : collide_data.new_asterisks ) {
		self->ecs.entity()
		    .add_component( AsteriskComponent{ a.size } )
		    .add_component( PositionOrientationComponent{ a.pos, 0.34f } )
		    .add_component( VelocityComponent{ a.vel } )
		    .add_component( ColliderComponent{ a.size * ASTERISK_SCALE } );
	}

	// Spawn explosions for asterisks which have been hit
	for ( auto const& e : collide_data.new_explosions ) {
		self->ecs.entity()
		    .add_component( ExplosionComponent{} )
		    .add_component( TimeLimitedComponent{ 30 } )
		    .add_component( PositionOrientationComponent{ e.pos, 0 } )
		    .add_component( VelocityComponent{ e.vel } );
	}

	// End collision detection

	// Reset input events which have been processed.
	self->input.left_right_count = 0;
	self->input.up_down_count    = 0;
	self->input.shoot_count      = 0;

	{
		// Update game state machine. This happens in 3 steps.
		//
		// (1) Before we apply any updates to state machine,
		// we store the original state of the state machine.
		// this allows us to detect changes in step (3).
		//
		GameState original_game_state = self->game_state;

		// (2) Implement state machine logic: change states
		// depending on conditions based on current state.

		switch ( self->game_state ) {
		case GameState::eGameOver: {
			self->state_age++;
			if ( self->state_age > 180 ) {
				self->game_state = GameState::ePlaying;
				self->score      = 0;
			}
		} break;
		case GameState::eInitial: {
			self->game_state = GameState::ePlaying;
		} break;
		case GameState::eNextLevel: {
			self->game_state = GameState::ePlaying;
		} break;
		case GameState::ePlaying: {

			if ( !collide_data.spaceship_data.empty() &&
			     collide_data.spaceship_data.front().was_hit ) {
				self->game_state = GameState::eGameOver;
				break;
			}

			// ----------| Invariant: spaceship was not hit.

			self->score += collide_data.score_delta;

			if ( collide_data.num_asterisks == 0 ) {
				// No more asterisks left.
				self->game_state = GameState::eNextLevel;
			}

		} break;
		default:;
		}

		// (3) Implement one-time triggers for select state changes,
		// if state changed from one specific state to another.
		//
		// No state changes should happen in this step.

		if ( self->game_state != original_game_state ) {
			// Change detected, reset age of current stage.
			self->state_age = 0;
		}

		if ( self->game_state == GameState::eGameOver &&
		     original_game_state == GameState::ePlaying ) {
			std::cout << "Game Over. Final score: " << std::dec << self->score << "." << std::endl;
		}

		if ( self->game_state == GameState::eNextLevel &&
		     original_game_state == GameState::ePlaying ) {
			self->level++;
			std::cout << "Next Level: " << ( self->level + 1 ) << std::endl;
			app_spawn_asterisks( self );
		}

		if ( self->game_state == GameState::ePlaying &&
		     original_game_state == GameState::eGameOver ) {

			app_remove_asterisks( self );
			std::cout << "New Game" << std::endl;
			app_start_game( self );
		}
	}

	// Setup rendergraph

	static le_image_resource_handle LE_SWAPCHAIN_IMAGE_HANDLE = self->renderer.getSwapchainResource();

	le::RenderGraph renderGraph{};
	{

		auto renderpass_main =
		    le::RenderPass( "main", le::QueueFlagBits::eGraphics )
		        .addColorAttachment( LE_SWAPCHAIN_IMAGE_HANDLE )
		        .setSampleCount( le::SampleCountFlagBits::e8 )
		        .setExecuteCallback( self, render_pass_main_exec ) //
		    ;

		renderGraph
		    .addRenderPass( renderpass_main );
	}

	// Evaluate rendergraph and execute render callbacks for renderpasses
	self->renderer.update( renderGraph );

	self->frame_counter++;

	return true; // keep app alive
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( asterisks_app, api ) {
	auto  asterisks_app_api_i = static_cast<asterisks_app_api*>( api );
	auto& asterisks_app_i     = asterisks_app_api_i->asterisks_app_i;

	asterisks_app_i.initialize = app_initialize;
	asterisks_app_i.terminate  = app_terminate;

	asterisks_app_i.create  = app_create;
	asterisks_app_i.destroy = app_destroy;
	asterisks_app_i.update  = app_update;
}

#include "hello_world_app.h"

#include "le_window.h"
#include "le_ui_event.h"
#include "le_renderer.hpp"

#include "le_camera.h"
#include "le_pipeline_builder.h"

#include "le_mesh.h"
#include "le_mesh_generator.h"

#include "le_resource_manager.h"
#include "le_pixels.h"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtx/string_cast.hpp"

#include <iostream>
#include <memory>
#include <sstream>
#include <vector>
#include <chrono>
#include <array>

#if defined( _MSC_VER )
#	define ALIGNED_( x ) __declspec( align( x ) )
#else
#	if defined( __GNUC__ )
#		define ALIGNED_( x ) __attribute__( ( aligned( x ) ) )
#	endif
#endif

using NanoTime = std::chrono::time_point<std::chrono::high_resolution_clock>;

struct world_geometry_vertex_t {
	glm::vec3 pos;
	glm::vec3 normal;
	glm::vec2 uv;
	glm::vec3 tangent;
};

static constexpr std::array<le_mesh_attribute_info_t, 4> world_geometry_layout{ {
    { le_mesh_attribute_name::ePosition, sizeof( glm::vec3 ) },
    { le_mesh_attribute_name::eNormal, sizeof( glm::vec3 ) },
    { le_mesh_attribute_name::eUv, sizeof( glm::vec2 ) },
    { le_mesh_attribute_name::eTangent, sizeof( glm::vec3 ) },
} };

struct hello_world_app_o {
	le::Window   window;
	le::Renderer renderer;
	uint64_t     frame_counter = 0;
	le::Extent2D swapchain_extent;

	LeCameraController cameraController;
	LeCamera           camera;

	le_image_resource_handle imgEarthAlbedo  = nullptr;
	le_image_resource_handle imgEarthNight   = nullptr;
	le_image_resource_handle imgEarthClouds  = nullptr;
	le_image_resource_handle imgEarthNormals = nullptr;

	le_image_resource_handle depth_buffer = nullptr;

	le_texture_handle texEarthAlbedo;
	le_texture_handle texEarthNight;
	le_texture_handle texEarthClouds;
	le_texture_handle texEarthNormals;

	LeResourceManager resource_manager{ renderer };

	le::Mesh      worldGeometry;
	NanoTime      timeStamp{};
	double        timeDelta{};       // time since last frame, in s
	double        earthRotation = 0; // day/night cycle
	bool          animate       = true;
};

static const glm::vec4 sunInWorldSpace = glm::vec4{ -200000, 0, 0, 1.f };

// type, triggerpointOnAxis, positionOnAxis, radius
static glm::vec4 lensflareData[] = {
    { 4, 0.0, 0, 0.125 * 0.5 }, //< flare point
    { 3, 0.0, 0, 0.25 },        //< screen glare
    { 0, 0.0, 0.1, 0.800 * 0.75 },
    { 0, 0.9, 0.9, 0.1120 * 0.5 },
    { 0, 1.0, 0.78 + 0.0 * 0.25, 0.1300 * 0.5 },
    { 0, 1.2, 0.78 + 0.2 * 0.25, 0.1120 * 0.5 },
    { 0, 1.5, 0.78 + 0.5 * 0.25, 0.1300 * 0.5 },
    { 1, 0.25, -0.2, 0.250 },
    { 1, 0.1, 0.1, 0.170 },
    { 1, 0.52, 0.55, 0.200 }, ///< screen centre
    { 1, 1.1, 1.1, 0.250 },
    { 1, 1.5, 2.5, 0.300 },
    { 2, 1.9, 0.78, 0.12500 * 0.75 * 0.5 },
    { 2, 1.0, 0.78 + 0.1 * 0.25, 0.12400 * 0.75 },
    { 2, 1.2, 0.78 + 0.2 * 0.25, 0.1400 * 0.75 },
    { 2, 1.9, 0.78 + 0.5 * 0.25, 0.12500 * 0.75 },
};

// ----------------------------------------------------------------------

static void hello_world_app_process_ui_events( hello_world_app_o* self ); // ffdecl
static void reset_camera( hello_world_app_o* self );                      // ffdecl

// ----------------------------------------------------------------------

static hello_world_app_o* hello_world_app_create() {
	auto app = new ( hello_world_app_o );


	app->imgEarthAlbedo  = app->renderer.createImageResourceHandle( "imgEarthAlbedo" );
	app->imgEarthNight   = app->renderer.createImageResourceHandle( "imgEarthNight" );
	app->imgEarthClouds  = app->renderer.createImageResourceHandle( "ImgEarthClouds" );
	app->imgEarthNormals = app->renderer.createImageResourceHandle( "ImgEarthNormals" );

	app->depth_buffer = app->renderer.createImageResourceHandle( "DEPTH_BUFFER" );

	le::Window::Settings settings;
	settings
	    .setWidth( 1920 )
	    .setHeight( 1080 )
	    .setTitle( "Island // Hello world" );

	// create a new window
	app->window.setup( settings );

	app->renderer.setup( app->window );

	// -- Declare graphics pipeline state objects
	app->swapchain_extent = app->renderer.getSwapchainExtent();

	// Set up the camera
	reset_camera( app );

	{
		le::Mesh sphereMesh;
		// Generate geometry for earth sphere
		LeMeshGenerator::generateSphere( sphereMesh, 6360, 120, 120 ); // earth radius given in km.

		size_t   vertex_count    = sphereMesh.getVertexCount();
		uint32_t bytes_per_index = 2;
		size_t   index_count     = sphereMesh.getIndexCount( &bytes_per_index );

		app->worldGeometry.setVertexCount( vertex_count );

		// Assign sphere mesh data to world geometry mesh
		{
			void* data = app->worldGeometry.allocateVertexData( world_geometry_layout.data(), world_geometry_layout.size(), app->renderer );
			sphereMesh.readVertexDataIntoBuffer( data, vertex_count * sizeof( world_geometry_vertex_t ), world_geometry_layout.data(), world_geometry_layout.size() );
		}

		{
			void* data = app->worldGeometry.allocateIndexData( index_count, &bytes_per_index, app->renderer );
			sphereMesh.readIndexDataInto( data, index_count * bytes_per_index );
		}
	}

	// load pixels for earth albedo

	const char* image_paths[] = {
	    "./local_resources/images/world_winter.jpg",
	    "./local_resources/images/earth_city_lights_8192_rs.png",
	    "./local_resources/images/storm_clouds_8k.jpg",
	    "./local_resources/images/earthNormalMap_8k-sobel.tga",
	};

	app->resource_manager.add_item( app->imgEarthAlbedo, le::ImageInfoBuilder().setMipLevels( 10 ).build(), image_paths + 0 );
	app->resource_manager.add_item( app->imgEarthNight, le::ImageInfoBuilder().setMipLevels( 10 ).setFormat( le::Format::eR8Unorm ).build(), image_paths + 1 );
	app->resource_manager.add_item( app->imgEarthClouds, le::ImageInfoBuilder().setMipLevels( 10 ).build(), image_paths + 2 );
	app->resource_manager.add_item( app->imgEarthNormals, le::ImageInfoBuilder().setMipLevels( 10 ).setFormat( le::Format::eR16G16B16A16Unorm ).build(), image_paths + 3 );

	// initialise texture handles
	app->texEarthAlbedo  = app->renderer.produceTextureHandle( "texEarthAlbedo" );
	app->texEarthNight   = app->renderer.produceTextureHandle( "texEarthNight" );
	app->texEarthClouds  = app->renderer.produceTextureHandle( "texEarthClouds" );
	app->texEarthNormals = app->renderer.produceTextureHandle( "texEarthNormals" );

	// initialise app timer
	app->timeStamp = std::chrono::high_resolution_clock::now();

	return app;
}

// ----------------------------------------------------------------------

static void reset_camera( hello_world_app_o* self ) {
	self->camera.setViewport( { 0, 0, float( self->swapchain_extent.width ), float( self->swapchain_extent.height ), 0.f, 1.f } );
	self->camera.setClipDistances( 100.f, 150000.f );
	self->camera.setFovRadians( glm::radians( 25.f ) ); // glm::radians converts degrees to radians

	// glm::mat4 camMatrix = glm::lookAt( glm::vec3{30000, -10000, 20000}, glm::vec3{0}, glm::vec3{0, 1, 0} );
	glm::mat4 camMatrix = glm::mat4{ { 0.585995, 0.191119, 0.787454, -0.000000 }, { -0.049265, 0.978394, -0.200800, 0.000000 }, { -0.808816, 0.078874, 0.582749, -0.000000 }, { 3039.844482, 3673.605225, -15533.671875, 1.000000 } };
	// glm::mat4 camMatrix = glm::mat4{{-0.254149, 0.880418, 0.400359, -0.000000}, {0.633864, 0.464280, -0.618607, 0.000000}, {-0.730506, 0.096555, -0.676056, 0.000000}, {-792.769653, 1875.776367, -15593.370117, 1.000000}};
	self->camera.setViewMatrix( &camMatrix[ 0 ][ 0 ] );
}

// ----------------------------------------------------------------------

// Returns whether a ray from the sun is obscured by earth,
// If false, tells us the closest distance ray / earth centre
static bool hello_world_app_ray_cam_to_sun_hits_earth( hello_world_app_o* self, float& howClose ) {

	// We're following the recipe from
	// "Real-Time Rendering", by Akenine-Moeller et al., 3rd. ed. pp. 740

	// We send a ray from the camera to the sun and want to know if the
	// earth is in the way...

	const float visibleSunRadius = 200; // when to start showing the sun
	const float cEARTH_RADIUS    = 6360.f - visibleSunRadius;

	glm::mat4 viewMatrix;
	self->camera.getViewMatrix( &viewMatrix[ 0 ][ 0 ] );
	glm::vec4 camera_pos_world_space = glm::inverse( viewMatrix ) * glm::vec4( 0, 0, 0, 1 );
	glm::vec3 camToEarthCentre       = glm::vec3( 0, 0, 0 ) - glm::vec3( camera_pos_world_space );

	float distanceToEarthSquared = glm::dot( camToEarthCentre, camToEarthCentre );
	float earthRadiusSquared     = cEARTH_RADIUS * cEARTH_RADIUS - ( 500 * 500 ); // < we subtract a little so that the flare will appear a bit earlier.

	if ( distanceToEarthSquared < earthRadiusSquared ) {
		// this effectively means the ray origin is within the sphere.
		// there's no way we won't hit the sphere at some point,
		// so we can already return true here.
		howClose = -0;
		return true;
	}

	// --- invariant: ray origin is outside of sphere

	// ray goes from camera to sun
	glm::vec3 rayDirection                = glm::normalize( glm::vec3( sunInWorldSpace ) - glm::vec3( camera_pos_world_space ) );
	float     camToSphereProjectedOntoRay = glm::dot( rayDirection, camToEarthCentre );

	if ( camToSphereProjectedOntoRay < 0 ) {
		// a negative result means the sphere is behind the ray origin,
		// so we can reject the intersection here.
		howClose = -0; ///< we return -1 to signal that we're not even close to intersect.
		return false;
	}

	// ---- invariant: sphere is not behind ray origin

	float orthogonalDistanceSquared = distanceToEarthSquared - camToSphereProjectedOntoRay * camToSphereProjectedOntoRay;

	if ( orthogonalDistanceSquared > earthRadiusSquared ) {
		// intersection outside of sphere.
		howClose = sqrtf( orthogonalDistanceSquared - earthRadiusSquared );
		return false;
	} else {
		// we've been hit!
		howClose = -sqrtf( earthRadiusSquared - orthogonalDistanceSquared );
		return true;
	}
}

// ----------------------------------------------------------------------

typedef bool ( *renderpass_setup )( le_renderpass_o* pRp, void* user_data );

// ----------------------------------------------------------------------

static bool pass_main_setup( le_renderpass_o* pRp, void* user_data ) {
	auto rp  = le::RenderPass{ pRp };
	auto app = static_cast<hello_world_app_o*>( user_data );

	auto texInfoAlbedo =
	    le::ImageSamplerInfoBuilder()
	        .withImageViewInfo()
	        .setImage( app->imgEarthAlbedo )
	        .end()
	        .withSamplerInfo()
	        .setAddressModeU( le::SamplerAddressMode::eRepeat )
	        .setAddressModeV( le::SamplerAddressMode::eMirroredRepeat )
	        .setMaxLod( 10.f )
	        .end()
	        .build();

	auto texInfoNight =
	    le::ImageSamplerInfoBuilder()
	        .withImageViewInfo()
	        .setImage( app->imgEarthNight )
	        .end()
	        .withSamplerInfo()
	        .setAddressModeU( le::SamplerAddressMode::eRepeat )
	        .setAddressModeV( le::SamplerAddressMode::eMirroredRepeat )
	        .setMaxLod( 10.f )
	        .end()
	        .build();

	auto texInfoClouds =
	    le::ImageSamplerInfoBuilder()
	        .withImageViewInfo()
	        .setImage( app->imgEarthClouds )
	        .end()
	        .withSamplerInfo()
	        .setAddressModeU( le::SamplerAddressMode::eRepeat )
	        .setAddressModeV( le::SamplerAddressMode::eMirroredRepeat )
	        .setMaxLod( 10.f )
	        .end()
	        .build();

	auto texInfoNormals =
	    le::ImageSamplerInfoBuilder()
	        .withImageViewInfo()
	        .setImage( app->imgEarthNormals )
	        .end()
	        .withSamplerInfo()
	        .setAddressModeU( le::SamplerAddressMode::eRepeat )
	        .setAddressModeV( le::SamplerAddressMode::eClampToEdge )
	        .setMaxLod( 10.f )
	        .end()
	        .build();

	static le_image_resource_handle LE_SWAPCHAIN_IMAGE_HANDLE = app->renderer.getSwapchainResource();

	app->worldGeometry.setupRenderPass( rp );

	rp
	    .addColorAttachment( LE_SWAPCHAIN_IMAGE_HANDLE, le::ImageAttachmentInfoBuilder().setLoadOp( le::AttachmentLoadOp::eClear ).build() ) // color attachment
	    .addDepthStencilAttachment( app->depth_buffer )
	    .sampleTexture( app->texEarthAlbedo, texInfoAlbedo )
	    .sampleTexture( app->texEarthNight, texInfoNight )
	    .sampleTexture( app->texEarthNormals, texInfoNormals )
	    .sampleTexture( app->texEarthClouds, texInfoClouds );

	return true;
}

// ----------------------------------------------------------------------

static void pass_main_exec( le_command_buffer_encoder_o* encoder_, void* user_data ) {
	auto                app = static_cast<hello_world_app_o*>( user_data );
	le::GraphicsEncoder encoder{ encoder_ };

	le::Extent2D passExtent = encoder.getRenderpassExtent();

	le::Viewport viewports[ 1 ] = {
	    { 0.f, 0.f, float( passExtent.width ), float( passExtent.height ), 0.f, 1.f },
	};

	app->camera.setViewport( viewports[ 0 ] );

	le::Rect2D scissors[ 1 ] = {
	    { 0, 0, passExtent.width, passExtent.height },
	};

	struct CameraParams {
		glm::mat4 view;
		glm::mat4 projection;
	};

	struct ModelParams {
		ALIGNED_( 16 )
		glm::mat4 model;
		ALIGNED_( 16 )
		glm::vec4 sunInEyeSpace;
		ALIGNED_( 16 )
		glm::vec4 worldCentreInEyeSpace;
	};

	// Draw main scene
	if ( true ) {

		CameraParams cameraParams;
		ModelParams  earthParams;

		double speed           = 0.005; // degrees per millisecond
		double angularDistance = app->animate ? app->timeDelta * speed : 0;
		app->earthRotation     = fmod( app->earthRotation + angularDistance, 360.0 );

		earthParams.model = glm::mat4( 1.f );                                                                                    // identity matrix
		earthParams.model = glm::rotate( earthParams.model, glm::radians( -13.4f ), glm::vec3{ 0, 0, 1 } );                      // apply ecliptic
		earthParams.model = glm::rotate( earthParams.model, glm::radians( float( app->earthRotation ) ), glm::vec3{ 0, 1, 0 } ); // apply day/night rotation
		app->camera.getViewMatrix( &cameraParams.view[ 0 ][ 0 ] );
		app->camera.getProjectionMatrix( &cameraParams.projection[ 0 ][ 0 ] );

		glm::vec4 sourceInCameraSpace     = cameraParams.view * sunInWorldSpace;
		glm::vec4 worldCentreInWorldSpace = glm::vec4{ 0, 0, 0, 1 };
		glm::vec4 worldCentreInEyeSpace   = cameraParams.view * earthParams.model * worldCentreInWorldSpace;
		glm::vec4 sourceInClipSpace       = cameraParams.projection * sourceInCameraSpace;
		sourceInClipSpace                 = sourceInClipSpace / sourceInClipSpace.w; // Normalise

		earthParams.sunInEyeSpace         = sourceInCameraSpace;
		earthParams.worldCentreInEyeSpace = worldCentreInEyeSpace;

		// draw surface

		static le_gpso_handle pipelineEarthAlbedo = nullptr;
		if ( pipelineEarthAlbedo == nullptr ) {
			LeGraphicsPipelineBuilder builder( encoder.getPipelineManager() );
			builder
			    .addShaderStage(
			        LeShaderModuleBuilder( encoder.getPipelineManager() )
			            .setShaderStage( le::ShaderStage::eVertex )
			            .setSourceFilePath( "./local_resources/shaders/earth_albedo.vert" )
			            .build() )
			    .addShaderStage(
			        LeShaderModuleBuilder( encoder.getPipelineManager() )
			            .setShaderStage( le::ShaderStage::eFragment )
			            .setSourceFilePath( "./local_resources/shaders/earth_albedo.frag" )
			            .build() )

			    .withRasterizationState()
			    .setPolygonMode( le::PolygonMode::eFill )
			    .setCullMode( le::CullModeFlagBits::eBack )
			    .setFrontFace( le::FrontFace::eCounterClockwise )
			    .end()
			    .withInputAssemblyState()
			    .setTopology( le::PrimitiveTopology::eTriangleList )
			    .end()
			    .withDepthStencilState()
			    .setDepthTestEnable( true )
			    .end();

			app->worldGeometry.applyVertexInputDescriptions( builder, world_geometry_layout.data(), world_geometry_layout.size() );

			pipelineEarthAlbedo = builder.build();
		}

		encoder
		    .setScissors( 0, 1, scissors )
		    .setViewports( 0, 1, viewports )
		    .bindGraphicsPipeline( pipelineEarthAlbedo );

		uint32_t num_indices = app->worldGeometry.bind( encoder );

		encoder
		    .setArgumentData( LE_ARGUMENT_NAME( "CameraParams" ), &cameraParams, sizeof( CameraParams ) )
		    .setArgumentData( LE_ARGUMENT_NAME( "ModelParams" ), &earthParams, sizeof( ModelParams ) )
		    .setArgumentTexture( LE_ARGUMENT_NAME( "tex_unit_0" ), app->texEarthAlbedo )
		    .setArgumentTexture( LE_ARGUMENT_NAME( "tex_unit_1" ), app->texEarthNormals )
		    .setArgumentTexture( LE_ARGUMENT_NAME( "tex_unit_2" ), app->texEarthNight )
		    .setArgumentTexture( LE_ARGUMENT_NAME( "tex_clouds" ), app->texEarthClouds )
		    .drawIndexed( num_indices ) //
		    ;

		// draw atmosphere

		static le_gpso_handle pipelineEarthAtmosphere = nullptr;
		if ( pipelineEarthAtmosphere == nullptr ) {
			LeGraphicsPipelineBuilder builder( encoder.getPipelineManager() );
			builder.addShaderStage(
			           LeShaderModuleBuilder( encoder.getPipelineManager() )
			               .setShaderStage( le::ShaderStage::eVertex )
			               .setSourceFilePath( "./local_resources/shaders/earth_atmosphere.vert" )
			               .build() )
			    .addShaderStage(
			        LeShaderModuleBuilder( encoder.getPipelineManager() )
			            .setShaderStage( le::ShaderStage::eFragment )
			            .setSourceFilePath( "./local_resources/shaders/earth_atmosphere.frag" )
			            .build() )

			    .withRasterizationState()
			    .setPolygonMode( le::PolygonMode::eFill )
			    .setCullMode( le::CullModeFlagBits::eBack )
			    .setFrontFace( le::FrontFace::eCounterClockwise )
			    .end()
			    .withAttachmentBlendState()
			    .usePreset( le::AttachmentBlendPreset::eAdd )
			    .end()
			    .withDepthStencilState()
			    .setDepthTestEnable( true )
			    .setDepthWriteEnable( false )
			    .end()
			    .withMultiSampleState()
			    .setSampleShadingEnable( true )
			    .end();

			// Note that we only use the 3 first attributes
			app->worldGeometry.applyVertexInputDescriptions( builder, world_geometry_layout.data(), 3 );

			pipelineEarthAtmosphere = builder.build();
		}

		earthParams.model = glm::scale( earthParams.model, glm::vec3{ 1.025f } );

		encoder
		    .bindGraphicsPipeline( pipelineEarthAtmosphere )
		    .setArgumentData( LE_ARGUMENT_NAME( "ModelParams" ), &earthParams, sizeof( ModelParams ) )
		    .setArgumentData( LE_ARGUMENT_NAME( "CameraParams" ), &cameraParams, sizeof( CameraParams ) );

		// note: we are only binding the first 3 attributes
		num_indices = app->worldGeometry.bind( encoder, world_geometry_layout.data(), 3 );

		encoder.drawIndexed( num_indices );

		// let's check if sun is in clip space

		float howClose;

		bool hit = hello_world_app_ray_cam_to_sun_hits_earth( app, howClose );

		// std::cout << "Hit? " << ( hit ? "true  " : " false " ) << ", distance: " << howClose << std::endl
		//           << std::flush;

		if ( !hit && fabsf( howClose ) > 1000.f ) {

			struct LensflareParams {
				// uCanvas:
				// .x -> global canvas height (in pixels)
				// .y -> global canvas width (in pixels)
				// .z -> identity distance, that is the distance at which canvas is rendered 1:1
				ALIGNED_( 16 )
				glm::vec3 uCanvas;
				ALIGNED_( 16 )
				glm::vec3 uLensflareSource; ///< source of flare in screen space
				float     uHowClose;
			};

			static auto pipelineLensflares =
			    LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
			        .addShaderStage(
			            LeShaderModuleBuilder( encoder.getPipelineManager() )
			                .setShaderStage( le::ShaderStage::eVertex )
			                .setSourceFilePath( "./local_resources/shaders/lensflare.vert" )
			                .build() )
			        .addShaderStage(
			            LeShaderModuleBuilder( encoder.getPipelineManager() )
			                .setShaderStage( le::ShaderStage::eFragment )
			                .setSourceFilePath( "./local_resources/shaders/lensflare.frag" )
			                .build() )
			        .addShaderStage(
			            LeShaderModuleBuilder( encoder.getPipelineManager() )
			                .setShaderStage( le::ShaderStage::eGeometry )
			                .setSourceFilePath( "./local_resources/shaders/lensflare.geom" )
			                .build() )

			        .withRasterizationState()
			        .setPolygonMode( le::PolygonMode::eFill )
			        .setCullMode( le::CullModeFlagBits::eNone )
			        .end()
			        .withInputAssemblyState()
			        .setTopology( le::PrimitiveTopology::ePointList )
			        .end()
			        .withAttachmentBlendState( 0 )
			        .usePreset( le::AttachmentBlendPreset::eAdd )
			        .end()
			        .withDepthStencilState()
			        .setDepthTestEnable( false )
			        .end()
			        .build();

			LensflareParams params{};
			params.uCanvas.x        = passExtent.width;
			params.uCanvas.y        = passExtent.height;
			params.uCanvas.z        = app->camera.getUnitDistance();
			params.uLensflareSource = sourceInClipSpace;
			params.uHowClose        = howClose;

			encoder
			    .bindGraphicsPipeline( pipelineLensflares )
			    .setArgumentData( LE_ARGUMENT_NAME( "CameraParams" ), &cameraParams, sizeof( CameraParams ) )
			    .setArgumentData( LE_ARGUMENT_NAME( "LensflareParams" ), &params, sizeof( LensflareParams ) )
			    .setVertexData( lensflareData, sizeof( lensflareData ), 0 )
			    .draw( sizeof( lensflareData ) / sizeof( glm::vec4 ) ) //
			    ;
		} // end inFrustum
	}     // end draw main scene
}

// ----------------------------------------------------------------------

static bool hello_world_app_update( hello_world_app_o* self ) {

	// Polls events for all windows -
	// This means any window may trigger callbacks for any events they have callbacks registered.
	le::Window::pollEvents();

	if ( self->window.shouldClose() ) {
		return false;
	}

	self->cameraController.setControlRect( 0, 0, float( self->swapchain_extent.width ), float( self->swapchain_extent.height ) );

	hello_world_app_process_ui_events( self );

	static bool resetCameraOnReload = false; // reload meand module reload
	if ( resetCameraOnReload ) {
		// Reset camera
		reset_camera( self );
		resetCameraOnReload = false;
	}
	//	self->cameraController.setPivotDistance( 0 );

	auto now        = std::chrono::high_resolution_clock::now();
	auto time_delta = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>( now - self->timeStamp ).count();

	//	std::cout << std::dec << time_delta << "ms per frame. FPS: " << 1000. / time_delta << std::endl
	//	          << std::flush;

	// self->timeDelta = time_delta;
	self->timeDelta = 1000. / 60.;
	self->timeStamp = now;

	le::RenderGraph renderGraph{};
	{

		self->resource_manager.update( renderGraph );

		le_mesh_o* meshes[] = {
		    self->worldGeometry,
		};

		le::Mesh::submitMeshesToRendergraph( meshes, 1, renderGraph, self->renderer );

		le::RenderPass renderPassFinal( "mainPass", le::QueueFlagBits::eGraphics );
		renderPassFinal
		    .setSetupCallback( self, pass_main_setup )
		    .setSampleCount( le::SampleCountFlagBits::e4 )
		    .setExecuteCallback( self, pass_main_exec ) //
		    ;

		renderGraph
		    .addRenderPass( renderPassFinal );

		renderGraph
		    .declareResource( self->depth_buffer, le::ImageInfoBuilder().setUsageFlags( le::ImageUsageFlags( le::ImageUsageFlagBits::eDepthStencilAttachment ) ).build() ) //
		    ;
	}

	// Update will call all rendercallbacks in this module.
	// the RECORD phase is guaranteed to execute - all rendercallbacks will get called.
	self->renderer.update( renderGraph );

	self->frame_counter++;

	return true; // keep app alive
}

// ----------------------------------------------------------------------
static void hello_world_app_process_ui_events( hello_world_app_o* self ) {
	using namespace le_window;
	uint32_t         numEvents;
	LeUiEvent const* pEvents;
	window_i.get_ui_event_queue( self->window, &pEvents, &numEvents );

	std::vector<LeUiEvent> events{ pEvents, pEvents + numEvents };

	bool         wants_toggle = false;
	bool         was_resized  = false;
	le::Extent2D window_extents;

	for ( auto& event : events ) {
		switch ( event.event ) {
		case ( LeUiEvent::Type::eWindowExtent ): {
			auto& e        = event.windowExtent;
			window_extents = {
			    .width  = e.width,
			    .height = e.height,
			};
			was_resized = true;
		} break;
		case ( LeUiEvent::Type::eKey ): {
			auto& e = event.key;
			if ( e.action == LeUiEvent::ButtonAction::eRelease ) {
				if ( e.key == LeUiEvent::NamedKey::eF11 ) {
					wants_toggle ^= true;
				} else if ( e.key == LeUiEvent::NamedKey::eZ ) {
					reset_camera( self );
					glm::mat4x4 view_matrix;
					self->camera.getViewMatrix( ( float* )( &view_matrix ) );
					float distance_to_origin = glm::distance( glm::vec4{ 0, 0, 0, 1 }, glm::inverse( view_matrix ) * glm::vec4( 0, 0, 0, 1 ) );
					self->cameraController.setPivotDistance( distance_to_origin );
				} else if ( e.key == LeUiEvent::NamedKey::eX ) {
					self->cameraController.setPivotDistance( 0 );
				} else if ( e.key == LeUiEvent::NamedKey::eC ) {
					glm::mat4x4 view_matrix;
					self->camera.getViewMatrix( &view_matrix[ 0 ][ 0 ] );
					float distance_to_origin = glm::distance( glm::vec4{ 0, 0, 0, 1 }, glm::inverse( view_matrix ) * glm::vec4( 0, 0, 0, 1 ) );
					self->cameraController.setPivotDistance( distance_to_origin );
				} else if ( e.key == LeUiEvent::NamedKey::eA ) {
					self->animate ^= true;
				} else if ( e.key == LeUiEvent::NamedKey::eP ) {
					// print out current camera view matrix
					//					std::cout << "View matrix:" << glm::to_string( self->camera.getViewMatrixGlm() ) << std::endl
					//					          << std::flush;
					//					std::cout << "camera node matrix:" << glm::to_string( glm::inverse( self->camera.getViewMatrixGlm() ) ) << std::endl
					//					          << std::flush;
				}
			} // if ButtonAction == eRelease

		} break;
		default:
			// do nothing
			break;
		}
	}

	if ( was_resized ) {
		self->renderer.resizeSwapchain( window_extents.width, window_extents.height );
		self->swapchain_extent = window_extents;
	}

	self->cameraController.processEvents( self->camera, events.data(), events.size() );

	if ( wants_toggle ) {
		self->window.toggleFullscreen();
	}
}

// ----------------------------------------------------------------------

static void hello_world_app_destroy( hello_world_app_o* self ) {
	delete ( self ); // deletes camera
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

LE_MODULE_REGISTER_IMPL( hello_world_app, api ) {
	auto  hello_world_app_api_i = static_cast<hello_world_app_api*>( api );
	auto& hello_world_app_i     = hello_world_app_api_i->hello_world_app_i;

	hello_world_app_i.initialize = app_initialize;
	hello_world_app_i.terminate  = app_terminate;

	hello_world_app_i.create  = hello_world_app_create;
	hello_world_app_i.destroy = hello_world_app_destroy;
	hello_world_app_i.update  = hello_world_app_update;
}

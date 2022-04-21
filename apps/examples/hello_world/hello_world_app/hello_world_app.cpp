#include "hello_world_app.h"

#include "le_window.h"
#include "le_ui_event.h"
#include "le_renderer.h"

#include "le_camera.h"
#include "le_pipeline_builder.h"

#include "le_mesh.h"
#include "le_mesh_generator.h"

#include "le_resource_manager.h"
#include "le_pixels.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // vulkan clip space is from 0 to 1
#define GLM_FORCE_RIGHT_HANDED      // glTF uses right handed coordinate system, and we're following its lead.
#define GLM_ENABLE_EXPERIMENTAL

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

struct WorldGeometry {
	le_buf_resource_handle  vertex_buffer_handle = LE_BUF_RESOURCE( "WORLD_VERTICES" );
	le_resource_info_t      vertex_buffer_info   = {};
	std::array<uint64_t, 4> buffer_offsets       = {};
	size_t                  vertexDataByteCount  = {}; // total byte count of vertex data
	size_t                  vertexCount          = 0;  // number of Vertices
	le_buf_resource_handle  index_buffer_handle  = LE_BUF_RESOURCE( "WORLD_INDICES" );
	le_resource_info_t      index_buffer_info    = {};
	size_t                  indexDataByteCount   = {};
	size_t                  indexCount           = {}; // number of indices
	bool                    wasLoaded            = false;
};

static le_img_resource_handle imgEarthAlbedo  = LE_IMG_RESOURCE( "imgEarthAlbedo" );
static le_img_resource_handle imgEarthNight   = LE_IMG_RESOURCE( "imgEarthNight" );
static le_img_resource_handle imgEarthClouds  = LE_IMG_RESOURCE( "ImgEarthClouds" );
static le_img_resource_handle imgEarthNormals = LE_IMG_RESOURCE( "ImgEarthNormals" );

struct hello_world_app_o {
	le::Window   window;
	le::Renderer renderer;
	uint64_t     frame_counter = 0;

	LeCameraController cameraController;
	LeCamera           camera;
	LeMesh             sphereMesh;

	le_texture_handle texEarthAlbedo;
	le_texture_handle texEarthNight;
	le_texture_handle texEarthClouds;
	le_texture_handle texEarthNormals;

	LeResourceManager resource_manager;

	WorldGeometry worldGeometry;
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

	le::Window::Settings settings;
	settings
	    .setWidth( 1920 / 2 )
	    .setHeight( 1080 / 2 )
	    .setTitle( "Island // Hello world" );

	// create a new window
	app->window.setup( settings );

	auto rendererInfo = le::RendererInfoBuilder()
	                        .addSwapchain()
	                        .setFormatHint( le::Format::eB8G8R8A8Unorm )
	                        .setWidthHint( 1920 )
	                        .setHeightHint( 1080 )
	                        .setImagecountHint( 3 )
	                        .asWindowSwapchain()
	                        .setWindow( app->window )
	                        .setPresentmode( le::Presentmode::eFifo )
	                        .end()
	                        .end()
	                        .build();

	app->renderer.setup( rendererInfo );

	// -- Declare graphics pipeline state objects

	// Set up the camera
	reset_camera( app );

	{
		// Generate geometry for earth sphere
		LeMeshGenerator::generateSphere( app->sphereMesh, 6360, 120, 120 ); // earth radius given in km.

		size_t vertexCount;
		size_t indexCount;
		app->sphereMesh.getData( vertexCount, indexCount ); // only fetch counts so we can calculate memory requirements for vertex buffer, index buffer

		app->worldGeometry.vertexDataByteCount = vertexCount * sizeof( float ) * ( 3 + 3 + 2 + 3 );
		app->worldGeometry.vertexCount         = vertexCount;
		app->worldGeometry.indexCount          = indexCount;
		app->worldGeometry.index_buffer_info   = le::BufferInfoBuilder()
		                                           .addUsageFlags( { LE_BUFFER_USAGE_INDEX_BUFFER_BIT } )
		                                           .setSize( uint32_t( indexCount * sizeof( uint16_t ) ) )
		                                           .build();
		app->worldGeometry.vertex_buffer_info = le::BufferInfoBuilder()
		                                            .addUsageFlags( { LE_BUFFER_USAGE_VERTEX_BUFFER_BIT } )
		                                            .setSize( uint32_t( app->worldGeometry.vertexDataByteCount ) )
		                                            .build();
	}

	// load pixels for earth albedo

	const char* image_paths[] = {
	    "./local_resources/images/world_winter.jpg",
	    "./local_resources/images/earth_city_lights_8192_rs.png",
	    "./local_resources/images/storm_clouds_8k.jpg",
	    "./local_resources/images/earthNormalMap_8k-sobel.tga",
	};

	app->resource_manager.add_item( imgEarthAlbedo, le::ImageInfoBuilder().setMipLevels( 10 ).build(), image_paths + 0 );
	app->resource_manager.add_item( imgEarthNight, le::ImageInfoBuilder().setMipLevels( 10 ).setFormat( le::Format::eR8Unorm ).build(), image_paths + 1 );
	app->resource_manager.add_item( imgEarthClouds, le::ImageInfoBuilder().setMipLevels( 10 ).build(), image_paths + 2 );
	app->resource_manager.add_item( imgEarthNormals, le::ImageInfoBuilder().setMipLevels( 10 ).setFormat( le::Format::eR16G16B16A16Unorm ).build(), image_paths + 3 );

	// initialise texture handles
	app->texEarthAlbedo  = le::Renderer::produceTextureHandle( "texEarthAlbedo" );
	app->texEarthNight   = le::Renderer::produceTextureHandle( "texEarthNight" );
	app->texEarthClouds  = le::Renderer::produceTextureHandle( "texEarthClouds" );
	app->texEarthNormals = le::Renderer::produceTextureHandle( "texEarthNormals" );

	// initialise app timer
	app->timeStamp = std::chrono::high_resolution_clock::now();

	return app;
}

// ----------------------------------------------------------------------

static void reset_camera( hello_world_app_o* self ) {
	le::Extent2D swapchainExtent{};
	self->renderer.getSwapchainExtent( &swapchainExtent.width, &swapchainExtent.height );
	self->camera.setViewport( { 0, 0, float( swapchainExtent.width ), float( swapchainExtent.height ), 0.f, 1.f } );
	self->camera.setClipDistances( 100.f, 150000.f );
	self->camera.setFovRadians( glm::radians( 25.f ) ); // glm::radians converts degrees to radians

	// glm::mat4 camMatrix = glm::lookAt( glm::vec3{30000, -10000, 20000}, glm::vec3{0}, glm::vec3{0, 1, 0} );
	glm::mat4 camMatrix = glm::mat4{ { 0.585995, 0.191119, 0.787454, -0.000000 }, { -0.049265, 0.978394, -0.200800, 0.000000 }, { -0.808816, 0.078874, 0.582749, -0.000000 }, { 3039.844482, 3673.605225, -15533.671875, 1.000000 } };
	// glm::mat4 camMatrix = glm::mat4{{-0.254149, 0.880418, 0.400359, -0.000000}, {0.633864, 0.464280, -0.618607, 0.000000}, {-0.730506, 0.096555, -0.676056, 0.000000}, {-792.769653, 1875.776367, -15593.370117, 1.000000}};
	self->camera.setViewMatrixGlm( camMatrix );
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

	glm::mat4 viewMatrix             = self->camera.getViewMatrixGlm();
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

static bool pass_resource_setup( le_renderpass_o* pRp, void* user_data ) {
	auto rp  = le::RenderPass{ pRp };
	auto app = static_cast<hello_world_app_o*>( user_data );

	rp
	    .useBufferResource( app->worldGeometry.vertex_buffer_handle, { LE_BUFFER_USAGE_TRANSFER_DST_BIT } )
	    .useBufferResource( app->worldGeometry.index_buffer_handle, { LE_BUFFER_USAGE_TRANSFER_DST_BIT } ) //
	    ;

	return !app->worldGeometry.wasLoaded;
}

// ----------------------------------------------------------------------

static void pass_resource_exec( le_command_buffer_encoder_o* encoder_, void* user_data ) {
	auto        app = static_cast<hello_world_app_o*>( user_data );
	le::Encoder encoder{ encoder_ };

	if ( false == app->worldGeometry.wasLoaded ) {

		// fetch sphere geometry
		auto& geom = app->worldGeometry;

		uint16_t const* sphereIndices{};
		float const*    sphereVertices{};
		float const*    sphereNormals{};
		float const*    sphereUvs{};
		size_t          numVertices{};
		size_t          numIndices{};
		float const*    sphereTangents{};
		app->sphereMesh.getData( numVertices, numIndices, &sphereVertices, &sphereNormals, &sphereUvs, nullptr, &sphereIndices );
		size_t numTangents;
		app->sphereMesh.getTangents( numTangents, &sphereTangents );
		uint32_t offset = 0;

		// upload vertex positions
		geom.buffer_offsets[ 0 ] = 0;
		encoder.writeToBuffer( geom.vertex_buffer_handle, offset, sphereVertices, numVertices * sizeof( float ) * 3 );
		offset += numVertices * sizeof( float ) * 3;

		// upload vertex normals
		geom.buffer_offsets[ 1 ] = offset;
		encoder.writeToBuffer( geom.vertex_buffer_handle, offset, sphereNormals, numVertices * sizeof( float ) * 3 );
		offset += numVertices * sizeof( float ) * 3;

		// upload vertex uvs
		geom.buffer_offsets[ 2 ] = offset;
		encoder.writeToBuffer( geom.vertex_buffer_handle, offset, sphereUvs, numVertices * sizeof( float ) * 2 );
		offset += numVertices * sizeof( float ) * 2;

		// upload vertex tangents
		geom.buffer_offsets[ 3 ] = offset;
		encoder.writeToBuffer( geom.vertex_buffer_handle, offset, sphereTangents, numTangents * sizeof( float ) * 3 );
		offset += numVertices * sizeof( float ) * 3;

		// upload indices
		encoder.writeToBuffer( geom.index_buffer_handle, 0, sphereIndices, numIndices * sizeof( uint16_t ) );

		geom.wasLoaded = true;
	}
}

// ----------------------------------------------------------------------

static bool pass_main_setup( le_renderpass_o* pRp, void* user_data ) {
	auto rp  = le::RenderPass{ pRp };
	auto app = static_cast<hello_world_app_o*>( user_data );

	auto texInfoAlbedo =
	    le::ImageSamplerInfoBuilder()
	        .withImageViewInfo()
	        .setImage( imgEarthAlbedo )
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
	        .setImage( imgEarthNight )
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
	        .setImage( imgEarthClouds )
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
	        .setImage( imgEarthNormals )
	        .end()
	        .withSamplerInfo()
	        .setAddressModeU( le::SamplerAddressMode::eRepeat )
	        .setAddressModeV( le::SamplerAddressMode::eClampToEdge )
	        .setMaxLod( 10.f )
	        .end()
	        .build();

	static le_img_resource_handle LE_SWAPCHAIN_IMAGE_HANDLE = app->renderer.getSwapchainResource();

	rp
	    .addColorAttachment( LE_SWAPCHAIN_IMAGE_HANDLE, le::ImageAttachmentInfoBuilder().setLoadOp( le::AttachmentLoadOp::eClear ).build() ) // color attachment
	    .addDepthStencilAttachment( LE_IMG_RESOURCE( "DEPTH_BUFFER" ) )
	    .sampleTexture( app->texEarthAlbedo, texInfoAlbedo )
	    .sampleTexture( app->texEarthNight, texInfoNight )
	    .sampleTexture( app->texEarthNormals, texInfoNormals )
	    .sampleTexture( app->texEarthClouds, texInfoClouds )
	    .useBufferResource( app->worldGeometry.vertex_buffer_handle, { LE_BUFFER_USAGE_VERTEX_BUFFER_BIT } )
	    .useBufferResource( app->worldGeometry.index_buffer_handle, { LE_BUFFER_USAGE_INDEX_BUFFER_BIT } );

	return true;
}

// ----------------------------------------------------------------------

static void pass_main_exec( le_command_buffer_encoder_o* encoder_, void* user_data ) {
	auto        app = static_cast<hello_world_app_o*>( user_data );
	le::Encoder encoder{ encoder_ };

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

		earthParams.model       = glm::mat4( 1.f );                                                                                    // identity matrix
		earthParams.model       = glm::rotate( earthParams.model, glm::radians( -13.4f ), glm::vec3{ 0, 0, 1 } );                      // apply ecliptic
		earthParams.model       = glm::rotate( earthParams.model, glm::radians( float( app->earthRotation ) ), glm::vec3{ 0, 1, 0 } ); // apply day/night rotation
		cameraParams.view       = app->camera.getViewMatrixGlm();
		cameraParams.projection = app->camera.getProjectionMatrixGlm();

		glm::vec4 sourceInCameraSpace     = cameraParams.view * sunInWorldSpace;
		glm::vec4 worldCentreInWorldSpace = glm::vec4{ 0, 0, 0, 1 };
		glm::vec4 worldCentreInEyeSpace   = cameraParams.view * earthParams.model * worldCentreInWorldSpace;
		glm::vec4 sourceInClipSpace       = cameraParams.projection * sourceInCameraSpace;
		sourceInClipSpace                 = sourceInClipSpace / sourceInClipSpace.w; // Normalise

		earthParams.sunInEyeSpace         = sourceInCameraSpace;
		earthParams.worldCentreInEyeSpace = worldCentreInEyeSpace;

		// draw mesh

		static auto pipelineEarthAlbedo =
		    LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
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
		        .end()
		        .build();

		// We use the same buffer for the whole mesh, but at different offsets.
		// offsets are held by app->worldGeometry.buffer_offsets
		le_buf_resource_handle buffers[ 4 ] = {
		    app->worldGeometry.vertex_buffer_handle, // position
		    app->worldGeometry.vertex_buffer_handle, // normal
		    app->worldGeometry.vertex_buffer_handle, // uv
		    app->worldGeometry.vertex_buffer_handle, // tangents
		};

		encoder
		    .setScissors( 0, 1, scissors )
		    .setViewports( 0, 1, viewports )
		    .bindGraphicsPipeline( pipelineEarthAlbedo )
		    .bindVertexBuffers( 0, 4, buffers, app->worldGeometry.buffer_offsets.data() )
		    .bindIndexBuffer( app->worldGeometry.index_buffer_handle, 0 );

		encoder
		    .setArgumentData( LE_ARGUMENT_NAME( "CameraParams" ), &cameraParams, sizeof( CameraParams ) )
		    .setArgumentData( LE_ARGUMENT_NAME( "ModelParams" ), &earthParams, sizeof( ModelParams ) )
		    .setArgumentTexture( LE_ARGUMENT_NAME( "tex_unit_0" ), app->texEarthAlbedo )
		    .setArgumentTexture( LE_ARGUMENT_NAME( "tex_unit_1" ), app->texEarthNormals )
		    .setArgumentTexture( LE_ARGUMENT_NAME( "tex_unit_2" ), app->texEarthNight )
		    .setArgumentTexture( LE_ARGUMENT_NAME( "tex_clouds" ), app->texEarthClouds )
		    .drawIndexed( uint32_t( app->worldGeometry.indexCount ) ) //
		    ;

		// draw atmosphere

		static auto pipelineEarthAtmosphere =
		    LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
		        .addShaderStage(
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
		        .end()
		        .build();

		earthParams.model = glm::scale( earthParams.model, glm::vec3{ 1.025f } );

		encoder
		    .bindGraphicsPipeline( pipelineEarthAtmosphere )
		    .setArgumentData( LE_ARGUMENT_NAME( "ModelParams" ), &earthParams, sizeof( ModelParams ) )
		    .setArgumentData( LE_ARGUMENT_NAME( "CameraParams" ), &cameraParams, sizeof( CameraParams ) )
		    .bindVertexBuffers( 0, 3, buffers, app->worldGeometry.buffer_offsets.data() )
		    .drawIndexed( uint32_t( app->worldGeometry.indexCount ) ) // index buffers should still be bound.
		    ;

		// let's check if sun is in clip space

		float howClose;

		bool hit = hello_world_app_ray_cam_to_sun_hits_earth( app, howClose );

		//		std::cout << "Hit? " << ( hit ? "true  " : " false " ) << ", distance: " << howClose << std::endl
		//		          << std::flush;

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

	le::Extent2D swapchainExtent{};
	self->renderer.getSwapchainExtent( &swapchainExtent.width, &swapchainExtent.height );

	self->cameraController.setControlRect( 0, 0, float( swapchainExtent.width ), float( swapchainExtent.height ) );

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

	le::RenderModule mainModule{};
	{

		self->resource_manager.update( mainModule );

		le::RenderPass resourcePass( "resources", LE_RENDER_PASS_TYPE_TRANSFER );
		resourcePass
		    .setSetupCallback( self, pass_resource_setup )
		    .setExecuteCallback( self, pass_resource_exec ) //
		    ;

		le::RenderPass renderPassFinal( "mainPass", LE_RENDER_PASS_TYPE_DRAW );
		renderPassFinal
		    .setSetupCallback( self, pass_main_setup )
		    .setSampleCount( le::SampleCountFlagBits::e8 )
		    .setExecuteCallback( self, pass_main_exec ) //
		    ;

		mainModule
		    .addRenderPass( resourcePass )
		    .addRenderPass( renderPassFinal );
		mainModule
		    .declareResource( self->worldGeometry.index_buffer_handle, self->worldGeometry.index_buffer_info )
		    .declareResource( self->worldGeometry.vertex_buffer_handle, self->worldGeometry.vertex_buffer_info );
	}

	// Update will call all rendercallbacks in this module.
	// the RECORD phase is guaranteed to execute - all rendercallbacks will get called.
	self->renderer.update( mainModule );

	self->frame_counter++;

	return true; // keep app alive
}

// ----------------------------------------------------------------------
static void hello_world_app_process_ui_events( hello_world_app_o* self ) {
	using namespace le_window;
	uint32_t         numEvents;
	LeUiEvent const* pEvents;
	window_i.get_ui_event_queue( self->window, &pEvents, numEvents );

	std::vector<LeUiEvent> events{ pEvents, pEvents + numEvents };

	bool wantsToggle = false;

	for ( auto& event : events ) {
		switch ( event.event ) {
		case ( LeUiEvent::Type::eKey ): {
			auto& e = event.key;
			if ( e.action == LeUiEvent::ButtonAction::eRelease ) {
				if ( e.key == LeUiEvent::NamedKey::eF11 ) {
					wantsToggle ^= true;
				} else if ( e.key == LeUiEvent::NamedKey::eZ ) {
					reset_camera( self );
					float distance_to_origin = glm::distance( glm::vec4{ 0, 0, 0, 1 }, glm::inverse( self->camera.getViewMatrixGlm() ) * glm::vec4( 0, 0, 0, 1 ) );
					self->cameraController.setPivotDistance( distance_to_origin );
				} else if ( e.key == LeUiEvent::NamedKey::eX ) {
					self->cameraController.setPivotDistance( 0 );
				} else if ( e.key == LeUiEvent::NamedKey::eC ) {
					float distance_to_origin = glm::distance( glm::vec4{ 0, 0, 0, 1 }, glm::inverse( self->camera.getViewMatrixGlm() ) * glm::vec4( 0, 0, 0, 1 ) );
					self->cameraController.setPivotDistance( distance_to_origin );
				} else if ( e.key == LeUiEvent::NamedKey::eA ) {
					self->animate ^= true;
				} else if ( e.key == LeUiEvent::NamedKey::eP ) {
					// print out current camera view matrix
					std::cout << "View matrix:" << glm::to_string( self->camera.getViewMatrixGlm() ) << std::endl
					          << std::flush;
					std::cout << "camera node matrix:" << glm::to_string( glm::inverse( self->camera.getViewMatrixGlm() ) ) << std::endl
					          << std::flush;
				}
			} // if ButtonAction == eRelease

		} break;
		default:
			// do nothing
			break;
		}
	}

	self->cameraController.processEvents( self->camera, events.data(), events.size() );

	if ( wantsToggle ) {
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

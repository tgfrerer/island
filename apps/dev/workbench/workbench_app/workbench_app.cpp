#include "workbench_app.h"

#include "pal_window/pal_window.h"
#include "le_renderer/le_renderer.h"
#include "le_renderer/private/le_renderer_types.h"
#include "le_gltf_loader/le_gltf_loader.h"

#include "le_camera/le_camera.h"
#include "le_pipeline_builder/le_pipeline_builder.h"
#include "le_pixels/le_pixels.h"
#include "le_ui_event/le_ui_event.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // vulkan clip space is from 0 to 1
#define GLM_FORCE_RIGHT_HANDED      // glTF uses right handed coordinate system, and we're following its lead.
#include "glm.hpp"
#include "gtc/matrix_transform.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include "gtx/easing.hpp"

#include <iostream>
#include <memory>

#include "imgui/imgui.h"

#include <sstream>

#include <chrono> // for nanotime
using NanoTime = std::chrono::time_point<std::chrono::high_resolution_clock>;

struct GltfUboMvp {
	glm::mat4 projection;
	glm::mat4 model;
	glm::mat4 view;
};

struct FontTextureInfo {
	uint8_t *                  pixels            = nullptr;
	int32_t                    width             = 0;
	int32_t                    height            = 0;
	const le_resource_handle_t le_texture_handle = LE_TEX_RESOURCE( "ImguiDefaultFontTexture" );
	const le_resource_handle_t le_image_handle   = LE_IMG_RESOURCE( "ImguiDefaultFontImage" );
	bool                       wasUploaded       = false;
};

struct le_mouse_event_data_o {
	uint32_t  buttonState{};
	glm::vec2 cursor_pos;
};

static constexpr le_resource_handle_t resImgDepth       = LE_IMG_RESOURCE( "ImgDepth" );
static constexpr le_resource_handle_t resImgPrepass     = LE_IMG_RESOURCE( "ImgPrepass" );
static constexpr le_resource_handle_t resTexPrepass     = LE_TEX_RESOURCE( "TexPrepass" );
static constexpr le_resource_handle_t resImgHorse       = LE_IMG_RESOURCE( "ImgHorse" );
static constexpr le_resource_handle_t resTexHorse       = LE_TEX_RESOURCE( "TexHorse" );
static constexpr le_resource_handle_t resBufTrianglePos = LE_BUF_RESOURCE( "BufTrianglePos" );

struct workbench_app_o {
	pal::Window                 window;
	le::Renderer                renderer;
	le_gpso_handle psoMain;           // weak ref, owned by renderer
	le_gpso_handle psoFullScreenQuad; // weak ref, owned by renderer
	le_gpso_handle psoImgui;          // weak ref, owned by renderer
	ImGuiContext *              imguiContext  = nullptr;
	uint64_t                    frame_counter = 0;
	float                       deltaTimeSec  = 0;
	float                       animT         = 0;

	FontTextureInfo imguiTexture = {};

	std::array<bool, 5> mouseButtonStatus{}; // status for each mouse button
	glm::vec2           mousePos{};          // current mouse position

	NanoTime update_start_time;

	// Note we use the c++ facade for resource handles as this guarantees that resource
	// handles are initialised to nullptr, otherwise this is too easy to forget...

	le_shader_module_o *shaderTriangle[ 2 ]{};
	le_shader_module_o *shaderPrepass[ 2 ]{};

	bool                imgHorseWasUploaded = false;
	le_gltf_document_o *gltfDoc             = nullptr;

	// NOTE: RUNTIME-COMPILE : If you add any new things during run-time, make sure to only add at the end of the object,
	// otherwise all pointers above will be invalidated. this might also overwrite memory which
	// is stored after this object, which is very subtle in introducing errors. We need to think about a way of serializing
	// and de-serializing objects which are allocated on the heap. we don't have to worry about objects which are allocated
	// on the stack, as the stack acts like a pool allocator, and they are only alife while control visits the code section
	// in question.

	le_resource_info_t resInfoHorse; // resource info for horse image
	le_resource_info_t resInfoFont;  // resource info for font image

	LeCamera           camera;
	LeCameraController cameraController;
};

static void workbench_app_process_ui_events( workbench_app_o *self ); // ffdecl

// ----------------------------------------------------------------------

static void initialize() {
	pal::Window::init();
};

// ----------------------------------------------------------------------

static void terminate() {
	pal::Window::terminate();
};

static void reset_camera( workbench_app_o *self ); // ffdecl.

// ----------------------------------------------------------------------

static workbench_app_o *workbench_app_create() {
	auto app = new ( workbench_app_o );

	pal::Window::Settings settings;
	settings
	    .setWidth( 1024 )
	    .setHeight( 768 )
	    .setTitle( "Hello world" );

	// create a new window
	app->window.setup( settings );

	app->renderer.setup( le::RendererInfoBuilder( app->window ).build() );

	le_pipeline_manager_o *pipelineCache = nullptr;
	{
		using namespace le_renderer;
		pipelineCache = renderer_i.get_pipeline_manager( app->renderer );
	}

	{
		// -- Declare graphics pipeline state objects

		{
			// create default pipeline

			auto defaultVertShader = app->renderer.createShaderModule( "./resources/shaders/default.vert", le::ShaderStage::eVertex );
			auto defaultFragShader = app->renderer.createShaderModule( "./resources/shaders/default.frag", le::ShaderStage::eFragment );

			app->shaderTriangle[ 0 ] = defaultVertShader;
			app->shaderTriangle[ 1 ] = defaultFragShader;

			// The pipeline state object holds all state for the pipeline,
			// that's links to shader modules, blend states, input assembly, etc...
			// Everything, in short, but the renderpass, and subpass (which are added at the last minute)
			//
			// The backend pipeline object is compiled on-demand, when it is first used with a renderpass, and henceforth cached.

			auto pso = LeGraphicsPipelineBuilder( pipelineCache )
			               .addShaderStage( defaultFragShader )
			               .addShaderStage( defaultVertShader )
			               .build();

			if ( pso ) {
				app->psoMain = pso;
			} else {
				std::cerr << "declaring main pipeline failed miserably.";
			}
		}

		{
			// Create pso for imgui rendering
			auto imguiVertShader = app->renderer.createShaderModule( "./resources/shaders/imgui.vert", le::ShaderStage::eVertex );
			auto imguiFragShader = app->renderer.createShaderModule( "./resources/shaders/imgui.frag", le::ShaderStage::eFragment );

			std::array<le_vertex_input_attribute_description, 3> attrs    = {};
			std::array<le_vertex_input_binding_description, 1>   bindings = {};
			{
				// location 0, binding 0
				attrs[ 0 ].location       = 0;                           // refers to shader parameter location
				attrs[ 0 ].binding        = 0;                           // refers to bound buffer index
				attrs[ 0 ].binding_offset = offsetof( ImDrawVert, pos ); // offset into bound buffer
				attrs[ 0 ].type           = le_vertex_input_attribute_description::eFloat;
				attrs[ 0 ].vecsize        = 2;

				// location 1, binding 0
				attrs[ 1 ].location       = 1;
				attrs[ 1 ].binding        = 0;
				attrs[ 1 ].binding_offset = offsetof( ImDrawVert, uv );
				attrs[ 1 ].type           = le_vertex_input_attribute_description::eFloat;
				attrs[ 1 ].vecsize        = 2;

				// location 2, binding 0
				attrs[ 2 ].location       = 2;
				attrs[ 2 ].binding        = 0;
				attrs[ 2 ].binding_offset = offsetof( ImDrawVert, col );
				attrs[ 2 ].type           = le_vertex_input_attribute_description::eChar;
				attrs[ 2 ].vecsize        = 4;
				attrs[ 2 ].isNormalised   = true;
			}
			{
				// binding 0
				bindings[ 0 ].binding    = 0;
				bindings[ 0 ].input_rate = le_vertex_input_binding_description::INPUT_RATE::ePerVertex;
				bindings[ 0 ].stride     = sizeof( ImDrawVert );
			}

			// NICE: Setting this static means that the builder only runs for the very first time.
			//
			// Which makes sense since every other time it will return the same hash value for
			// given data.
			// and all calculations will be in vain, and write access to the cache is expensive.
			static le_gpso_handle psoHandle = LeGraphicsPipelineBuilder( pipelineCache )
			                                                   .addShaderStage( imguiFragShader )
			                                                   .addShaderStage( imguiVertShader )
			                                                   .setVertexInputAttributeDescriptions( attrs.data(), attrs.size() )
			                                                   .setVertexInputBindingDescriptions( bindings.data(), bindings.size() )
			                                                   .build();

			if ( psoHandle ) {
				app->psoImgui = psoHandle;
			} else {
				std::cerr << "declaring pso for imgui failed miserably.";
			}
		}

		// load shaders for prepass

		app->shaderPrepass[ 0 ] = app->renderer.createShaderModule( "./resources/shaders/prepass.vert", {le::ShaderStage::eVertex} );
		app->shaderPrepass[ 1 ] = app->renderer.createShaderModule( "./resources/shaders/prepass.frag", le::ShaderStage::eFragment );

		{
			// create full screen quad pipeline

			auto fullScreenQuadVertShader = app->renderer.createShaderModule( "./resources/shaders/fullscreenQuad.vert", le::ShaderStage::eVertex );
			auto fullScreenQuadFragShader = app->renderer.createShaderModule( "./resources/shaders/fullscreenQuad.frag", le::ShaderStage::eFragment );

			auto psoHandle = LeGraphicsPipelineBuilder( pipelineCache )
			                     .addShaderStage( fullScreenQuadFragShader )
			                     .addShaderStage( fullScreenQuadVertShader )
			                     .build();

			if ( psoHandle ) {
				app->psoFullScreenQuad = psoHandle;
			} else {
				std::cerr << "declaring workbench pipeline failed miserably.";
			}
		}
	}

	app->imguiContext = ImGui::CreateContext();

	// get imgui font texture handle
	{
		ImGuiIO &io = ImGui::GetIO();
		io.Fonts->AddFontFromFileTTF( "./resources/fonts/IBMPlexSans-Regular.otf", 20.0f, nullptr, io.Fonts->GetGlyphRangesDefault() );
		io.Fonts->GetTexDataAsRGBA32( &app->imguiTexture.pixels, &app->imguiTexture.width, &app->imguiTexture.height );

		auto extent = app->renderer.getSwapchainExtent();

		io.DisplaySize = {float( extent.width ),
		                  float( extent.height )};

		// we want to save the raw value in the pointer, because if we passed in a
		// pointer to the name of the texture, the texture may have changed.
		// for this to work, we first cast to uint64_t, then cast to void*
		io.Fonts->TexID = ( void * )( uint64_t const & )app->imguiTexture.le_texture_handle;

		// Keyboard mapping. ImGui will use those indices to peek into the io.KeysDown[] array.
		io.KeyMap[ ImGuiKey_Tab ]        = uint32_t( LeUiEvent::NamedKey::eTab );
		io.KeyMap[ ImGuiKey_LeftArrow ]  = uint32_t( LeUiEvent::NamedKey::eLeft );
		io.KeyMap[ ImGuiKey_RightArrow ] = uint32_t( LeUiEvent::NamedKey::eRight );
		io.KeyMap[ ImGuiKey_UpArrow ]    = uint32_t( LeUiEvent::NamedKey::eUp );
		io.KeyMap[ ImGuiKey_DownArrow ]  = uint32_t( LeUiEvent::NamedKey::eDown );
		io.KeyMap[ ImGuiKey_PageUp ]     = uint32_t( LeUiEvent::NamedKey::ePageUp );
		io.KeyMap[ ImGuiKey_PageDown ]   = uint32_t( LeUiEvent::NamedKey::ePageDown );
		io.KeyMap[ ImGuiKey_Home ]       = uint32_t( LeUiEvent::NamedKey::eHome );
		io.KeyMap[ ImGuiKey_End ]        = uint32_t( LeUiEvent::NamedKey::eEnd );
		io.KeyMap[ ImGuiKey_Insert ]     = uint32_t( LeUiEvent::NamedKey::eInsert );
		io.KeyMap[ ImGuiKey_Delete ]     = uint32_t( LeUiEvent::NamedKey::eDelete );
		io.KeyMap[ ImGuiKey_Backspace ]  = uint32_t( LeUiEvent::NamedKey::eBackspace );
		io.KeyMap[ ImGuiKey_Space ]      = uint32_t( LeUiEvent::NamedKey::eSpace );
		io.KeyMap[ ImGuiKey_Enter ]      = uint32_t( LeUiEvent::NamedKey::eEnter );
		io.KeyMap[ ImGuiKey_Escape ]     = uint32_t( LeUiEvent::NamedKey::eEscape );
		io.KeyMap[ ImGuiKey_A ]          = uint32_t( LeUiEvent::NamedKey::eA );
		io.KeyMap[ ImGuiKey_C ]          = uint32_t( LeUiEvent::NamedKey::eC );
		io.KeyMap[ ImGuiKey_V ]          = uint32_t( LeUiEvent::NamedKey::eV );
		io.KeyMap[ ImGuiKey_X ]          = uint32_t( LeUiEvent::NamedKey::eX );
		io.KeyMap[ ImGuiKey_Y ]          = uint32_t( LeUiEvent::NamedKey::eY );
		io.KeyMap[ ImGuiKey_Z ]          = uint32_t( LeUiEvent::NamedKey::eZ );
	}

	app->update_start_time = std::chrono::high_resolution_clock::now();

	{
		using le_gltf_loader::gltf_document_i;

		app->gltfDoc = gltf_document_i.create();
		// gltf_document_i.load_from_text( app->gltfDoc, "resources/gltf/BoomBoxWithAxes.gltf" );
		gltf_document_i.load_from_text( app->gltfDoc, "resources/gltf/FlightHelmet.gltf" );
		//gltf_document_i.load_from_text( app->gltfDoc, "resources/gltf/Box.gltf" );
		//gltf_document_i.load_from_text( app->gltfDoc, "resources/gltf/exportFile.gltf" );
		gltf_document_i.setup_resources( app->gltfDoc, app->renderer, pipelineCache );
	}

	{
		reset_camera( app );
	}

	{
		app->resInfoHorse = le::ImageInfoBuilder()
		                        .setExtent( 640, 425 )
		                        .addUsageFlags( LE_IMAGE_USAGE_TRANSFER_DST_BIT )
		                        .setFormat( le::Format::eR8G8B8A8Unorm )
		                        .build() // create resource for horse image
		    ;

		app->resInfoFont = le::ImageInfoBuilder()
		                       .setExtent( uint32_t( app->imguiTexture.width ), uint32_t( app->imguiTexture.height ) )
		                       .setUsageFlags( LE_IMAGE_USAGE_TRANSFER_DST_BIT )
		                       .setFormat( le::Format::eR8G8B8A8Unorm )
		                       .build() // create resource for imgui font texture if it does not yet exist.
		    ;
	}

	return app;
}

// ----------------------------------------------------------------------

static void reset_camera( workbench_app_o *self ) {
	auto extent = self->renderer.getSwapchainExtent();
	self->camera.setViewport( {0, 0, float( extent.width ), float( extent.height ), 0.f, 1.f} );
	self->camera.setFovRadians( glm::radians( 60.f ) ); // glm::radians converts degrees to radians
	glm::mat4 camMatrix = glm::lookAt( glm::vec3{0, 0, self->camera.getUnitDistance()}, glm::vec3{0}, glm::vec3{0, 1, 0} );
	self->camera.setViewMatrix( reinterpret_cast<float const *>( &camMatrix ) );
}

// ----------------------------------------------------------------------

static bool pass_resource_setup( le_renderpass_o *pRp, void *user_data_ ) {
	auto app = static_cast<workbench_app_o *>( user_data_ );
	auto rp  = le::RenderPass{pRp};

	rp.useResource( resImgHorse, app->resInfoHorse );

	rp.useResource( app->imguiTexture.le_image_handle, app->resInfoFont );

	rp.useResource( resBufTrianglePos,
	                le::BufferInfoBuilder()
	                    .setSize( sizeof( glm::vec3 ) * 3 )
	                    .addUsageFlags( LE_BUFFER_USAGE_VERTEX_BUFFER_BIT )
	                    .build() // create resource for triangle vertex buffer
	);

	{
		using namespace le_gltf_loader;
		// create resources for gltf document
		le_resource_info_t *        resourceInfo;
		le_resource_handle_t const *resourceHandles;
		size_t                      numResourceInfos;
		gltf_document_i.get_resource_infos( app->gltfDoc, &resourceInfo, &resourceHandles, &numResourceInfos );

		for ( size_t i = 0; i != numResourceInfos; i++ ) {
			rp.useResource( resourceHandles[ i ], resourceInfo[ i ] );
		}
	}

	return true;
}

// ----------------------------------------------------------------------

static void pass_resource_exec( le_command_buffer_encoder_o *encoder, void *user_data_ ) {

	using namespace le_renderer;

	auto app = static_cast<workbench_app_o *>( user_data_ );

	// Writing is always to encoder scratch buffer memory because that's the only memory that
	// is HOST visible.
	//
	// Type of resource ownership decides whether
	// a copy is added to the queue that transfers from scratch memory
	// to GPU local memory.

	if ( false == app->imgHorseWasUploaded ) {
		auto pix      = LePixels( "./resources/images/horse-1330690_640.jpg", 4 );
		auto pix_info = pix.getInfo();
		auto pix_data = pix.getData();
		encoder_i.write_to_image( encoder, resImgHorse, app->resInfoHorse, pix_data, pix_info.byte_count );
		app->imgHorseWasUploaded = true;
	}

	if ( false == app->imguiTexture.wasUploaded ) {
		// tell encoder to upload imgui image - but only once
		// note that we use the le_image_handle field to signal that the image has been uploaded.
		size_t numBytes = size_t( app->imguiTexture.width ) * size_t( app->imguiTexture.height ) * 32;

		encoder_i.write_to_image( encoder, app->imguiTexture.le_image_handle, app->resInfoFont, app->imguiTexture.pixels, numBytes );
		app->imguiTexture.wasUploaded = true;
	}

	{
		// upload triangle data
		glm::vec3 trianglePositions[ 3 ] = {
		    {-50, -50, 0},
		    {50, -50, 0},
		    {0, 50, 0},
		};

		encoder_i.write_to_buffer( encoder, resBufTrianglePos, 0, trianglePositions, sizeof( trianglePositions ) );
	}

	using namespace le_gltf_loader;
	gltf_document_i.upload_resource_data( app->gltfDoc, encoder );
}

// ----------------------------------------------------------------------

static bool pass_pre_setup( le_renderpass_o *pRp, void *user_data_ ) {
	auto rp  = le::RenderPass{pRp};
	auto app = static_cast<workbench_app_o *>( user_data_ );

	rp.addColorAttachment( resImgPrepass );

	LeTextureInfo textureInfo{};
	textureInfo.imageView.imageId = resImgHorse;
	textureInfo.sampler.magFilter = le::Filter::eLinear;
	textureInfo.sampler.minFilter = le::Filter::eLinear;

	rp.sampleTexture( resTexHorse, textureInfo );

	rp.setWidth( 640 );
	rp.setHeight( 425 );

	return true;
}

// ----------------------------------------------------------------------

static void pass_pre_exec( le_command_buffer_encoder_o *encoder_, void *user_data ) {

	auto encoder = le::Encoder( encoder_ ); // use c++ facade for less typing ;)
	auto app     = static_cast<workbench_app_o *>( user_data );

	static float t_start = 0;
	float        info    = fmodf( t_start + app->deltaTimeSec, 3.f );
	info /= 3.f;
	info = fabs( ( glm::sineEaseInOut( info ) - 0.5f ) * 2.f );
	t_start += app->deltaTimeSec;

	static auto psoPrepass = LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
	                             .addShaderStage( app->shaderPrepass[ 0 ] )
	                             .addShaderStage( app->shaderPrepass[ 1 ] )
	                             .build();

	encoder
	    .bindGraphicsPipeline( psoPrepass ) // Bind full screen quad pipeline
	    .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_0" ), resTexHorse, 0 )
	    .setArgumentData( LE_ARGUMENT_NAME( "TimeInfo" ), &info, sizeof( info ) )
	    .draw( 3 );
}

// ----------------------------------------------------------------------

static bool pass_final_setup( le_renderpass_o *pRp, void *user_data_ ) {
	auto rp  = le::RenderPass{pRp};
	auto app = static_cast<workbench_app_o *>( user_data_ );

	rp
	    .addColorAttachment( app->renderer.getSwapchainResource() ) // color attachment
	    .addDepthStencilAttachment( LE_IMG_RESOURCE( "ImgDepth" ) ) // depth attachment
	    .sampleTexture( resTexPrepass, {{le::Filter::eLinear, le::Filter::eLinear}, {resImgPrepass, {}}} )
	    .sampleTexture( app->imguiTexture.le_texture_handle, {{le::Filter::eLinear, le::Filter::eLinear}, {app->imguiTexture.le_image_handle, {}}} )
	    .setIsRoot( true );

	return true;
}

// ----------------------------------------------------------------------

static void pass_final_exec( le_command_buffer_encoder_o *encoder_, void *user_data ) {

	using namespace le_renderer;
	auto app = static_cast<workbench_app_o *>( user_data );

	auto encoder = le::Encoder( encoder_ );

	auto screenWidth  = encoder.getRenderpassExtent().width;
	auto screenHeight = encoder.getRenderpassExtent().height;

	le::Viewport viewports[ 3 ] = {
	    {0.f, 0.f, float( screenWidth ), float( screenHeight ), 0.f, 1.f},
	    {10.f, 10.f, 160.f * 3.f + 10.f, 106.f * 3.f + 10.f, 0.f, 1.f},
	    {10.f, 10.f, 640 / 5, 425 / 5, 0.f, 1.f},
	};

	app->camera.setViewport( viewports[ 0 ] );

	le::Rect2D scissors[ 3 ] = {
	    {0, 0, screenWidth, screenHeight},
	    {10, 10, 160 * 3 + 10, 106 * 3 + 10},
	    {10, 10, 640 / 5, 425 / 5},
	};

	struct MvpUbo_t {
		glm::mat4 modelMatrix;
		glm::mat4 viewMatrix;
		glm::mat4 projectionMatrix;
	};

	app->animT       = fmodf( app->animT + app->deltaTimeSec, 10.f );
	float r_val      = app->animT / 10.f;
	float r_anim_val = glm::elasticEaseOut( r_val );

	// Draw RGB triangle
	if ( true ) {

		static auto psoTriangle = LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
		                              .addShaderStage( app->shaderTriangle[ 0 ] )
		                              .addShaderStage( app->shaderTriangle[ 1 ] )
		                              .withRasterizationState()
		                              .setPolygonMode( le::PolygonMode::eFill )
		                              .end()
		                              .build();

		MvpUbo_t matrixStack;

		matrixStack.projectionMatrix = *reinterpret_cast<glm::mat4 const *>( app->camera.getProjectionMatrix() );

		matrixStack.modelMatrix = glm::mat4( 1.f ); // identity matrix
		matrixStack.modelMatrix = glm::translate( matrixStack.modelMatrix, glm::vec3( 0, 0, -100 ) );
		matrixStack.modelMatrix = glm::rotate( matrixStack.modelMatrix, glm::radians( r_anim_val * 360 ), glm::vec3( 0, 0, 1 ) );
		matrixStack.modelMatrix = glm::scale( matrixStack.modelMatrix, glm::vec3( 4.5 ) );

		matrixStack.viewMatrix = reinterpret_cast<glm::mat4 const &>( *app->camera.getViewMatrix() );

		le_resource_handle_t buffers[] = {resBufTrianglePos};
		uint64_t             offsets[] = {0};

		glm::vec4 triangleColors[ 3 ] = {
		    {1, 0, 0, 1.f},
		    {0, 1, 0, 1.f},
		    {0, 0, 1, 1.f},
		};

		uint16_t indexData[ 3 ] = {0, 1, 2};

		encoder
		    .bindGraphicsPipeline( psoTriangle )
		    .setScissors( 0, 1, scissors )
		    .setViewports( 0, 1, viewports )
		    .setArgumentData( LE_ARGUMENT_NAME( "MatrixStack" ), &matrixStack, sizeof( MvpUbo_t ) )
		    .bindVertexBuffers( 0, 1, buffers, offsets )
		    .setVertexData( triangleColors, sizeof( glm::vec4 ) * 3, 1 )
		    .setIndexData( indexData, sizeof( indexData ), le::IndexType::eUint16 ) // 0 for indexType means uint16_t
		    .drawIndexed( 3 )                                                       //
		    ;
	}

	// Draw GLTF file
	if ( true ) {

		encoder_i.set_scissor( encoder, 0, 1, scissors );
		encoder_i.set_viewport( encoder, 0, 1, viewports );

		GltfUboMvp ubo;

		ubo.projection = *reinterpret_cast<glm::mat4 const *>( app->camera.getProjectionMatrix() );
		ubo.model      = glm::mat4( 1 );
		ubo.model      = glm::translate( ubo.model, glm::vec3( 0, 0, 0 ) );

		ubo.model = glm::rotate( ubo.model, glm::radians( r_val * 360.f ), glm::vec3( 0, 1, 0 ) );
		ubo.model = glm::scale( ubo.model, glm::vec3( 400.f ) ); // identity matrix

		ubo.view = *reinterpret_cast<glm::mat4 const *>( app->camera.getViewMatrix() );

		// FIXME: we must first set the pipeline, before we can upload any arguments
		using namespace le_gltf_loader;
		gltf_document_i.draw( app->gltfDoc, encoder, &ubo );
	}

	// Draw result of prepass
	if ( true ) {

		encoder
		    .bindGraphicsPipeline( app->psoFullScreenQuad )
		    .setArgumentTexture( LE_ARGUMENT_NAME( "src_tex_unit_0" ), resTexPrepass, 0 )
		    .setScissors( 0, 1, &scissors[ 2 ] )
		    .setViewports( 0, 1, &viewports[ 2 ] )
		    .draw( 3 ) //
		    ;
	}

	ImDrawData *drawData = ImGui::GetDrawData();
	if ( drawData ) {
		// draw imgui

		auto ortho_projection = glm::ortho( 0.f, float( screenWidth ), 0.f, float( screenHeight ) );

		ImVec2 display_pos = drawData->DisplayPos;

		encoder
		    .bindGraphicsPipeline( app->psoImgui )
		    .setViewports( 0, 1, &viewports[ 0 ] )
		    .setArgumentData( LE_ARGUMENT_NAME( "MatrixStack" ), &ortho_projection, sizeof( glm::mat4 ) )
		    .setArgumentTexture( LE_ARGUMENT_NAME( "tex_unit_0" ), app->imguiTexture.le_texture_handle, 0 ) //
		    ;

		le_resource_handle_t currentTexture = app->imguiTexture.le_texture_handle; // we check this for changes so that we don't have to switch state that often.

		ImVec4 currentClipRect{};

		for ( ImDrawList **cmdList = drawData->CmdLists; cmdList != drawData->CmdLists + drawData->CmdListsCount; cmdList++ ) {
			auto &im_cmd_list = *cmdList;

			// upload index data
			encoder_i.set_index_data( encoder, im_cmd_list->IdxBuffer.Data, size_t( im_cmd_list->IdxBuffer.size() * sizeof( ImDrawIdx ) ), le::IndexType::eUint16 );
			// upload vertex data
			encoder_i.set_vertex_data( encoder, im_cmd_list->VtxBuffer.Data, size_t( im_cmd_list->VtxBuffer.size() * sizeof( ImDrawVert ) ), 0 );

			uint32_t index_offset = 0;
			for ( const auto &im_cmd : im_cmd_list->CmdBuffer ) {

				if ( im_cmd.UserCallback ) {
					// call user callback
					continue;
				}
				// -----| invariant: im_cmd was not user callback

				static_assert( sizeof( le::Rect2D ) == sizeof( ImVec4 ), "clip rect size must match for direct assignment" );

				// -- update bound texture, but only if texture different from currently bound texture
				const le_resource_handle_t nextTexture = reinterpret_cast<const le_resource_handle_t &>( im_cmd.TextureId );
				if ( nextTexture != currentTexture ) {
					encoder_i.set_argument_texture( encoder, nextTexture, hash_64_fnv1a_const( "tex_unit_0" ), 0 );
					currentTexture = nextTexture;
				}

				// -- set clip rectangle as scissor
				if ( 0 != memcmp( &im_cmd.ClipRect, &currentClipRect, sizeof( ImVec4 ) ) ) {
					// clip rects are different
					currentClipRect = im_cmd.ClipRect;
					le::Rect2D scissor;
					scissor.x      = ( im_cmd.ClipRect.x - display_pos.x ) > 0 ? uint32_t( im_cmd.ClipRect.x - display_pos.x ) : 0;
					scissor.y      = ( im_cmd.ClipRect.y - display_pos.y ) > 0 ? uint32_t( im_cmd.ClipRect.y - display_pos.y ) : 0;
					scissor.width  = uint32_t( im_cmd.ClipRect.z - im_cmd.ClipRect.x );
					scissor.height = uint32_t( im_cmd.ClipRect.w - im_cmd.ClipRect.y + 1 ); // FIXME: Why +1 here?

					encoder_i.set_scissor( encoder, 0, 1, &scissor );
				}

				// uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);
				encoder_i.draw_indexed( encoder, im_cmd.ElemCount, 1, index_offset, 0, 0 );
				index_offset += im_cmd.ElemCount;
			}

		} // end for ImDrawList
	}     // end if DrawData
}

// ----------------------------------------------------------------------

static bool workbench_app_update( workbench_app_o *self ) {

	static bool resetCameraOnReload = false; // reload meand module reload

	// update frame delta time
	auto   current_time = std::chrono::high_resolution_clock::now();
	double millis       = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>( current_time - self->update_start_time ).count();
	self->deltaTimeSec  = float( millis / 1000.0 );

	ImGui::SetCurrentContext( self->imguiContext ); // NOTICE: that's important for reload.
	{
		auto &io                = ImGui::GetIO();
		io.DeltaTime            = self->deltaTimeSec;
		self->update_start_time = current_time;
	}

	// Polls events for all windows -
	// This means any window may trigger callbacks for any events they have callbacks registered.
	pal::Window::pollEvents();

	if ( self->window.shouldClose() ) {
		return false;
	}

	auto swapchainExtent = self->renderer.getSwapchainExtent();
	self->cameraController.setControlRect( 0, 0, float( swapchainExtent.width ), float( swapchainExtent.height ) );

	// Process pending ui events.
	workbench_app_process_ui_events( self );

	if ( resetCameraOnReload ) {
		// Reset camera
		reset_camera( self );
		resetCameraOnReload = false;
	}

	ImGui::NewFrame();
	{
		ImGuiIO &io = ImGui::GetIO();

		io.DisplaySize = {float( swapchainExtent.width ),
		                  float( swapchainExtent.height )};

		// update mouse pos and buttons
		for ( size_t i = 0; i < self->mouseButtonStatus.size(); i++ ) {
			// If a mouse press event came, always pass it as "mouse held this frame", so we don't miss click-release events that are shorter than 1 frame.
			io.MouseDown[ i ] = self->mouseButtonStatus[ i ];
		}
		io.MousePos = {self->mousePos.x, self->mousePos.y};
	}

	//	ImGui::ShowDemoWindow();
	ImGui::ShowMetricsWindow();
	ImGui::Render();

	// Grab interface for encoder so that it can be used in callbacks -
	// making it static allows it to be visible inside the callback context,
	// and it also ensures that the registry call only happens upon first retrieval.

	le::RenderModule mainModule{};
	{
		le::RenderPass resourcePass( "resource copy", LE_RENDER_PASS_TYPE_TRANSFER );
		resourcePass
		    .setSetupCallback( self, pass_resource_setup )
		    .setExecuteCallback( self, pass_resource_exec ) //
		    ;

		le::RenderPass renderPassPre( "prepass", LE_RENDER_PASS_TYPE_DRAW );
		renderPassPre
		    .setSetupCallback( self, pass_pre_setup )
		    .setExecuteCallback( self, pass_pre_exec ) //
		    ;

		le::RenderPass renderPassFinal( "root", LE_RENDER_PASS_TYPE_DRAW );
		renderPassFinal
		    .setSetupCallback( self, pass_final_setup )
		    .setExecuteCallback( self, pass_final_exec ) //
		    ;

		mainModule.addRenderPass( resourcePass );
		mainModule.addRenderPass( renderPassPre );
		mainModule.addRenderPass( renderPassFinal );
	}

	// Update will call all rendercallbacks in this module.
	// the RECORD phase is guaranteed to execute - all rendercallbacks will get called.
	self->renderer.update( mainModule );

	self->frame_counter++;

	return true; // keep app alive
}

// ----------------------------------------------------------------------

static void workbench_app_destroy( workbench_app_o *self ) {

	if ( self->gltfDoc ) {
		static auto const &gltf_i = Registry::getApi<le_gltf_loader_api>()->document_i;
		gltf_i.destroy( self->gltfDoc );
		self->gltfDoc = nullptr;
	}

	if ( self->imguiContext ) {
		ImGui::DestroyContext( self->imguiContext );
		self->imguiContext = nullptr;
	}
	delete ( self );
}

// ----------------------------------------------------------------------

static void workbench_app_process_ui_events( workbench_app_o *self ) {

	using namespace pal_window; // For calls to the Window object

	ImGuiIO &io = ImGui::GetIO();

	bool wantsFullscreenToggle = false; // Accumulate fullscreen toggles to minimize toggles.

	LeUiEvent const *events;
	uint32_t         numEvents = 0;

	window_i.get_ui_event_queue( self->window, &events, numEvents );

	LeUiEvent const *const events_end = events + numEvents; // end iterator

	for ( LeUiEvent const *event = events; event != events_end; event++ ) {
		// Process events in sequence

		switch ( event->event ) {
		case LeUiEvent::Type::eKey: {
			auto &e = event->key;

			if ( e.key == LeUiEvent::NamedKey::eF11 && e.action == LeUiEvent::ButtonAction::eRelease ) {
				wantsFullscreenToggle ^= 1;
			}

			if ( e.action == LeUiEvent::ButtonAction::ePress ) {
				io.KeysDown[ uint32_t( e.key ) ] = true;
			}
			if ( e.action == LeUiEvent::ButtonAction::eRelease ) {
				io.KeysDown[ uint32_t( e.key ) ] = false;
			}

			// ( void )e.mods; // Modifiers are not reliable across systems
			io.KeyCtrl  = io.KeysDown[ uint32_t( LeUiEvent::NamedKey::eLeftControl ) ] || io.KeysDown[ uint32_t( LeUiEvent::NamedKey::eRightControl ) ];
			io.KeyShift = io.KeysDown[ uint32_t( LeUiEvent::NamedKey::eLeftShift ) ] || io.KeysDown[ uint32_t( LeUiEvent::NamedKey::eRightShift ) ];
			io.KeyAlt   = io.KeysDown[ uint32_t( LeUiEvent::NamedKey::eLeftAlt ) ] || io.KeysDown[ uint32_t( LeUiEvent::NamedKey::eRightAlt ) ];
			io.KeySuper = io.KeysDown[ uint32_t( LeUiEvent::NamedKey::eLeftSuper ) ] || io.KeysDown[ uint32_t( LeUiEvent::NamedKey::eRightSuper ) ];

		} break;
		case LeUiEvent::Type::eCharacter: {
			auto &e = event->character;
			if ( e.codepoint > 0 && e.codepoint < 0x10000 ) {
				io.AddInputCharacter( uint16_t( e.codepoint ) );
			}
		} break;
		case LeUiEvent::Type::eCursorPosition: {
			auto &e        = event->cursorPosition;
			self->mousePos = {float( e.x ), float( e.y )};
		} break;
		case LeUiEvent::Type::eCursorEnter: {
			auto &e = event->cursorEnter;
		} break;
		case LeUiEvent::Type::eMouseButton: {
			auto &e = event->mouseButton;
			if ( e.button >= 0 && e.button < int( self->mouseButtonStatus.size() ) ) {
				self->mouseButtonStatus[ size_t( e.button ) ] = ( e.action == LeUiEvent::ButtonAction::ePress );
			}
		} break;
		case LeUiEvent::Type::eScroll: {
			auto &e = event->scroll;
			io.MouseWheelH += float( e.x_offset );
			io.MouseWheel += float( e.y_offset );

		} break;
		} // end switch event->event
	}

	// -- Forward events to camera controller. Todo: we could have filtered events, based on whether
	// a gui window was hit by the mouse, for example.
	self->cameraController.processEvents( self->camera, events, numEvents );

	// We have accumulated all fullscreen toggles - if we wanted to change to fullscreen-we should do it now.
	// We do this so that the screen size does not change whilst we are processing the current event stream.
	// but it might be an idea to do so.
	//
	if ( wantsFullscreenToggle ) {
		// toggle fullscreen if requested.
		window_i.toggle_fullscreen( self->window );
	}
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_workbench_app_api( void *api ) {
	auto  workbench_app_api_i = static_cast<workbench_app_api *>( api );
	auto &workbench_app_i     = workbench_app_api_i->workbench_app_i;

	workbench_app_i.initialize = initialize;
	workbench_app_i.terminate  = terminate;

	workbench_app_i.create            = workbench_app_create;
	workbench_app_i.destroy           = workbench_app_destroy;
	workbench_app_i.update            = workbench_app_update;
	workbench_app_i.process_ui_events = workbench_app_process_ui_events;

#ifdef PLUGINS_DYNAMIC
	Registry::loadLibraryPersistently( "./libs/imgui/libimgui.so" );
#endif
}

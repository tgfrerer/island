#include "le_imgui.h"
#include "pal_api_loader/ApiRegistry.hpp"

#include "3rdparty/include/imgui/imgui.h"

#include "le_renderer/le_renderer.h"
#include "le_pipeline_builder/le_pipeline_builder.h"
#include "le_ui_event/le_ui_event.h"

#include "le_backend_vk/le_backend_vk.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // vulkan clip space is from 0 to 1
#define GLM_FORCE_RIGHT_HANDED      // glTF uses right handed coordinate system, and we're following its lead.
#include "glm.hpp"
#include "gtc/matrix_transform.hpp"

#include <array>
#include <vector>

struct FontTextureInfo {
	uint8_t *                             pixels                  = nullptr;
	int32_t                               width                   = 0;
	int32_t                               height                  = 0;
	static constexpr le_resource_handle_t le_image_sampler_handle = LE_IMAGE_SAMPLER_RESOURCE( "ImguiDefaultFontTexture" );
	static constexpr le_resource_handle_t le_image_handle         = LE_IMG_RESOURCE( "ImguiDefaultFontImage" );
	bool                                  wasUploaded             = false;
};

struct le_mouse_event_data_o {
	std::array<uint32_t, 3> buttonState{};
	glm::vec2               cursor_pos{};
};

struct le_imgui_o {
	ImGuiContext *        imguiContext            = nullptr;
	FontTextureInfo       imguiTexture            = {};
	le_mouse_event_data_o mouse_state             = {};
	bool                  areResourcesInitialised = false; // whether resources were initialised
};

// ----------------------------------------------------------------------

static le_imgui_o * le_imgui_create() {
	auto self = new le_imgui_o();

	self->imguiContext = ImGui::CreateContext( nullptr );

	return self;
}

// ----------------------------------------------------------------------

static void le_imgui_destroy( le_imgui_o *self ) {

	ImGui::DestroyContext( self->imguiContext );

	delete self;
}

// ----------------------------------------------------------------------

static void le_imgui_begin_frame( le_imgui_o *self ) {
	// -- destroy imgui context
	ImGui::SetCurrentContext( self->imguiContext );
	ImGui::NewFrame();
}

// ----------------------------------------------------------------------

static void le_imgui_end_frame( le_imgui_o *self ) {
	ImGui::SetCurrentContext( self->imguiContext );
	ImGui::Render();
}

// ----------------------------------------------------------------------
/// Load font, generate font atlas, upload font atlas
/// declare font atlas resource
/// Setup key mappings
/// Upload any resources which need uploading
///
static void le_imgui_setup_gui_resources( le_imgui_o *self, le_render_module_o *p_render_module, float display_width, float display_height ) {

	auto module = le::RenderModule{p_render_module};

	if ( self->areResourcesInitialised ) {

		// If resources were initialised, the only thing which remains for us to do
		// is to tell the rendergraph what kind of resource to expect, which means
		// we must declare the resource.

		auto fontImgInfo = le::ImageInfoBuilder()
		                       .setExtent( uint32_t( self->imguiTexture.width ), uint32_t( self->imguiTexture.height ) )
		                       .setUsageFlags( {LE_IMAGE_USAGE_TRANSFER_DST_BIT} )
		                       .setFormat( le::Format::eR8G8B8A8Unorm )
		                       .build(); // create resource for imgui font texture if it does not yet exist.

		module.declareResource( self->imguiTexture.le_image_handle, fontImgInfo );

		return;
	}

	// ---------| invariant: resources are not initialised yet.

	// get imgui font texture handle

	ImGuiIO &io = ImGui::GetIO();

	uint8_t *pixels = nullptr;
	io.Fonts->AddFontFromFileTTF( "./resources/fonts/IBMPlexSans-Regular.otf", 20.0f, nullptr, io.Fonts->GetGlyphRangesDefault() );
	io.Fonts->GetTexDataAsRGBA32( &self->imguiTexture.pixels, &self->imguiTexture.width, &self->imguiTexture.height );

	//	self->imguiTexture.pixels_data.resize( self->imguiTexture.width * self->imguiTexture.height * 4u );
	//	memcpy( self->imguiTexture.pixels_data.data(), pixels, self->imguiTexture.pixels_data.size() );

	// Declare font image resource
	auto fontImgInfo = le::ImageInfoBuilder()
	                       .setExtent( uint32_t( self->imguiTexture.width ), uint32_t( self->imguiTexture.height ) )
	                       .setUsageFlags( {LE_IMAGE_USAGE_TRANSFER_DST_BIT} )
	                       .setFormat( le::Format::eR8G8B8A8Unorm )
	                       .build(); // create resource for imgui font texture if it does not yet exist.

	module.declareResource( self->imguiTexture.le_image_handle, fontImgInfo );

	// Upload resources

	le::RenderPass pass{"imguiSetup", LE_RENDER_PASS_TYPE_TRANSFER};

	pass
	    .useImageResource( self->imguiTexture.le_image_handle, {LE_IMAGE_USAGE_TRANSFER_DST_BIT} )
	    .setExecuteCallback( self, []( le_command_buffer_encoder_o *p_encoder, void *user_data ) {
		    auto imgui = static_cast<le_imgui_o *>( user_data );

		    // Tell encoder to upload imgui image - but only once
		    if ( false == imgui->imguiTexture.wasUploaded ) {

			    le::Encoder encoder{p_encoder};
			    size_t      numBytes = size_t( imgui->imguiTexture.width ) * size_t( imgui->imguiTexture.height ) * 4;

			    auto writeInfo = le::BuilderWriteToImageSettings()
			                         .setImageW( int32_t( imgui->imguiTexture.width ) )
			                         .setImageH( int32_t( imgui->imguiTexture.height ) )
			                         .build();

			    encoder.writeToImage( imgui->imguiTexture.le_image_handle, writeInfo, imgui->imguiTexture.pixels, numBytes );
			    imgui->imguiTexture.wasUploaded = true;
		    }
	    } );

	// Now we must add the transfer pass to the module so that it gets executed.

	module.addRenderPass( pass );

	// We want to save the raw value in the pointer, because if we passed in a
	// pointer to the name of the texture, the texture may have changed.
	// for this to work, we first cast to uint64_t, then cast to void*
	io.Fonts->TexID = ( void * )( uint64_t const & )self->imguiTexture.le_image_sampler_handle;

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

	io.DisplaySize.x = display_width;
	io.DisplaySize.y = display_height;

	self->areResourcesInitialised = true;
}

// ----------------------------------------------------------------------

static void le_imgui_draw_gui( le_imgui_o *self, le_renderpass_o *p_rp ) {

	auto rp = le::RenderPass{p_rp};

	// TODO: We must implement a safeguard in renderpass which checks
	// resources, and makes sure that each resource is declared consistently.
	//
	rp.sampleTexture( self->imguiTexture.le_image_sampler_handle, {{le::Filter::eLinear, le::Filter::eLinear}, {self->imguiTexture.le_image_handle, {}}} );

	rp.setExecuteCallback( self, []( le_command_buffer_encoder_o *p_encoder, void *user_data ) {
		auto encoder = le::Encoder{p_encoder};
		auto imgui   = static_cast<le_imgui_o *>( user_data );

		// Fetch pipeline Manager so that we can create pipeline,
		// and shader modules if needed.
		//
		// These objects are owned by the renderer/or backend, so we don't have to worry about
		// deleting them.
		//
		auto pipelineManager = encoder.getPipelineManager();

		// TODO: we should not have to call into backend this way - there must be a way for encoder to
		// be self-sufficient, i.e. not depend on renderer in any way.
		static auto imguiVertShader = le_backend_vk::le_pipeline_manager_i.create_shader_module( pipelineManager, "./resources/shaders/imgui.vert", {le::ShaderStage::eVertex} );
		static auto imguiFragShader = le_backend_vk::le_pipeline_manager_i.create_shader_module( pipelineManager, "./resources/shaders/imgui.frag", {le::ShaderStage::eFragment} );

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

		// Setting this static means that the builder only runs for the very first time.
		//
		// Which makes sense since every other time it will return the same hash value for
		// given data.
		// and all calculations will be in vain, and write access to the cache is expensive.
		static auto psoImgui = LeGraphicsPipelineBuilder( pipelineManager )
		                           .addShaderStage( imguiFragShader )
		                           .addShaderStage( imguiVertShader )
		                           .setVertexInputAttributeDescriptions( attrs.data(), attrs.size() )
		                           .setVertexInputBindingDescriptions( bindings.data(), bindings.size() )
		                           .build();

		auto extents = encoder.getRenderpassExtent();

		// We patch display size as late as possible - here is the best place, since we know extents
		// of the renderpass into which the gui will be drawn.
		ImGuiIO &io    = ImGui::GetIO();
		io.DisplaySize = {float( extents.width ), float( extents.height )};

		ImDrawData *drawData = ImGui::GetDrawData();
		if ( drawData ) {
			// draw imgui

			le::Viewport viewports[ 1 ] = {
			    {0.f, 0.f, float( extents.width ), float( extents.height ), 0.f, 1.f},
			};

			auto ortho_projection = glm::ortho( 0.f, float( extents.width ), 0.f, float( extents.height ) );

			ImVec2 display_pos = drawData->DisplayPos;

			encoder
			    .bindGraphicsPipeline( psoImgui )
			    .setViewports( 0, 1, &viewports[ 0 ] )
			    .setArgumentData( LE_ARGUMENT_NAME( "MatrixStack" ), &ortho_projection, sizeof( glm::mat4 ) )
			    .setArgumentTexture( LE_ARGUMENT_NAME( "tex_unit_0" ), imgui->imguiTexture.le_image_sampler_handle, 0 ) //
			    ;

			le_resource_handle_t currentTexture = imgui->imguiTexture.le_image_sampler_handle; // we check this for changes so that we don't have to switch state that often.

			ImVec4 currentClipRect{};

			for ( ImDrawList **cmdList = drawData->CmdLists; cmdList != drawData->CmdLists + drawData->CmdListsCount; cmdList++ ) {
				auto &im_cmd_list = *cmdList;

				// upload index data
				encoder.setIndexData( im_cmd_list->IdxBuffer.Data, size_t( im_cmd_list->IdxBuffer.size() * sizeof( ImDrawIdx ) ), le::IndexType::eUint16 );
				// upload vertex data
				encoder.setVertexData( im_cmd_list->VtxBuffer.Data, size_t( im_cmd_list->VtxBuffer.size() * sizeof( ImDrawVert ) ), 0 );

				uint32_t index_offset = 0;
				for ( const auto &im_cmd : im_cmd_list->CmdBuffer ) {

					if ( im_cmd.UserCallback ) {
						// call user callback
						continue;
					}
					// -----| invariant: im_cmd was not user callback

					static_assert( sizeof( le::Rect2D ) == sizeof( ImVec4 ), "clip rect size must match for direct assignment" );

					// -- update bound texture, but only if texture different from currently bound texture
					le_resource_handle_t const nextTexture = reinterpret_cast<le_resource_handle_t const &>( im_cmd.TextureId );
					if ( nextTexture != currentTexture ) {
						encoder.setArgumentTexture( LE_ARGUMENT_NAME( "tex_unit_0" ), nextTexture, 0 );
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

						encoder.setScissors( 0, 1, &scissor );
					}

					// uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);
					encoder.drawIndexed( im_cmd.ElemCount, 1, index_offset, 0, 0 );
					index_offset += im_cmd.ElemCount;
				}

			} // end for ImDrawList
		}     // end if DrawData
	} );
}

// ----------------------------------------------------------------------

void le_imgui_process_events( le_imgui_o *self, LeUiEvent const *events, size_t numEvents ) {
	// Todo: filter relevant events, update internal state based on events.
	LeUiEvent const *const events_end = events + numEvents; // end iterator

	ImGui::SetCurrentContext( self->imguiContext );
	ImGuiIO &io = ImGui::GetIO();

	bool wantsFullscreenToggle = false; // Accumulate fullscreen toggles to minimize toggles.

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
			auto &e                      = event->cursorPosition;
			self->mouse_state.cursor_pos = {float( e.x ), float( e.y )};
		} break;
		case LeUiEvent::Type::eCursorEnter: {
			auto &e = event->cursorEnter;
		} break;
		case LeUiEvent::Type::eMouseButton: {
			auto &e = event->mouseButton;
			if ( e.button >= 0 && e.button < int( self->mouse_state.buttonState.size() ) ) {
				self->mouse_state.buttonState[ size_t( e.button ) ] = ( e.action == LeUiEvent::ButtonAction::ePress );
			}
		} break;
		case LeUiEvent::Type::eScroll: {
			auto &e = event->scroll;
			io.MouseWheelH += float( e.x_offset );
			io.MouseWheel += float( e.y_offset );

		} break;
		} // end switch event->event
	}

	// update mouse pos and buttons
	for ( size_t i = 0; i < self->mouse_state.buttonState.size(); i++ ) {
		// If a mouse press event came, always pass it as "mouse held this frame", so we don't miss click-release events that are shorter than 1 frame.
		io.MouseDown[ i ] = self->mouse_state.buttonState[ i ];
	}
	io.MousePos = {self->mouse_state.cursor_pos.x, self->mouse_state.cursor_pos.y};
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_imgui_api( void *api ) {
	auto &le_imgui_i = static_cast<le_imgui_api *>( api )->le_imgui_i;

	le_imgui_i.create      = le_imgui_create;
	le_imgui_i.destroy     = le_imgui_destroy;
	le_imgui_i.begin_frame = le_imgui_begin_frame;
	le_imgui_i.end_frame   = le_imgui_end_frame;

	le_imgui_i.setup_gui_resources = le_imgui_setup_gui_resources;
	le_imgui_i.process_events      = le_imgui_process_events;
	le_imgui_i.draw_gui            = le_imgui_draw_gui;

	Registry::loadLibraryPersistently( "libimgui.so" );
}

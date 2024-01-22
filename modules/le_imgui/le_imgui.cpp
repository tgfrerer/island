#include "le_imgui.h"
#include "le_core.h"

#include "imgui.h"

#include "le_renderer.hpp"
#include "le_pipeline_builder.h"
#include "le_ui_event.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // vulkan clip space is from 0 to 1
#define GLM_FORCE_RIGHT_HANDED      // glTF uses right handed coordinate system, and we're following its lead.
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include <array>
#include <vector>
#include <algorithm>
#include <iterator>

namespace {
// We use an anonymous namespace here because we don't want to export our shader data to other compilation units
// the code for these files is auto-generated via glslang, and you can recompile from source by issueing
// `./compile_shaders.sh` in the shaders directory.
#include "shaders/imgui_frag.h"
#include "shaders/imgui_vert.h"
} // namespace

static le_img_resource_handle IMGUI_IMG_HANDLE = LE_IMG_RESOURCE( "ImguiDefaultFontImage" );

namespace {
// anonymous namespace so that we can forward-declare
extern const char font_ibm_plex_sans_compressed_data_base85[];
} // namespace

struct FontTextureInfo {
	uint8_t* pixels      = nullptr;
	int32_t  width       = 0;
	int32_t  height      = 0;
	bool     wasUploaded = false;
};

struct le_mouse_event_data_o {
	std::array<uint32_t, 3> buttonState{};
	glm::vec2               cursor_pos{};
};

struct le_imgui_o {
	ImGuiContext*         imguiContext            = nullptr;
	FontTextureInfo       imguiTexture            = {};
	le_mouse_event_data_o mouse_state             = {};
	le_texture_handle     texture_font            = {};
	bool                  areResourcesInitialised = false; // whether resources were initialised
};

// ----------------------------------------------------------------------

static le_imgui_o* le_imgui_create() {
	auto self = new le_imgui_o();

	self->imguiContext = ImGui::CreateContext( nullptr );
	self->texture_font = le::Renderer::produceTextureHandle( "ImguiDefaultFontTexture" );

	return self;
}

// ----------------------------------------------------------------------

static void le_imgui_destroy( le_imgui_o* self ) {

	ImGui::DestroyContext( self->imguiContext );

	delete self;
}

// ----------------------------------------------------------------------

static void le_imgui_begin_frame( le_imgui_o* self ) {
	// -- destroy imgui context
	ImGui::SetCurrentContext( self->imguiContext );
	ImGui::NewFrame();
}

// ----------------------------------------------------------------------

static void le_imgui_end_frame( le_imgui_o* self ) {
	ImGui::SetCurrentContext( self->imguiContext );
	ImGui::Render();
}

// ----------------------------------------------------------------------
/// Load font, generate font atlas, upload font atlas
/// declare font atlas resource
/// Setup key mappings
/// Upload any resources which need uploading
///
static void le_imgui_setup_gui_resources( le_imgui_o* self, le_rendergraph_o* render_graph, float display_width, float display_height ) {

	auto module = le::RenderGraph{ render_graph };

	if ( self->areResourcesInitialised ) {

		// If resources were initialised, the only thing which remains for us to do
		// is to tell the rendergraph what kind of resource to expect, which means
		// we must declare the resource.

		auto fontImgInfo = le::ImageInfoBuilder()
		                       .setExtent( uint32_t( self->imguiTexture.width ), uint32_t( self->imguiTexture.height ) )
		                       .setUsageFlags( le::ImageUsageFlags( le::ImageUsageFlagBits::eTransferDst ) )
		                       .setFormat( le::Format::eR8G8B8A8Unorm )
		                       .build(); // create resource for imgui font texture if it does not yet exist.

		module.declareResource( IMGUI_IMG_HANDLE, fontImgInfo );

		return;
	}

	// ---------| invariant: resources are not initialised yet.

	// get imgui font texture handle

	ImGuiIO& io = ImGui::GetIO();

	// test whether we can find font file - if yes, load it, otherwise use in-memory font.

	io.Fonts->AddFontFromMemoryCompressedBase85TTF( font_ibm_plex_sans_compressed_data_base85, 20.0f, nullptr, io.Fonts->GetGlyphRangesDefault() );
	io.Fonts->GetTexDataAsRGBA32( &self->imguiTexture.pixels, &self->imguiTexture.width, &self->imguiTexture.height );

	// Declare font image resource
	auto fontImgInfo =
	    le::ImageInfoBuilder()
	        .setExtent( uint32_t( self->imguiTexture.width ), uint32_t( self->imguiTexture.height ) )
	        .setUsageFlags( le::ImageUsageFlagBits::eTransferDst | le::ImageUsageFlagBits::eSampled )
	        .setFormat( le::Format::eR8G8B8A8Unorm )
	        .build(); // create resource for imgui font texture if it does not yet exist.

	module.declareResource( IMGUI_IMG_HANDLE, fontImgInfo );

	// Upload resources

	le::RenderPass pass{ "imguiSetup", le::QueueFlagBits::eTransfer };

	pass
	    .useImageResource( IMGUI_IMG_HANDLE, le::AccessFlagBits2::eTransferWrite )
	    .setExecuteCallback( self, []( le_command_buffer_encoder_o* p_encoder, void* user_data ) {
		    auto imgui = static_cast<le_imgui_o*>( user_data );

		    // Tell encoder to upload imgui image - but only once
		    if ( false == imgui->imguiTexture.wasUploaded ) {

			    le::TransferEncoder encoder{ p_encoder };
				size_t              numBytes = size_t( imgui->imguiTexture.width ) * size_t( imgui->imguiTexture.height ) * 4;

			    auto writeInfo = le::WriteToImageSettingsBuilder()
			                         .setImageW( int32_t( imgui->imguiTexture.width ) )
			                         .setImageH( int32_t( imgui->imguiTexture.height ) )
			                         .build();

			    encoder.writeToImage( IMGUI_IMG_HANDLE, writeInfo, imgui->imguiTexture.pixels, numBytes );
			    imgui->imguiTexture.wasUploaded = true;
		    }
	    } );

	// Now we must add the transfer pass to the module so that it gets executed.

	module.addRenderPass( pass );

	// We want to save the raw value in the pointer, because if we passed in a
	// pointer to the name of the texture, the texture may have changed.
	// for this to work, we first cast to uint64_t, then cast to void*
	io.Fonts->TexID = static_cast<void*>( self->texture_font );

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

static void le_imgui_draw_gui( le_imgui_o* self, le_renderpass_o* p_rp ) {

	auto rp = le::RenderPass{ p_rp };

	// TODO: We must implement a safeguard in renderpass which checks
	// resources, and makes sure that each resource is declared consistently.
	//
	rp.sampleTexture( self->texture_font, { { le::Filter::eLinear, le::Filter::eLinear }, { IMGUI_IMG_HANDLE, {} } } );

	rp.setExecuteCallback( self, []( le_command_buffer_encoder_o* p_encoder, void* user_data ) {
		auto encoder = le::GraphicsEncoder{ p_encoder };
		auto imgui   = static_cast<le_imgui_o*>( user_data );

		// Fetch pipeline Manager so that we can create pipeline,
		// and shader modules if needed.
		//
		// These objects are owned by the renderer/or backend, so we don't have to worry about
		// deleting them.
		//
		auto pipelineManager = encoder.getPipelineManager();

		// Attribute input via imGUI:
		//
		// struct ImDrawVert{
		//	  ImVec2  pos;
		//	  ImVec2  uv;
		//	  ImU32   col;
		// };
		//
		static auto psoImgui =
		    LeGraphicsPipelineBuilder( pipelineManager )
		        .addShaderStage(
		            LeShaderModuleBuilder( pipelineManager )
		                .setShaderStage( le::ShaderStage::eVertex )
		                .setSpirvCode( SPIRV_SOURCE_IMGUI_VERT, sizeof( SPIRV_SOURCE_IMGUI_VERT ) / sizeof( uint32_t ) )
		                .setHandle( LE_SHADER_MODULE_HANDLE( "imgui_vert_shader" ) )
		                .build() )
		        .addShaderStage(
		            LeShaderModuleBuilder( pipelineManager )
		                .setShaderStage( le::ShaderStage::eFragment )
		                .setSpirvCode( SPIRV_SOURCE_IMGUI_FRAG, sizeof( SPIRV_SOURCE_IMGUI_FRAG ) / sizeof( uint32_t ) )
		                .setHandle( LE_SHADER_MODULE_HANDLE( "imgui_frag_shader" ) )
		                .build() )
		        .withAttributeBindingState()
		        .addBinding( sizeof( ImDrawVert ) )
		        .addAttribute( offsetof( ImDrawVert, pos ), le_num_type::eFloat, 2 )
		        .addAttribute( offsetof( ImDrawVert, uv ), le_num_type::eFloat, 2 )
		        .addAttribute( offsetof( ImDrawVert, col ), le_num_type::eChar, 4, true )
		        .end()
		        .end()
		        .build();

		auto extents = encoder.getRenderpassExtent();

		// We patch display size as late as possible - here is the best place, since we know extents
		// of the renderpass into which the gui will be drawn.
		ImGuiIO& io    = ImGui::GetIO();
		io.DisplaySize = { float( extents.width ), float( extents.height ) };

		ImDrawData* drawData = ImGui::GetDrawData();
		if ( drawData ) {
			// draw imgui

			le::Viewport viewports[ 1 ] = {
			    { 0.f, 0.f, float( extents.width ), float( extents.height ), 0.f, 1.f },
			};

			auto ortho_projection = glm::ortho( 0.f, float( extents.width ), 0.f, float( extents.height ) );

			ImVec2 display_pos = drawData->DisplayPos;

			encoder
			    .bindGraphicsPipeline( psoImgui )
			    .setViewports( 0, 1, &viewports[ 0 ] )
			    .setArgumentData( LE_ARGUMENT_NAME( "Mvp" ), &ortho_projection, sizeof( glm::mat4 ) )
			    .setArgumentTexture( LE_ARGUMENT_NAME( "tex_unit_0" ), imgui->texture_font, 0 ) //
			    ;

			le_texture_handle currentTexture = imgui->texture_font; // we check this for changes so that we don't have to switch state that often.

			ImVec4 currentClipRect{};

			for ( ImDrawList** cmdList = drawData->CmdLists; cmdList != drawData->CmdLists + drawData->CmdListsCount; cmdList++ ) {
				auto& im_cmd_list = *cmdList;

				// upload index data
				encoder.setIndexData( im_cmd_list->IdxBuffer.Data, size_t( im_cmd_list->IdxBuffer.size() * sizeof( ImDrawIdx ) ), le::IndexType::eUint16 );
				// upload vertex data
				encoder.setVertexData( im_cmd_list->VtxBuffer.Data, size_t( im_cmd_list->VtxBuffer.size() * sizeof( ImDrawVert ) ), 0 );

				uint32_t index_offset = 0;
				for ( const auto& im_cmd : im_cmd_list->CmdBuffer ) {

					if ( im_cmd.UserCallback ) {
						// call user callback
						continue;
					}
					// -----| invariant: im_cmd was not user callback

					static_assert( sizeof( le::Rect2D ) == sizeof( ImVec4 ), "clip rect size must match for direct assignment" );

					// -- update bound texture, but only if texture different from currently bound texture
					le_texture_handle const nextTexture = reinterpret_cast<le_texture_handle>( im_cmd.TextureId );
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

void le_imgui_process_events( le_imgui_o* self, LeUiEvent const* events, uint32_t numEvents ) {
	// Todo: filter relevant events, update internal state based on events.
	LeUiEvent const* const events_end = events + numEvents; // end iterator

	ImGui::SetCurrentContext( self->imguiContext );
	ImGuiIO& io = ImGui::GetIO();

	for ( LeUiEvent const* event = events; event != events_end; event++ ) {
		// Process events in sequence

		switch ( event->event ) {
		case LeUiEvent::Type::eKey: {
			auto& e = event->key;

			if ( e.key == le::UiEvent::NamedKey::eUnknown ) {
				break;
			};

			// -----------| invariant: key is not -1 (unknown)

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
			auto& e = event->character;
			if ( e.codepoint > 0 && e.codepoint < 0x10000 ) {
				io.AddInputCharacter( uint16_t( e.codepoint ) );
			}
		} break;
		case LeUiEvent::Type::eCursorPosition: {
			auto& e                      = event->cursorPosition;
			self->mouse_state.cursor_pos = { float( e.x ), float( e.y ) };
		} break;
		case LeUiEvent::Type::eCursorEnter: {
			auto& e = event->cursorEnter;
		} break;
		case LeUiEvent::Type::eMouseButton: {
			auto& e = event->mouseButton;
			if ( e.button >= 0 && e.button < int( self->mouse_state.buttonState.size() ) ) {
				self->mouse_state.buttonState[ size_t( e.button ) ] = ( e.action == LeUiEvent::ButtonAction::ePress );
			}
		} break;
		case LeUiEvent::Type::eScroll: {
			auto& e = event->scroll;
			io.MouseWheelH += float( e.x_offset );
			io.MouseWheel += float( e.y_offset );

		} break;
		default:
			break;
		} // end switch event->event
	}

	// update mouse pos and buttons
	for ( size_t i = 0; i < self->mouse_state.buttonState.size(); i++ ) {
		// If a mouse press event came, always pass it as "mouse held this frame", so we don't miss click-release events that are shorter than 1 frame.
		io.MouseDown[ i ] = self->mouse_state.buttonState[ i ];
	}
	io.MousePos = { self->mouse_state.cursor_pos.x, self->mouse_state.cursor_pos.y };
}

// ----------------------------------------------------------------------

void le_imgui_process_and_filter_events( le_imgui_o* self, LeUiEvent* events, uint32_t* num_events ) {
	if ( nullptr == num_events || *num_events == 0 ) {
		return;
	}
	// ---------| invariant: num_events > 0
	le_imgui::le_imgui_i.process_events( self, events, *num_events );

	auto const& io            = ImGui::GetIO();
	uint32_t    ioFilterFlags = 0;

	if ( io.WantCaptureMouse ) {
		ioFilterFlags |= uint32_t( LeUiEvent::Type::eCursorEnter );
		ioFilterFlags |= uint32_t( LeUiEvent::Type::eCursorPosition );
		ioFilterFlags |= uint32_t( LeUiEvent::Type::eScroll );
		ioFilterFlags |= uint32_t( LeUiEvent::Type::eMouseButton );
	}

	if ( io.WantCaptureKeyboard ) {
		ioFilterFlags |= uint32_t( LeUiEvent::Type::eKey );
		ioFilterFlags |= uint32_t( LeUiEvent::Type::eCharacter );
	}

	// Filter out events which have been captured by ImGui as these
	// should not further propagate to other ui elements.
	// We do this by copying only events ones which pass our filter in a new
	// events queue, which we then pass on for further processing.
	//
	std::vector<LeUiEvent> ev{};
	std::copy_if( events, events + *num_events, std::back_inserter( ev ), [ &ioFilterFlags ]( LeUiEvent const& e ) -> bool {
		// This will only return true if none of the filter flags were found
		// in the current event type.
		return !( uint32_t( e.event ) & ioFilterFlags );
	} );
	memcpy( events, ev.data(), sizeof( LeUiEvent ) * ev.size() );
	*num_events = ev.size();
}

// ----------------------------------------------------------------------

void le_imgui_register_set_clipboard_string_cb( le_imgui_o* self, void* addr ) {
	ImGui::SetCurrentContext( self->imguiContext );
	ImGuiIO& io           = ImGui::GetIO();
	io.SetClipboardTextFn = ( void ( * )( void*, char const* ) )addr;
}

// ----------------------------------------------------------------------

void le_imgui_register_get_clipboard_string_cb( le_imgui_o* self, void* addr ) {
	ImGui::SetCurrentContext( self->imguiContext );
	ImGuiIO& io           = ImGui::GetIO();
	io.GetClipboardTextFn = ( char const* ( * )( void* ))addr;
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_imgui, api ) {
	auto& le_imgui_i = static_cast<le_imgui_api*>( api )->le_imgui_i;

	le_imgui_i.create                           = le_imgui_create;
	le_imgui_i.destroy                          = le_imgui_destroy;
	le_imgui_i.begin_frame                      = le_imgui_begin_frame;
	le_imgui_i.end_frame                        = le_imgui_end_frame;
	le_imgui_i.process_events                   = le_imgui_process_events;
	le_imgui_i.process_and_filter_events        = le_imgui_process_and_filter_events;
	le_imgui_i.setup_resources                  = le_imgui_setup_gui_resources;
	le_imgui_i.draw                             = le_imgui_draw_gui;
	le_imgui_i.register_get_clipboard_string_cb = le_imgui_register_get_clipboard_string_cb;
	le_imgui_i.register_set_clipboard_string_cb = le_imgui_register_set_clipboard_string_cb;

#if defined( PLUGINS_DYNAMIC )
	le_core_load_library_persistently( "./modules/libimgui.so" );
#endif
}

// File: 'resources/fonts/IBMPlexSans-Regular.otf' (69172 bytes)
// Exported using binary_to_compressed_c.cpp
//
// Copyright © 2017 IBM Corp. with Reserved Font Name "Plex"
// This Font Software is licensed under the SIL Open Font License, Version 1.1.
namespace {
const char font_ibm_plex_sans_compressed_data_base85[ 66165 + 1 ] =
    "7])#######gP)b3'/###W<7+>(*ml&Ql#v#BGMT9N8GR.,U+##Q**##O:?t8p+hwYi`'##I.'##+K.h<mj]wd2)dw'09@;#sMet9[mb/WkS^&#m<5+#YCrR<aXaeNJ_5,#,5TkL>`jsB"
    "'/DiFcrf#MW84<#NJ$[D^,Mt''6guLX@7&M;$tLD7$Ws%i]vtLN4pfLKO%KD`IM/P<TP/#UC':#J:B/FpDYf#Rh3O+EZajL-5cAF0pLq1Q-T%#`5B2#HC5,G%?c+vY9E)#_6DxL/KG&#"
    ".c&gLv&bp+V>#t'X#(v#M5DX(pdnW=F*PF%iQhau1L3t$<]UpLWF?##Zik0MSrZ=.k-^Lp&K]W%qYS=u*qIX:I_9.$8^#3#v&]Y#x:T01#ecF#dBP##a&Bi#-)m<-'oj/1j^1;6Yn$##"
    "8(Mv#?NK40o=)4#8UT+#Ovu$'&NaO:(#I&#*oI`t4xUvu#Tv?$f0LhLb?0vLdLZY#i5tZMcN6##GT'B#K5iH-]I,hLmZGs-1j],NaF6##8c(.NUL?##N7i.NSIH##J'%Z$v&WG<@$ev$"
    "5/@S@5_)<%2jn+D2OvV%A`FM_7q`s%UXNxb9-AT&o41`j:6]p&+W.crj$8nN`I@8%/(1B#OA*&+01L^#c<ji01:h#$t7or68hd?$8B=D<'5>>#uvF3bOjWcD+fbA#;)NS]01L^#U<Loe"
    "1:h#$,luCjtrg-6L?IZ$FhI8%@$ev$]FcV-5_)<%Vfi]42OvV%*Y6.6+F=G2`IbR*._t(3VrQX.Vmg#$84xU.XO?,$X;^gLD+i#$pB`T.Lv,?$=@`T.O,?\?$b@`T.Ljp>$lCfT%/[T`3"
    "m_;M-C#gb4rFXj;i=6fG#h0s793i]G&>.FH9rCVC:1k(II_a0M2xxhEu>s`=dbJM='DvlEVDtk1DAu$6GU9I$d:r0,#;1eG38vLFhNViLfaRFNb3BM-,YD5B6J/>BgTV=B;kl`FYuCH-"
    "'Z;J511=GHpg`9C;pwF-/(B%J@-ofG#<4nD)H@h>7eLG->JZlEx8/:Ci)JdFbw#g;r9Z<BjNAOEL.4TBtlpOEg?)S*J3FGH,C%12cOFs1U3(@-eNq<.<rY1FQE-gD2tHX(I($1F0C%F-"
    "1)uVC#T(*HE:;EQ4g]*HxVKSD0;tEF#O`TC@L7WC7>HlE9(oROxKhjEIffDF$hKu:RZ5AJr>G/#B;F&#0E-(#d]>lLDHh*#57]nL@01*#Z)1/#D'BkL<ht)#3*B;-RqEn/I<x-#U5C/#"
    "P`.u-(t6qL2(8qLQ9(sL3x.qL/ARA-EE_w-.;9sLi/n+#vS3rL=a5oL#M#.#RY`=-V?:@-x&l?-k=:D.92<)#o><(&Q_VL5U;*L5$.Z'8j)O-Q0^c3=QRDL5IWt:M`A+jLeR=RM,hipL"
    "C-lrL$*B-#(o,D-$'6-..BnqLs'm.#6-XU-0=ho-Mu(F7od2RNg%b-6Jr[w'i^6)FxCbDFDEa'/`u,qi`<s'//:Ne$sh%;H:6i`F7Ss>-*i:w^0G^Qs3_f'&Uo`'JaX(58EEmcE_%#g;"
    "WGD2C5k7sI(Qh34A_-XCS%co7_e6L#qhY'83^i+M8$;@-Fs0@/^st.#e,KkL>?1sLrX`/#+p-A-OgoF-AhD&.7g$qLjnY.#t/rfDgY3)F20CF.7+w?09uP3twBt4J;<cPBw^TmBgRDJ1"
    "YBqS-^Mo7Mr5JqL$Gp-#[RFv-+sarL4r%qLY_4rL9RxqLS;(sL5RxqLD'crLfG:sL=kFrLi3)/#PMM=-=SR6/YSq/#,T3rL=@;/#v2RA-?5T;-@5)=-CufN-LF4v-,Z<rLL*8qLvq,D-"
    "odq@-=T@0VrT[I-^wX?-FTJF-R/PG-u.FCM13oiLFR#<-X$5@MZj(P-lEII-`S1H-W?:@-]S1H-IT#<-%v&$.35KSM?_4rL0]5<-HtjgQl)8qL_[_@-h3(@-r%9;-/]sfL`MVG)Z2c'&"
    "dvrx+^>l'&2UQv$[fDX(HNeCs=v0F%6CYF%LK)k#XTgt@BU<SI#IQcDcoP`<:^v1BDQCaEBP>+#`=#+#mvv(#E8E)#IDW)#MPj)#Q]&*#_I5+#K:w0#(A*1#soA*#a3=&#e?O&#iKb&#"
    "mWt&#qd0'#upB'##'U'#'3h'#+?$(#/K6(#3WH(#7dZ(#;pm(#B<x-#USq/#d7g*#s7Fi%V]UV$0u68%6INP&<h/2'@*gi'DBGJ(HZ(,)Ls_c)P5@D*TMw%+XfW]+7#T5oO)]-#I0f-#"
    "I->>#MgkI#&G<L#<2uQ#Z9J5#0E]5#7&Y6#cZT:#GTv;#HKvV#*Ml>#*AY>#Z-=A#e9OA#lpKB#[7#F#ZxJ*#d5C/#KRp2#oXN1#$<E)#Xmk.#6D>b#B=3L#]L:7#.m?0#/M<1#chf=#"
    "a-GY##,###x)1/#A$####^t&#3Oi,#9lj1#:8*L#%EV,#xUcb#sNFI#TbhR#$]H(#`1g*#x$),#tnl+#-isL#u<&=#uE^2#*=M,#&3A5#xRE1#&1;,#J<+I#e0oH#7r&M#_9)O#fQMO#"
    "jfw#M=<bi_49]%kZvEuu_d;58cVKG;ecgc;kPoi9px0,;)1TAX[57W[T1vr$,#5L#HRu1B_#6MKF4AMKGWarQ8AxMT,=lu#$2N`#=U_n##'Q7$9:7w#Pq3x#hp^#$s1ub'$[eh,O*gU-"
    "<Bx=-Nsk>-er?@-7@KB-M+.H-9sDL-Y;sL-p.5N-)`(O-71rk-96oX-XAUZ-:E2`-j&/a-^Mmg-o@Yj-.rLk-Ghw1.p6Ms-3*ft-Js'v-0qP$.0;U$/OoKR/BZeQomDBiotS5+qgX3tt"
    "Rle+M)lG&#@Vs)#CB+.#C^_XMGgEO#&u=u#DH_)0T=eo#WIuu#.b/N-JU/N-UC_)0mo7=-BN4>-DU/N-`hf/.jEE6NoY/M-u:GN-L[8Z2_QD`-l,8a-JYSi-xRuj-:j/N-;h/N-5]/N-"
    "2bf/._Wb`Mv&#tt2wD#ME<S(W/YWh#MGlu#&oLP/=wW.U<=BcMA<w$vgCG'vTSs)#U2,mqa)Dul;dGxkxv#50EBFr5(gDl.pJ8L#oxsws%M%%sJ>ZbqnPgepg)kho/.dLJc].%jo%2R3"
    "]YhUcTw2F%_go-$9h^@Nx+=rGELRq2FH+##mM.uF&5qU5$H6o-W<NC)j9T_A.r9B#O;ElfKSHt.Tu2;?u7x3+]:7w#GR[w#O=#-Mg(x`*vmaM06rs-$R]uHN4ARA4cU[rmA;nfL/t&@P"
    ")r-AkR?@@'OIS?NJen&M<6Q%N1Y,W-<6B(&/(_#$DXZ&MrBC6$URWfLdOCVMuV<X%b392#h?T2#P_,3#Ts9f#.mk.#ECV,#J&T*#/p8*#m(wg#.S(Z#igf=#qe&i##65##I[@d#Ten[#"
    "Fd@H#F0oH#Ns'J#MgkI#rxZK#3G<L#3`aL#jFKs$(R7L#>>1R#=,lQ#H)Y6#cUUR#7[^U#7n#V#GUTU#hUx5#+E]5#7Kf5#_B0:#ATv;#C<Q;#hOl>#';P>#`*HV#`KkA#`9OA#k6=&#"
    "ue9B#[oJE#[+gE#liAE#?Q=.#=.]-#>XF.#Ud-0#)wH0#-'R0#+k60#,-[0#13e0#=vs1#@E*1#<^N1#E&'2#BK31#K]#3#[Vp2#^i53#`c,3#buG3#ho>3#tkrO#']`O#t1vN#vbiO#"
    "w:v3#wq%P#.+AP#31JP#D:%@#7B.@#;N@@#9H7@#=TI@#?ZR@#Aa[@#Emn@#Cge@#InOQ#SNLR#U$cQ#THCR#kS*T#&Z3T#)gET#*a<T#+mNT#0)kT#2#bT#0sWT#6GBU#=A9U#;;0U#"
    "JFmV#W4QV#M`gU#O@dV#U.HV#T:ZV#aw`W#o9/X#uEAX#)vRH#-,fH#+&]H#exTB#]dcF#I(lQ#$QTU#alBB#5@WD#/ws897Mr/:gu]a#sT.d#/[7d#$7Vc#.=`c#1t[d#1Irc#<B=e#"
    "C?+I#oD`G#i2DG#m>VG#=Y4Q#AfFQ#WRUR#[_hR#^eqR#`k$S#YX_R#M4(R#Q@:R#SFCR#ULLR#O:1R#BV2W#FcDW#Lu`W#N%jW#R1&X#T7/X#V=8X#P+sW##Uu>#uHc>#TGbA#XStA#"
    "qF6C#uRHC#wXQC##`ZC#sL?C#g(_B#k4qB#m:$C#o@-C#i.hB#L3pE#P?,F#_jlF#cv(G#e&2G#g,;G#apuF##d7H#v].H#%j@H#&g.-#tnl+##uu+#%1;,#gG_/#H'Z3#`3m3#VF^2#"
    "hWik#mpT^##3$_#'9-_#GJs`#+KH_#/QQ_#8,E`#9)3D#K$ng#:p)`#8jv_#IR,j#M_>j#RqYj#'hte#;,85#%%###.VjfL?@S(W=uh*%4%UB#);5##2Csl&8[m,ME-R^5;bL%tjap0g"
    "Pe5LgIrA6$EBR]tabfucs#91Tqs/&#W@=XtP/,F%3*pt'.[Ps-K.IhL5WAa+qj*$MQCSS%Hm$)*nTEM0OZ0bI4TTc;2?ZlAOFH]FGLvs?'G31#G_,3#bK;4#(Kf5#DJ:7#aIe8#'I9:#"
    "CHd;#`G8=#'Jc>#BF7@#_EbA##?-C#?>WD#_OGF#(OrG#DNFI#aMqJ#'MEL#CLpM#o2JP#VInS#66QV#k1Yu#Fen[#(dm_#[UYb#:HFe#o:3h#M-vj#,vbm#Mo-o#k*tp#:6Zr#R)ss#"
    "p=u:$84.w#T3Xx#q2-$$72W%$S1,'$o*M($Aat*$n:<-$Xd%1$9213$YCw4$uBK6$;Bv7$Psi8$hr=:$11.<$`j^>$(^v?$Ji]A$a[uB$4<FE$KsBF$grmG$9@#J$[K`K$vJ4M$<J_N$"
    "XI3P$uH^Q$;H2S$T;JT$$]_r$DqEX$xoD[$@VJ]$ftL_$*nn`$L;$c$o:Nd$0(^e$Gquf$dpIh$*pti$H+ek$l0Bm$9H;o$SA]p$pCC7%9Fes$TE9u$qDdv$=VSx$YU($%vTR%%<T''%"
    "XSQ(%uR&*%8F>+%_pR-%%dk.%=V-0%_hs1%%hG3%Agr4%^fF6%%o6S%@eE9%_v5;%6PS=%SU1?%uan@%>aBB%Z`mC%p:aD%04,F%XjRH%w]kI%CiQK%Y[jL%%bGN%;ZiO%^(uQ%/CnS%"
    "J30U%g2ZV%-2/X%I1YY%f0.[%+*O]%IAH_%j@s`%>wCc%]dRd%#&Lf%Cumg%_tAi%1BMk%K5fl%bxtm%'+e4&Bwsp%^p>r%->Jt%MO:v%mH[w%2H0#&OSm$&kF/&&3FY'&C'V(&`&+*&"
    "&&U+&B%*-&_$T.&%$)0&A#S1&^x'3&%+nO&@w&6&]vP7&#v%9&?uO:&[t$<&xsN=&>s#?&ZrM@&wqxA&=qLC&YpwD&voKF&<ovG&XnJI&umuJ&;mIL&WltM&tkHO&;nsP&VjGR&sirS&"
    "C7(V&b6RW&(6'Y&D5QZ&a4&]&'4P^&C3%`&`2Oa&&2$c&B1Nd&_0#f&(Big&O`ki&%IW1'X2)o&==:r&#BBu&]FJx&:97%'o%q''R0,+',T7-'_@q/'1h8M'bP`4'E[q7'*a#;'p9l>'"
    "eupB'`g1G'WL6K'EH,N'aDVO''D+Q'CCUR'[*[S'Fa1c'8n[HB>CRFHxd?hOOc8:JE#=oMO)r&4M(:oD_NYs8-x@fG7DmHG*dYLF=4BF%61M*HI;%6/oKd<BIZ<SM=5thF.<X7DlN;9C"
    "ijjjE*jXVC2(9F%1`>LFRJFsLKwvh2%G%+%.InhMTAM(G*DqoDj&4nDUq`^HGBWq)DxYK3kcl8.MMTj2X##d3Z5YD4]G:&5^PUA5fC/s7gLJ88i_+p8@u?[B<KKjMG_j2MmlZL2Q+t^-"
    "Ajt*.P(6@9P(6@9P(6@9I>x*74TG9B)`I'ITtkZ-L]o9MlO.J3b6vsB*$a=B-2vLFsccdGoKv<BLS05N3*TNM?]Ud4U58bITWG<8_%Z<-XMp;-gSp;-*cldM1Xa2MN_Ma-Ab&:;moxcM"
    "[o<30q;fuB.QBkEBHI=((^IUC-mgoDZiudMF`ZhMs*56D(NkVCpP^G3Lcs9`R=c3bVhH-duES7N;x^kE,iKC-pd2a-4(^%'<k+LP+->IH`>>)45O0s7vq+p8KpDr;J)HcO$qJjMmbSTC"
    "q6'mB6=OmB*SvLFA2fr%/A,hFkt1tB%a=;CRZU3Cu2j=B'WldGu&&UC>UXMC?VA_SY5Gs-NCP$8/2XMCX1ucM'<qh$E=8pgxdov76YoS8jw.>-+e.n$?q.lLbv).3(=,RMAu^0OG,TtB"
    "$nV=Bg->/DvOAC/)Sk=1wSS73&2C15D-$KDmPBSDDrsfD.cQW-pZ'CSmQYnMO^XnMH75FHnWrTCvG'4EqN?.%1ZWI3buP<-*AtB-GbHA1#HFVCpjM=BnKitBpJj:/rJcdGM2TLOURxqL"
    "#skRMQqkRMWQdLO7O^=B@C=GH3p0SDucD/DV8)+8>WCU8b_IbHxVcdG)hM=BW)=oM0CP+HxVKSD87:aF1];MF/d2p88,N59JQf;-LSwA-<oef$QA$KNi/#0N6kcl$77E@0EHFs-@$;SM"
    "54:SM9l2MO4;CSM6tluBnG^kEg]PZ%Jp;MO<F5UC(EKKFD6s[8$)k>H#i3GH14H>HS?J0Y0Hwh2L?\?Fn6,*@K7c4]H,niYH&niYH,:=j08/?LF3MqoD$,.bHU%O59*arP9Kp/V-r]A(N"
    "rP_T9HcAN-EANM-<7B8%EUDeG/1F$J4r2F7:(:uIPN%%'[x-+%'Z.TMEe)B89I/s7ra>s7sdXv-^KBjM`lAjMtuXs8#,G590[T88k7,dMA/#03`8#d3eC/s7iUfS8e6:@0CBFiM*_]v7"
    "::.I-lxCp.u:ViFogut_S5?@T&NEO+dMtdGXvF>9^HL59t3G59fpwdMvefv7oqSN-x-%f&7lN88j^lS8@:tS8+_eI-khSj-*G&@'6,,(5B8B[BHb?4O[;]AmsSugNfetTBt[F-O5<pNM"
    "O8eEP[iov7daF&5OHkJ-XD`A0bx^kE+VGn:6KkNt.kP:;4<K1G6Yu*.ZKZ<q2v3S*(:?,<1N<,<1N<,<1ec594/4x7^`fS8`F+T8*TLjM#jlkMhkhX8_vVG<bG,<-ow]N-rP2Y$LM=F7"
    "Y9f&6$q+w7lZ+p8e$W&Hi/#0N,]$q$hrsF=^AXD=^AXD=eJt`=2po`=O^o;-h<wT-dfc9'P[RE5+4G<BZ5l'8Ar#2,0Hwh2x7IOk-05IHl:pC>X0WA>R0WA>Zo;j0n.?LFiApoDY`rH>"
    "L`N59'jRQ9K(:oDfP-0NV&*6.4VEPM$kw.O+F[Y8b1h#?*G%'I4N)6C6;t[?m8068F$uP9*l-@9J*fj)fVLQC+m,-G4G%bH9[XcH)&DEHe+wO<j0v<B[.Z6N4#^b4hkk*%`XA@'Yr-+%"
    "Zu-+%[x-+%]%.+%]x$+%1LOU23R[hM^edhMWkmhMjwWB-FxWB-FxWB-FxWB-FxWB-F(t^->=FU2/LeLM09/+4*81g2.T_[->=FU2/LeLM09/+4;n0s7['<g2jc-+%gC.+%hF.+%iI.+%"
    "jL.+%SYq*%LQ;-k`SJf3$PVA59:WA5kM0g2*uUH-jKYk-4tk*I#tk*Ir3WkF*^)@K;b7LM^h/NM7)^j2]1;t-aZTNMTU?LM'sAN2S1J<ohX;:8gSiS8`>iS876^A5r$Q*n&Wjk+%OWkF"
    "$LWkF?A?kkE2Cs-wY%6:rgu^$^p).3g,3+MeZWmL,%EI3s48rLXqvhM369oLGZJ$MN2:w-=:[hMG[vxLeedhMkOnA-b%Z<-t%K,M3%EI3sW4Z-x?LU)?J5@9Uaq*@ktVkF2$dk4k-8SB"
    "f]qV:wJu'&,#/>B/5Iq$8=;iFTu(IMUDiM%,lqaH;F:%')JcdG?b:%'[[.alc%$*O3BP-Op%P0MKrxW8c?$]0Y7FI$:rN<B>=Ea>Fk(eM#MLQ8F)@@'1HW,M:u;j&=.LtUk<+OOX5<.H"
    "n4J3kGV&U`Fp*^G@WEkNtW></KGRR2I9g+O4URr7(+nL,,Om@'.wq5CK52P-1RW(88f3]^1L8ZI%C+<Rt.K)8*S?M5%#p6hP=UkL)-G<8%g-+%Wl-+%Xo-+%Zu-+%Ylq*%h>QW8Dt@Z5"
    "e8FI3/NqQCDCRSD7ml91-GFVC*5?LF/1xiC='oF-b1vlE)s%lD*A2eGs)djD+HG91Y;bdG1<[rLMa96C/wr6*Pgk9B452eGxi[:Ce]bdGLU0@-nE5)F>WOG-X%ZhF=sTv/l7`TAd`+EE"
    "lRv1FGvSuY&),##-gb.#^d`4#7J:7#CPSX#_2qB#2`6K#[1/t#XC>b#i:3h#0Pf9$gbJ&$be&.$U?D0$.=(V$W,-?$J+,B$@TkE$Z,]L$nNgQ$[+lq$eVvZ$&b6k$M,J5%Lp#v$%Bb&%"
    "c:W)%FEi,%rJF.%IIE1%7V=Q%A_<9%6o+>%I.ND%8FGF%:oZK%_n/M%=5]P%oOhn%JKTU%&&sW%iWlb%'&Lf%JUjh%2s@l%5Tgq%%&&t%B+Yu%e*.w%'0bx%MAQ$&nqo&&MK7)&si9+&"
    "3D-,&NIa-&O.:3&9UE5&`]V8&5Po9&%TK>&]xV@&.kCC&n]0F&W6#J&C;+M&lU6k&Jd>R&8+lU&cBeW&/NKY&JAdZ&nRS]&GQR`&&D?c&I[8e&%0Mg&Qr0j&'.wk&>t/m&UjPn&*P+q&"
    "N[hr&iN*t&:)Hv&eLSx&$'RKs.;pEsj;Ujtu4S`tliUP/6l1_tj#>QsKcgPoYJTihgP`B4>33#-xxNDsU><]slFr#l74n],*Gg]tOx;btX[Q^tNo$`$#D0%OUpxxkU9_hB*_T/%XC0b$"
    "c/0v)MAJDsi^Ip$kE9Esph#_scC,51/[@`t5NT`t%]_:o$13#-(3P.<Q.+v,sA>G,wo.-))R/-))fLSW*u$/)u?fd)mF6^tRe3m^B0ET@?sFj^p=3DsY<@WZx]e1-W<WCTNX(2LXZ(*J"
    "e+x)ENB]+-dQY]S:Fsh,]?QaI&Ea>c&aAoXFA^7S?0'WUe3AWZ$ae1-:-'cJf4K[JSPclKXWldIYNC^FqGgR;g-RKnFg566W&o]t='Bjr/GnZ`wj2,%ER?OY#9.^U>bb@Yb6%d,1[/Q&"
    "j*KNa7FQ^T$W7p+Y/(>B1Pb2Bh14#AY2tIId;kb$LTvVEcP]xa8W%q>J[^&ILdk,-?%fSsC&X)sTjl_Ud6l-I%h>%J,<dP,aim,-p0FaHnoA3Y/hLS&@#WZmVua-rSfg=,-&7uS[6V'B"
    "6i+ihZfRxjVX[x[veFk$E^'W]$g&Y,O#rI(aJ`Ls3nl%cZ&;5IL_-I+2=dY/*iZfe@cv'?[.A=jBq.^+^se/hkqQhA*oL/U<<,g$bMm6Vg^R0[w2>)_gHpk,e8ODagt%55Kog]?Uw=EC"
    "`bdo]TN6aLYVpK+;^4u9/07u`TbFx@P930eE_HP'`BiS4;s)4HcvW^IF/55KT9,HICG)2Ljs`tBbMp2%(NJ]hJNSH%O*CCBltM@&x#v[J1KdP,^u)--(mXI,_2^8en4$GpMEF9^%Ac4<"
    "q<t_tNTq[sa%2R%Qs&Ko$9kUc3CFl,Dfmlo(Nnqt1rK19U4`_teW8Trl:'19c_V$ua)*lA`>8C<$8cJo;`#Gs)%*d`1PO2-YW*V$FLU?%3%9<9;'pI(gPF@+0@U?%L_jh'Gp,Y#hxEx+"
    "*uZT.2cRatUQH]s](vq$vNF`kJq*wl:'N&Zx_xj$jR#]dG0s_tm55^tW:CjtK0>^td2uirrZUDU:t0N'qeTDs56S]$I7[C<<ox`Npc;GsJExT%3iQ0u]cA=Y-xNgRPr=U+.+$g7UY^4E"
    "+*rvDiNH6XA5&S]u'8+-=1=+HpFcS&eA#(Kecj@&vCu9&@/pFsbvwfLwN9EsRL8F0=:p?%v;4hW@tDl$eI1Y#C#kT@f,sT-(/[Eh`gRV_VM6>Z(Ss>%%WAuB4-m,)'-YE&%8C[CNr)H("
    "(Y?1FCeSueP_OIsE2;wrPFMX:i+IKsJi?qnV%SN'47MC<t^%#uD%%`$U<6V^P[UE&]XXj$rrOsLYo+ZK=6a'TJU.7-RlumY,<`0-xOu9&87T9&l<U^f$sWX5AgErf^=V)INsPoAk5NgU"
    "gKcn+^.?[K]utWU^Bho+1N=B,En4H,FDB'%ZYh]b[nxPcu:^8_[:4wA0ug(b73*GS>iFq+Y+soD/C=Q&tLXhZ:6wVZWs@<BA_ie,[HUqUv`fS&#j:+Q0gFWS5+gK%$YjS&/U@[b6GaUZ"
    "lgl[bQG7N'%`TI[5To]thE&e?m2AWZ/,7xL25OCT0m(O=)_(*J*vUdMTOV$YkR2I$-bNmLx5fe*;:eq7=SXQ&/ucq[,)42->3k+J^SE$JK2,5KTEG-Ii*od)HPnbtBH0d$:N^_39t%T&"
    "pc<Z%W+.fl67ESfcd#W<i7qf@8-?-Occ(&X+H;(-k1J%D:CgGDBN#^f?$?Ns>@OH=+ZQ'_0B)m,#tZ19$(6&LWosbsiQZd)/mHT%g4;WTJ'$Gs(0u^$wFIaAF,:Es,aIK/2EHdtb*c$c"
    "7o4GaF_u[]>tKM%rLxPc3XaB7,wm87fG8c,7,$t$RDKJG.5s49oh'-OU$]2-d7QC<g_XwN2EBF2?A<77IZoC<c5QqP4YmF2mO.RnvkoZU2E/H%Ul^KHgS$jYU(&oXCYgDEgFO^@3_nAF"
    "ZZvl,I8hrU5?&H%vQrZ_cL]5E5@V)E6%Qd,M5b<W*Ja89]3nd)kW[C&dvH*IsVt)RXFmS&F3RZsK_O[FRx]X#qUkUl?s.Y#0Fi]tSJ,/4Q&[kA*IiX-Tp5-LE5Q&i3S4/)Sj&3*>84e)"
    "ah4U%=ZOZgA'od)3iQ0uS):7Br,.3D=1+h,c=W/1)ilKs@LRO&//YO&1A[H%&M_:>gNh[,.;wS@#Cb1ZZKafRXARZUpWIS&5oSP''&hG`>AZr,,(P(%oC[&@-EsBoO>7-%Q3YC-*`:i'"
    "F.l2g;eUDsLp&Y$>Kr39$Cm]tCQ<X$6H53^3DO2-?K5%L8G*j,rTu-[2Yo/:?E-mDQ,]4KO'T0HP0:ase-d=&o*9HV*'od)3Nv-CFk'>4UY.oN65uxtgcXeL4J52PEQvj3vXCa$kh#tC"
    "puvIDZ9kCK)4=Q&H^/^tWDJjg-hH.(L8C=6S]###r2,t3Z=dh0[S#ajB=3#-(Ag]t^kAct`$L6A:f%SUwaow+''7?gs/Ng&a1QC33?;<%Sr%Lb0W7r6xr.-)g#GUmA]%=H:bKwsZb,t8"
    ")<<T'2e(Je;mn52L[ZU7xi1`3W)Gx@BvAp_r5@;7n0E0VNRA@+Cd)?mGj/$-8,0#uv:Mh)`d?b4AcA.CPF$w,%pMU%0Sjh$37u(J4R]vAP$7]>,jxn,9W-Y`uQbFsW7p^sss]`5nW%o8"
    "KtZ*G&2:^t1$Ivs9aosH,UoanV:t-CAq_P`G/da?b8g%pNP<1%:d+bg`I]86<;NO>LXot5Xan]t_hu>>VSYX,Y&(Q&Zd:%XxYtIuZssU$b1/[s)WS?jGI7.UmDf]tFCYYs)*ta$vxpO/"
    "kF^J3hI#(E,iC0Cj0iC*?1OLsQ[-9I0.Mxd8?fe&mrGD,/-]KWJ?gQ&6mCUqfoNca$vQI2/W7r6d62Kh0erU$X6[@%TeLwt.AdJsS)2Q&dn7+<E#UI;_b%+2-Ddm6)xV'@6F;a4ZS@Q&"
    "WEWI`.$&FcASw4$`n8_e[5ONntx*T>x4EYd)fMghOGl3]q@:xhZ[IA#rI_PgNMwstL5j8&J-3^t^r;Q&Rk>@`Tsb<Ef]^udi$DKAcsXa+Y]Af3OVnd)R<j-orE,pnvZDPn1HE_,0JqB>"
    "*;^P'[P+Fsl(#n$*.Znt3g9mdYqTP'n+3+38q1'U+1afVBQ5TV%Og)I.+bD%`JZV_n/-1KWlY0h:o#%FBYA.CBKDV#3rC0$6U7LTZt[LT.TN**0*r/1`UjYs(8Q10X%.Q&Soflq,)[hB"
    "J8i=I]X;tD8.K^&wP6ctqXXhUeGw]b*Rp;-Lu7T%dp?ELvfH.(q@@ukaKL^sfm%Es(h7:-UxqP&WEWI`akcORY6f>cSM94'<km^TujA'K,$N#K.3)ZTisnS&/cs-C@hC5`H2da?a/K`o"
    "lXe3o;(Q10GT@9$3H2Os1#(Ab0VmP&SRN/.j;[tdBKs94Kjx5P(;oe$cN$x+b/T/`CfMYsmX1DsudIf$GOMGlW3HKOn>*g$:BTEI#@BZsixWX5-_%T&LZuf%14Q&i;C5OHG8Uk`s,<^t"
    "Io-1TKI5^twG6vs6jJKCwfon@oL#g+PY0BX5YGFs.a(,);VSQ&#'ZS&>_WUa*_bEs_nX6u#kL9%HgPOOY=LClB)&N'*DFsPKT>Gl*DXk$<;pgMZVtE^p/0pLOqsuN_%@A-<17`%8&'@O"
    "N#a-_;?v9Zjk5HfsWu5$D<09I9cgiq()`C&4%=Qsc%-D)f>q?b>Kf_*2*TZs2Sud)pn'E%r.4.M@c#Z&ojZQ&xe?@kSLWaEVpOZGDBOhg:6hUuUNVQ&1rALB7,kGD;x)Q&5W^.;n0QDs"
    "n`-Q&&an'h7B<h&eMd-IK7qh&2<tIN@hdXhNd4^&+TSq$dK6%F#cBH3aiDYYi#X_3<jV#LRv7'd,gag&67Xp%wJs-C9GP?#gH@>#C-BP8^djL#)mt$#p^GJ(Zp&M#=4m##KMOV-1xL$#"
    "[u'##rP^%OdOj)#2P###cnV]+0rC$#?x###9?uu#eUs)#+;###YXt7IDChJ#:m/##[b9SI.g7-#%)###jE4;--c:?#`-%##id?S@,Z%-#3S###oMSiB)GY##H>'##wv$GV45o-#A)'##"
    "tNW%Xhi>3#Yr'##T*@cV.f1$#?&0##g]*87-a.-#/G###gRc.Uwnr4#nX(##%%)>PA'+&#C4?>#&/AYGB4C/#a.(##NS[rQ5m@-#iF(##@E8MB(AP##3ZA>#=5e%FB/@8#'.)##HTUrZ"
    "DA[8#+@;##Nvm4]FMn8#LPC&#3JDigISw8#SgE##Ev#PfURTU#KQN>#)?vLglccC)d:($&NTE>#@NV.ho5s<#P^E>#d#SrQ*MuY#RjaY#XsnFi-Yl##UvjY#^5kCj1x_Z#dF_Y#c>0`j"
    "=-^6&eI_Y#sPK%kD?b]#fL_Y#?>[(WW_^:#J=$##,7?5/Zq#;#[HW$#?#d]4]'6;#b9I##GY[V6$+85#d3%##hn<87'=S5#hE7##n9TP8)If5#s&x##SG*)<,Oo5#$*A##qod4]&3DG#"
    "/XiY#17a1^8u:s'1[`Y#DFAi^<]XI#4e`Y#(;ZxOKkQg#5h`Y#[kX+`Mq?0#8tiY#a0U(aQ93h#^4_Y#f9qCa^D1C&_7_Y#vK6`aj.iG%lR(##jt[fLox.Mbm%Q3#?0aY#?Hj=c(@]P#"
    "A6aY#G6Juc-[x5#iU_Y#s+k4SsUM4#$4`Y#2xGiTEDeS#mU(##?^t7euwQ0#oV+##&hIfLUK<1#KG'##tB2MKVWN1#OS'##I%%GrUE31#Wx^Y#V3@>#`CS=%A:bfLhvS%#1bFoLT]h2#"
    "CX`@'*Vb]4-+Ql]2d5Vm;'YJ(pIelJ]Ls(W8>r4ox=_;$e3E202?vlAmKuxXM69Ab9iW>5*X0s?9'aVQ/&95]gonoeo'^,))WlG2n3:5fL+SPo5Fs5&Q<`586CC&O.l;)a.X@g:Y#gJ_"
    "Wlv>>0%$asG[=dVX_1BOX'$w5sO%'=p>-6JvG]p[=aB^kJa-h(ePpg:kA23Kr<>EWgP.<d^9m,r/D8$5R?NQ]#v@$5:O6'Oqm9<mS<@q7ah)bEw.0-rWDNe)c#k^tB;*1(nk'XdE*&49"
    "Ta&@P[u@4'tr,1:0,*7Ju0*IiRHP7&*q67A#qm$Y/(xU%VKE78X)>=QVU#4p3t6:eWla[c^i^COtfE`433LV%#CZ:e11T(b+7Jl^a'i%Ys[f`=D6^xuPG*T&Hf_V[[/qM:e(x8&HKM#?"
    "l]C#QMS3#mW+Oj1-6:aF]smp&(dRsI7tS/iG%cd3?EIgVOqO?$WlcmBU7@W[RB6t%Fw,*=Cp8EX,uL-*>I.-NnE%6p3uaK;qjPN_,%%I*nqI0D5@gTSD4h^cxZ;e3>2a?HpMf*42qxKD"
    "2V._PQ*sWer>7=%IO4O:9<<tnfnu-3Am-FFF&3bkUDrt.7BUeE*T.L`hdU@$Xp@C>[x<CGcrp*X1b?[mPm>%-'=SOCiwjt[#0mC#-?=(5b?WOLAP+.ad'TbtfA]C>Pm<1V>d47p[*Y4:"
    "u#kOLK1UFbs/.A$HrMu7FQY1Dd%dFXN%WIs@YVu7X#xOUAHW=nk:FV/g^Jxd%02)#59AJ3^*<`c@oAGFsYoV&A'`S0rlb`>Q?MVS1=Lckh]VA-KgUMD5dIP_@h8g*3=jVA6TVS^J0Bg*"
    "mj6m:AM)jMNY]P_$kcctGD6a5jR,aG5w9#[S#0#nvOXv.2SuD>Y?d>Ri12soA,-E5(wNsJYO6gae#LK*&N<N;Cle,OCW6Z[]Dg>nSZGTB%l.HXRaGaYOH[)lgs'k)4A,-=YwTaP+j_ve"
    "3e`Q1j@gdFGDFQ_pm'b54KB9Kg'*-b+l'[.O09w@l8fWS9i86h^?4R(@exWAc*(eO>r&qg]t`t&#kLt8B,?qK6AnWf[qit&@'LqB*>_6ULwhKjo/LO)IIIb>Y(3RLc(g9^`'&u&&xPeF"
    "@^,XoKjOO2r.FODCr=et]Muk2$S01E1g2OrBb6x.RGot]ngdnhfVN(-L:`I=osi_Q%FRO`L>x*ua<gRLx*`R_sigbl%B:S(ZEg@@+m=xR^B`Oi#6(8(I+`.=s#jCQ8T,fbK#&ist40PD"
    "#-L(d6mAS:0Sx7C&9a(Qml<`ZGpBiaHuNM*2<RY86:?MEqt]oLDV,AREd0MavDdP);t[cG*[Wl`#B6p(:-b`HQ70>fvAkP;P#AA[enrA%]xtV9.9wfO2NLGcJ*W#/JEfGGh>@,YwD=2j"
    "4j$9(IjrA7X1X5M++K;p_^U#A>149(5tl5Mjvc2W[1oDdB:jDm/[nd#fW@E-UcKW9E=(9CGAKdP-YBaZhn9^eVQLvoA#2w&+Avs0wjbg=XKENEZ*BTUAk`v]#'s8hvk$HuG3qg4KIusB"
    "8v=EQ'#]gX$*&9hZWCZoE/Dw&6iM91wWxj<W6[QD?TRNN=_rv]:M$0kS-3b-sv>*?kD#hFtv^TUS,qm`?2pph+U;qC9[UwA-Aeh=^a;@AH9N=Gaq%Da@u6DaG]'FsSTkat.oT97?6Zpk"
    "7/LC)k*18*tXwH`ElSnJnM;T%sLG9&A]3e`/+3Vjj4e_3/qY^4qhn<Z(0KbEg_+9&KAovgSLln@$d/CI?W6[s@L[nJ+tLtk3NwH`t`/UdI`SnJT9r'E`Y=78h.t5-ODR,Mt`mG,oi&8$"
    "%bkN4qC#xLo8'vLL?wX5/s((**=(JeVc?e6ftQ'AY6]$0$/p3]ej*B4.EM=-#1_YV>YnCsT02DsiRJwsB.)2gpWHL,if]L,4,5)L74pDs$=Jsh/<*`s6N+H,P5U3=9^59o[33-)ff)I,"
    "`Kd,=6BT8ob&SN'Ed(5/vB1ns5Jp63L]/-)B)2-)hxNT7Y66dEDfcYN=I;m7h+57^ik(2X;bgrt]BZ6or<0/Gw=W<TMOA7RmlehL=T#oL42WJsp/e_:AVhQV%T6T%f+c[j,XC7$CL/[s"
    "oj$<&%s;#%rDUbViCPHp?:fe(5Zok963BHUO5%+M9cgiq[%t_4+`/f)3nmJ,83TL'.29-:L3lfE%xRFIgMm<:2u2`t^.e;td_[6/n3wP,Mx0XMTiNEI[)-i4IVXhK<I8[sfd*[tlJl4Z"
    "&`xf@>wHLgT*R<%-Oc.e7a*Q2:5<Q/iQfe(C<wG3Mg0]IQmWJh'<i%:ARXwA?XEGN.W2qkPGRH%c:d@&U.AHN@Uf+MJ#S0qL*pt@VjGf_Dg_QlJCu%Lo>IfV[2PW1[ACnorZWm)7DL2Q"
    "M5q92Bj_763p[-ue)tZ%u8vDMW`Zt.4'3bjQI)@01n0*6sO6]/epRT.rA(S,rs:t_vx;H,]pWxb=VsRixQ^wK*<0`torI?#U'?7$FBP@>Z6lg(U&q*M_aDaId&[RA)/U7%5M_B,FA$d`"
    "3mmn@B+d<*L=v0TFVZ,2XduetN<5+%9ewj2EoEv5hX8*urB9Es4fC]YoK-Ds]=G:&r`3JV&qh(sQe.Hsvm(,%B=B.Cg2f]tU-bTsN?lf$ts`=,Vfs`N4`>FsZ#aw$L.$oRFN:+M0?oiL"
    "Ml+wL'oTtY8H6:&h`mQWAGLrs/djk8Swel9ufBqs[VJtu2tFi_a*btP=ljVhK2[[sMTq^m5ceA*;Opfq38^R%d;UR^gVVYQAQ&e_)PA>r%&pl8mUFZ,dHj]k7($FsCm,bsYvl<9MPg29"
    "INda,,9e`s/ZJX>T[F3fes`nL%&iqBD?:`t'QVNTC3o0gQ:'lm0R8,LY7;8cw+U0G+uL8$ZT&t$pM6d)^-PI,+A9U)>*p-U#+GT@cfrBj;SEVh?Y8N0o5/Fs:0k_hO`GUH$d29uU6[@%"
    "uT?3OSl'lm*`ZC)esG<*=>xH`>g)*M4`kbh#d6sL'?N'sP:E[XN8hvGlh<C3<4@nnr'kl0x#pBc.r_6nC<_>t<xC`X676)MkjD8*<[IAcfd)EF%iqFs&+(V%;[LLBdH]I(``PC<#/^]t"
    "9>wW$_b-o@)GT]tOqp-k=dB,%;]aDsltfXs7W0ocevI;QVADEMxAf?tAp8LTR)nx>52:h058F8$Qu@q)oY<uL$;5_hZwuWh)'t$MFbZk%c>mt>Z,S3gP%KEsL?-ttOn#uYUmf]k.l#Fs"
    "UUYJ]L**#?AZQ6D2t5u>%0evt7__bh>]fA40$1[s8XM`slLJ=-/L`e1cNRI(M%4Y?g21cFqCgEs7bso8B1.YDRkc6%hc>RI(wWfka5.G&s*=%Nm(_Ms@)^)a@m@Vkila7$(4wW1lQc-I"
    "M@'U#1th7$*[rU$=E8I,5#[[sx4hulJj_9&Nil.V:_1DsRcm-LO[/a3))#^t@7rhTHcJs6dw)S%F69d2F@$Ku'B9Es5O3X%6M`8$gnaI2h#&M,7@N5$`p2[tG/8PsBoMw$c>%uY_ob]t"
    "d=ki$gw@UH/0gOJUFwrmjUELsvhu+%r'ie_-F4N'A/rFs:>2Dss_$D,W]r7[SYJ`si(x>&Khdvs@`ZucR`E4m8d/^ti4o]s[4;7%Iu$P^U&wCsW+`at]H:F&VA-t.A`un%D8(=10'+u#"
    "$J0%,tpmIsF5YAU$/58SFaGpV3XBdH5npE%TZ+&]DQ1eOw?Y9B-P@,-8?h3ff;E/PuukIVM3SHsk9<:M.6dG^DCfk,?Nqne1b%RO8'=P'70MA#X>j7$kZ@LhfWnU$s*/uG,p:>;vV94&"
    "/Rfpt&<7N'X<nvsev:`s:UM`s`e^$uu0n$=Ba9;mEWgZs@Rnk&XXh=,lKEF5mfLihXH9&V>S_u+P.%&7gXd/:[(-L<]xtith6cesV_M[%ckZZsb8=LK,@qk/ioeCsO`lg)8JF2pBV#&-"
    ">Oj%?A]%SBrAYe,uudD<AxCLsf%57.FY:1'`w7i7tvMG%F>5'VWihY+nh5wd)S(V$)f'PDl*Da$F3[jZQ3?\?apPGJM(PH&@W0rdMOcZX,:FVEIYf,Ej;Rj2*P3:KgiC_jtENJI_7Y=p7"
    "pv-Y,cbeL'YMa77u81N'Ca%b/VR:@=@e$(<UsQ/N(#Y$uMxO.u;+Ie$u[8SX_]SAaIQJML^#kc>wW1ON10h2d93S^+g:mLoJAww+;j_1'@*$`@7*$+%_KRh$iY-I?\?_FZsMK3IUMT'pl"
    "el/8$r)?q)c4v1oH)%%+?p_1'fYGK=lP:LP7.[Fk#n7Hs9Yg=#l1T58oP$]4Cxl_t`ohGsq<@=h&W1wg5X%jg$jn6$r?2FslZ-W$P52gtgDJC*2nm]tws?W$u/74h3f.ZsO^j?c*aq[s"
    "(6CBmDjT$b3aCdDf&w[s*[`IbZO8;RTkgGh%k&Yj=eNrfY1^:-T)B#6vs#qG40m,)jnQM=e$k_t5bEIsMLqQ$5/<<jp:?)j%<O/pIc#a,0=nc=lhwFsgP;no9-#agb5fMgV:9rlORma,"
    "GjUZ@$VMsJgSX^g9,H?N<-CXYxJ&Fs(Hfvt>/N/2`hdk%w_n:5q?2%aj=#m,IXngtix2btcW2&+oYEDsg'_X,w3k*M$_X?K)3QxlQ/V'IMXsYs+v;kgMo)[s0OctHDHr-r9(mFs>3@BU"
    "Vl';+3*h=,.0bRL?(LftLW9g*WmMZaPM[f9g<-Y#xZCh;:eBk?Rs5e8D/9A.kul<46+vq$`'qoXrXYit;uix)[0`I>spT#>.lXkD#ED6G<5#SSTW^)^qH&V/5hC%*>TCBhpd&HM';w7b"
    "1sR_<7gmPAH^k::Kbh3]Bj,Ysrs5Ud%)nxccwjYsiv?IC9LuseUBUJ<a6/*%*]nF)(w3@=^_UDsxYGFs4iaLsq.i_t4ora,1?c:$2denCj-$o,nI3q97v#-JMO#f+wK>dtN-Ydju%Si["
    "UfS1-MjX`Qsl4s+6*bWsr]Rh%eRI+;i<<%=v<jF2=pI=P$+II1s@;OJh08*%2tVnRJF$:_KAWO%,E]ag`OhE?RXj9%3E/=9X29N,Hg^PsJ9#8[[rJ`s62[t)%DFH7suEO8/]8)tJn,1K"
    "/o4AupnbU%5:#Y5C=-rsXgx9Z?_q3gOZZ?%:@a+L^PAfh$s*DsfAPXQjj'pL<3DhLi#X$3EL`Gs$9f4t*+[R8EV-ntaXg*%O4seU9h3`tk7qs.'a-(*fbh*.qpNO/7/9F.q>m?$sHi]l"
    "68d*NHf74&uYQ^tm7>tQ.[_XY-7TP'=BknIP93+hHNFes=J5UZ<lqxLWOpnLRHB$l(W]B)'2UxN1t9baYjse-PnbhK5kJI(Gg2BMl;[bsEEipL6PG=tFeLH-bQit-Dqc9M^w3+NT3DhL"
    "Zb%Y-.t/R<_+r/)up/3*gU*d*au1j(f=t+j;)LKV-a-+MvP6=lZ]sjMZE[[++D#=(=T_b.94c[+h8#Fb%&-C#:bT+$iA`[+VvP^tB+?GaFOvGauHuJ,V,f--L-e--2Vl$F1MJLBViq/)"
    "Djm-boUDO+nRDO+YJ4<Mf']u-f]&kLSj&+iLv((*#'8hPHMfCswA_k)J*JvGN&3jBd787e5AKB%oCu$*bZN?&nfEQs-d4OI.f)E4wO8FW<xEGs]jBb.?PZw0=j6r?Y<B(M6-B`t9V<IU"
    "Gno292P$[sBV-C)-U$ktB%ge(OtDxW0ZHi$HCAa?sOVZ@de25T@H7#avGsi,8$^ct-g@:JX]3c)pD'di4#:[,f67&*P;h3=VtoBJ4l:?hM]RNi[@-%#b@=IC7M?8%W:V8dAeQFs9kgst"
    "3k3L:(.df,<0:5fek-[i+]6eAd^TE%NMSftpK<`tgI&pto$j[%lV+Y,iCq@k)%J[sXcP:?*QWSpVqm3%LQh28q'UU-tZVDsH(tt5,6)t,m@Pd3>dQctGPQ^tQbbGs:x1Ds?W/;/OK4Js"
    "?*U(MsR?:$S@Bu59X^(42'8s7Xth=#E0M=/scH[s3_koLHfQmYR886$v7Fu,)]2*tn4_rtx<^:%rO=R*K.IqfuM<=Pn(^Vu,/pKsFSFR*UUIP,.`2U2RXJU20wZtPq^<0brn^lT7DlD]"
    "m4XFsIG3s4Xoj7DvlobMK;sjE..d>%ZbcL=8(as@fG+o,H0.C87-v20VW&H;pD[CLmZ9^=TX@P;IV;>1md(aaRU_3-lw<#ce$Io,eP,VB$aO['v%kC[xALa5+tlDI=S1Xl*,U.1p)0Qt"
    "+R($%r#u.(b7obMd0oVs[F_R%3rS`t&,JY-QNZk+gb4lA&n*lA.RH]tY+[D,);?*LuhEPs0K3Oa1h.P,CD.4QW-LraPwfqH4Jf0qq$jb)tF#wL^Xq&tj)L_/UKxpX,:BZsJj,%#%g2-)"
    "AGc?-b62X-HPEL5n)c<-w6gk*sGWH&M*wBt@i/D*8tsj0S3%FsTC9CsV7C1^6u2mf)uiF2*KlwfG,ZhA3p23jIwxHi0=#s)lTUK@.*aG%/#ZsX&n%W+cT]qt97NJ;[;V3V[$PU+v>J]h"
    "+tq/Glq&qb1-e2<%@u_t7.uq053]s^<3?F%*tudFD3?I+rZU1sE*kiIuNQA]]$r@>1eA^?4UpUEL_L`$/dYfJ]E74&+199-swxMsH8mGG&L@f,D<>-Qr%%7U*E'=DjL1i_(UGpXJb<CS"
    ">9]ed]d^nHUB3J+D6kO&w0B[OsJ%/hO-nkt0/&<tgh+>uSp7T%IR5stSxf=#/Jh=#1&>`2H`eP'LC/-dw'n-UWoZS#XsdX#^XWjfc'L1LxO<LsX()GsQH=itu4S`tsiUP/aX@`tQ0&Ps"
    "(`]Hs^VOP$M_Hu#Ib@F2;]6I$t):C&P9^b2vU$S%g3FG<q%J`tk^3T@PHjrru/ug[NvKA>`+tBI2i#h+[I)Qc1WZ>cP4mI]>o8XTbSri$gBf&[M`W,<e55^t)=<2rcThC*0WtVsfZTPZ"
    "XF9P]<`0agljJ3$T8#Cal%.VmJ3bd2#]*//gomB&4OXEe]BGN$Q$`ZjrFZDj>9w4n3Zo`,&D52;plCJDR%ctZw2v]$s@&,lP`V>U9TvU[i&4e,j$vZ])Jh;$LKsMT-mC--Kx,9MP$aP'"
    "b^<2_=fmd`DQXWslu0DsSq[i)7.(G_28,amG2SDsq1,+)B*@o$>QbttW;HF2'mxYc<vU]t=0v9ZhoWdt%HG+%*(BY$sX9/%3*HNsA6Ngb&1._aViDYs2N,%#6GB4/*[177mRBS_f'-6D"
    "-Qv4^*BjCTDV=q+N'[f>RLP?%Sa%x8cITt<)O-QdlA>`T?Nd,)4;k=Up`-YZr3IL0`wk,)i3>dGF6cYT6B/H%*H06bZl]ICen7Xs09Qi^RD0WfrBTMs'A$Wc<8<DssuNd07NZdtB(A>c"
    "6i=GaEXl[]-*_M%`OxPcG]fK==rrA=t%/oLbN0_,7ZGYs*$Soem0/;g@uXmsOeTGhm2HN8>_d986kx2h:$/;eBeH6`=Lc>M-I@@@-VjB@F`Q^t]]GFstbdrsLw[qHM0f'kQH5+^KNaDs"
    "<G8Af'_Y`Q7*R293%;l/rsUkH?eW2Cq./^bC3V?>,<%2tL=Q&tHIi>u32nw'25CUHBBq7[</8Ps<d?x=#q#G&pT8x=a7#ftN?t)s>u?LB<-v%d[gKs(,VuC)(NR$tgeT2%11'b$Rf;rt"
    "25UUs&t7:-=1vcaBsJkLhtEG<Np&^F]-V]t=)?K)C^dLBWrdt.xf9o._**:2mJXe-N/sdM9>'K#fcS.$VFU$k;3djsjRSx$0#l7%I[U_<7@6vPmtSetC<qdMO<Y5Q`MGustL#x4XcHC="
    "UIct6:WuCs;&2Ds6j*FsRmDYf)<VeCkZrgtg5.:?g44dM4;*vs/TAXYB^exL(%m_f03cR3++Mh)02in32<hct^>Eu.w#@_WN=D[0t-_U697/%4h+6^nXS2qt(kbIs#s,v?w:S`td:V9."
    "X)[_3#A]`tMii'&52S]$oE4Xs4UaDqtYnCsm$3I)6o;/5Yw6xF7R7csEWsM,DU6as$NLvY3W%ht368b-:bL<h<Vq-$[bdTiMVh%MhxRiLmTp$5R>rfLNSZk_aECtgC72q$3x3sto-vYs"
    "pKpw4]jst$stxP8V?gZ$UtEKs=gNdttd>x4%]st$R^<qMd_vJ#=+&&$CF=q)I0BLpg9I8RVZx*%1-4#u?Ecf$N@qdM0B=Zm>7?=54VGDjGbg*%$;?_/O#Xa$601lL5?rdMNS[X:0/J8u"
    "M=m`sM)%&*]IhD==.E_&N<R`$E''1p3D,sQVZx*%8B]YmWnEh>-K:;mp$VD=3gdGs<M;[$/U=kLRxeOA0PPlLv0i$%'86VsaA[@#t*Rw#$IKbM8h3`tsJ3LsuH`ZsV.(7@dIw<moj*5-"
    "XNs=EUS0.E`bX`F0vAGAe2rdM)MtssA3@nn>H2OsAHs-$<(>#%x*^eqt*.m/oRV$*M0>[XswhP/dfJDsQ8mnt#r#Z$+fEp%bq+wfMc?uir;g2u@S$r-4%8N'd>cls4O&+;=6$rs%o+u#"
    "3bG/te.G#%x?1q6Zg?)Ng;3R[dDo]$8FrTp/]:$u5gP^t@SS;gO<_T#;/A7$NZWm)B)mN[u$h(t+x@3gVRJ2D6K$Aue`s9%;fYFs%6i$OnI$6k]AW7$q;XR%6fsF^a-<>QqAIob3dcFs"
    "t#6[sgFui9=laS7vuA<IdnxoniYW0..Sui9XZA]t(_/Jsn0CQ$R(mPcx)Z>cpo/iesR8.WFhb`@>b]Fs*h7[@HOA,-hBE0[-^K.]QNaY?EW_9CC8:Esf-+Xs?eMeU2XHVs:-*Ds55>I:"
    "q@Gbs$T)=GW+pq-H16L#1A>r-QXQ=GWw@T%kVELsLhAEqY'7jWJoiA7@Wx^+`_/c]P[e0^(ZWA%TM=SYj]0RdtV73-6+8N'%P4?gIu<T#MriZs_t1DsF`Uqn;kEnZHc3j$SBH[AR]'/F"
    "4Xdl^1S>FsltwdmmxoC+>Mlcc8BT@@]#9`t8`T(IEqK%J0C[LU9t7ZUM1#F%<f$9paC2(<UlVOGaK9E%se6(5ZZk0T_IP7XOBUasl4GPsQA$ua5d&(Lp(v%j_>iBi]%H9gS413tR&hc)"
    "(N+&M-8ga#@jsh2^[Iu#?tP^;FF5V-.i>bsk*VAkrb3ILF&monW/?HLPjp(ucg3N0&PhUH$jP,2pW)&t*fIi)SSk;a:3;gL0W@9ab^YUbUpxc,>NRPDE<#Gs>gkj)2E+]:4bbvKwlrmf"
    "?V'K#(bLh^JP1FVuE)Fs<(lIs:YDo,/;FDs(-94&bR%eN7kZSF>7>f+=X]:/(aG(3JnLO/ZST4n3cC`tmLpn)ZvK$rdR#gqn_9rl.nH^,:D])=1J^P'I^Qvs'7Nf_w[A6$8Ef7%h`u]k"
    "3pD6Yd%^s*lgS0FVTx`s6Q0[M:i.e@%[%`F81c65KbjK9S1<l&]m6at7%nL0s,41':6n;%2Q:%*@#Q3[sAwj,3wDj9qSk@ij.B$LU/X;flTln7Mu*ol/L/,LnPOkgY?E3-jU$T7k?TM3"
    "$D.tfRb-Gsmv_OSpH-EfXDnfqT)b>sB[@M@PcGVe'pBjA)RFo%LFO?.WcZLK79%wt=l7'-6GP^t_'nX#T9?Ag`uEICp>.]+:]Hs$?dJ*g#E%Vs//iq6@_:n$^,;&XUcADsge?Ag7IEt$"
    "d3d;99)ZitSQX7.Ik^^$UN2gCx%T]t2f=0L:x;FD`j:PcmX49uoSoP]q6w%iO<9%T[`%GscW=%FV#hgKuAu;ajFErTFEw)-C;C5%)@^%KkaAN+Bv9f=8TE<%ibl[g[ghT#*;YO&)=d*j"
    "8Y2V5kNVDs*GAbh0uTIrNiGu,Q6lKPPEfE%A(rWEZ)C`_YSN&;M#9d%8P@>%7O0h5$h9V.TNYX,?X=4SaI4g?GeJu<C#]$;p8caMd_PDs,tju)^++E6E2C[O8'DD[A$l_t,YC%*'B&&h"
    "CijXGl)+@Cn]>P'h5GPs'elMfl1,K#AK`7$+u54/G9R19pG'<f:`ODsGMYn$<QR77%s@)Nai$Ks+%aw$>CCFi3N'2:1+[atLi.T%N'lDkT8-N'D'ovs/HxTnkPuYsAFCblleX*rGe*1%"
    "_m$I&;wLJSB8_Y+JM]]%JljuH(`q`t9``GsoR$3%u]owF8'H3sZn0pe:F9FWden7F[RnC%A>uU:b,b]t4S)x)2?@wtwfGA5`4c.%c)<E_alMJD>W2NEORe(%3'aXh&iCPV3Rd%dG%$3%"
    "w&;0TBu-sj[gQNH(-4Q;tk^ts,GgSVR6`0+4J_Fd7ZU1WtlBq$*8Fk4Xb3BJ#]<cNsW4CMf5w64WJC%Gb/,^t&XZ=5o3.Vmo[VjtUI#3%j9TTNsViQO6(R7%Je)6Oa;OD=1fF/P:9c)R"
    "T,]n[hOadtleuq$Rxd>imMZY?`=O2BB;1AiW;xjOMN[:fh#.194cP$*bl:,%E4x1d08`DsN%lOA.7q@LTAPf%_J)stcHkHrdl<(S8A$5K;rUxK]LmacnrX@SD].`%exa,)_EoA=@@Z87"
    "+i7N'n6GPs+flMfh$)K#$/)Oo2=K[OI9YhKJF>Vo)r4IsXJteLtv`]tlEJe$Z(B-6JU.kL$MQntNxTeLkg`]tFe*FsRbSDsGinh$t6,Fs3LjrNXAd-Lu:EPs8:pHe*>:')2uU[=Gumdg"
    "c8C<Lk)/Fs+*_JqZZY,)O>'#[E%Vt+DdDta2]d5W+HsoJ/2ZZ]YAMS+boI0Zj4q?KOud4cKN)oDw2_u6.<h<A>9>r=_WJd$PM2vL%MZgFmg=)Hn%if,U)ta$h:FMd'Unk&I9Cl&/.u#%"
    "KehCuM&JwsvSQhK6ZYJ]Xjxr@G)N3E;8:EsXY/7.6m/aMU0sW[s`a%t/^J5%i;8e$U.&stlQ$FouJ1N7hnWhKe:I#>LSX]tax3Ls0nMw$x+2t-C*YoLI?qx$gA/e$CJL5%GQU]t(8*(*"
    "92w5/?+J5%nMSe$q#a]ta3pf)Q/FLsL`Pj$qH,gLiWGtlmcph:dDDtLQVOjLX.sG<dCY]t^dOr)4:7h$)4`rtb<9a$f`-N'[#N@g]g_T#PkE`Mq5&9$n32FsF_A=#0CF@+T[G.6IGDa3"
    "a7^c,W<;CE?Qh'u@6.o%fWXAbQ%L%-CS7[s&E?@++xm=#cL$KuL3P?Gk3LMep/SSc,V+K#O;QbMiYD=,(Ii-i(uqPdloxJ<93,LsB98GN^<9`@,.u_tON^ct+v=Sf'MKoe@$MqX),c]t"
    "Uh$r7)]96-3x8^`@2@#aL^:`t4R7`?=nxFsT;*m)w&g5:p(&WV>=h-gVG+K#<92DsQK1%*%dTkJELkipLFuQsa:8ICa'?(%PD:[bX[_]t[0El$ROwR5_/1S9ekt<ktpGf?_0RRTtvIUH"
    "ZbYnf5TKi:djVDs?^%dMOn_h9al.9utnbtGn^oR,.N5`tWs4@F][YwL6Ua`>*f#go=8rT#IxI[s=;2Ds1)wl)6pqlo-x(]W4*5Q62_.3Pff9lTqb5xTqCC^2$_EM=$)t_tNo.88cYC&l"
    "^LL7%;.0Y%/uJ5G<:F`t?i>x4Qhv3S6h<4Sq@Tnfinb2B$l'G?Rhx;O0mZ6i<k,U#'ccO&.C*<6olBOT%[rbRI=Sm+H_@F2a(R*-Bm4F%)SqSaS;eQVlWM,^s>^4`ck0lTYZ1$%gX;)N"
    "->vk+.P_`t:aTb%((1st2S=/CCv?[$*hvd)uoNDs?9H[s:c),%QFbi$PBM<g.R[O&w;LB%.Nvdt_MJ0PT9(;[>I&N'heQZsmM:l/$w(U#vAQbMw*q8Ijda$l4hluacB?l$Fp9&I&KBA?"
    "\?gshQmdh0-HY%stjllt5tT++)rwpVQ9BBtVog:>58R,68'K0+>^Fl`$?1x^U;n'b_%<4mNjf1--l_IKsm.;u5ab4f(wviGMXUNMFpPp%k5;_[,3*sth[p-U#uP2DsxI3T%^IvPO9o__*"
    "fr[f^Eosj$$gGC?#ENqM4V%.-r$F>`L#7qUrWM,^uJ,5`hn0lTar5X$BB@UMK8w_t4uwO&::)c;@JEDs/`Rn&A@qnf)itZ,<.1'uuUYBZesRZ++jd-I<fBmm3mJhTw<Onebuo3ggJ[?%"
    "F,H%4Pq#QSHr+cDlqGC%(ad-IBJ>go]1aT#mj?7$^nH_tRbvk$kKNmdhh9PpPZ,gLp&uDJ@V%SsrR*iT/SMD%gF94<_AJRnNb[5%&6xt9J::>9'/FDsRnvk$s6M=hEb$K#J(S[sX82Ds"
    "7^cq?\?l8Dlm%/77((`]tfX<bt?ig=#S7jYcJYwYcdcGe4mw2N0'o_?%U(rG290xP'VEDE/w8rJg_<o^MA>Gkm+E)Ost.Ast48q)-`k:EsLE6ttOn#uY(AD89Y%l-$A7f_k2Vb`sKcft>"
    "%8d;-^6Dt%HTiYV=Y3vghQN')i7h5dHHd%<goeCs$_q3g;*;EiJ6dBtMF_;6UlE)1gTtK9$7mK9Cmx=uf`gS.bFSM%$G#CjZ1t//W+pO<AUT6:PHo`tuCGB$<eb,)lZ4wDUF[C%$nV[8"
    "K,t&%NGwQb<Yuq$AFM;p4[FW-iRJws%X7-u8G%1TxrNL,Z^s?kvwqk9[A+kOrxrI&+.lw0T?KC)vSAM[pUc.uuKo^&<r-DOJBD@jargRj:#gX#;j=C,_t'FO>F)B,p*O2e<%:uFsp=qL"
    "B*<UQ?[.r.C3ou-t7YDM83KQ,o>cNsJFXk+0>lqQ$/ex-@-A;M7%X(r4E'uIRVfvFsK2$#F=LtutTnFs09OjLnlfCsi-q(*lfGn]IQ/-)&TjdY/Z&BLn[v5LoI@T%/`Ise+tNC)(?DhG"
    "i>nq6Vq*s@4LoY6Yw<s@@Uj9`]*BC3tY3F.rP'i8B=kcs#X,9MZp'19**B?.`_c%V)@X?6)^&FsRZh@.cU[9MR'u%N;h^X#git;.`qLPGF0%?$ai:5MSBg@uQEKS8<CEXsgidc%jl8Ce"
    "20De`;&($*xkg/%*<xYeU$Kh;c$If,RGof_H[4id3xd&CW84#/@2U69*`C/)$i#<QHjTq_+/6o*W1WrB%;o`6-rXtt$v@q)_%?J=s)iqBIk<JVD.QpXvdU97cn2O,$0.IhkrsQN&lUqQ"
    "JI,qreWAmLC'U`7vvC8]UTaU-X?xj1X0Dh)`CtctVpac$_-+SNrVtwt[D#ckluYpk`pPV>h_###3BeCs:S3/)/pJKML%(*uMx-A-*3=%8>K;8]gP?x/#aAk^1S2Ud)=9W$kwA112**Ds"
    "(rHk4wQjLuH3kr7]L=K3`w>+D5+x4ttB'KM13l1ucY2E-A?iP-%KiP-`I12R3R3RsUv2/)oVxXu;#4;7o.wXs4vEtL,t6&c'N94'O6Ros*Hb,3x]>PM6_wruFv,O-0rD'N=(pBL.l&mg"
    "Ls5:I/Cm0)l]^,I`^eh+Euo3KlSE]$0cSEuXSTC-.0NM-D_59%/+_-Zt^l6eljU0DwgrO/k_0as4;LQ7B8RGs?`1'8=(ed+gp3xFY[vJt%#XEe?$Ab.ntXa#BVpo1*LH(u,0q*Mb]m2p"
    "'WSlLEQHG,'$`:&9v(gs+2Rd/,KoFsQee<LiRUZHQObDs4Z7v3RxJP^e4(vHSxJP^&?-dNdOu%Mt5%Ns7qmSp+r8$R]=v##MO/N-wGvD-gMxG(X`J<&BiCd`r0EC3k3FRAXc-1K(kCC*"
    "R02=&lt=9&ijn9[NR2n8O[D&)<=XQsSrKC)uxr(9jTMs@tKonnE#VYZ]AE)F:VU_/:_(uLXrTPsnMJ_/P7mqQ*7_q@$=miPq0kh'gVeU0oUpIpYJ7FEWE#u8-IQv?W8vtuM8D>ZKO`]."
    "wq;[YJ@v`-tQm92dw7<le9IVssI=j)dg;=,U=_'/mXHb.=X[-iqqqHdoaBC3*.KG0@f#@kLoYtPJ]wIdk%REda<PGsjf1?MXGnu8in;GsL74oOT4pq-)'_-33AjrN#]NOsPJ<X?@H_Ms"
    "Wki+P86-itbiuq-v4alsJ*UEIP*UHsmO]Tj-e>FMAA6S,3eB@1Yi#I%>=$Z>K`nE?RbOU%'Ge*d1rv_dkrJP%)HRlmtXTK=CT<A%ctl?8HBI<&JOpDs[%G$*kpVX,kl=nE7S$%M`N*la"
    "+fZ[MLk_&V(@in8W6NGsv'.rQw<&HsDXEd`S],Um(&$@Yr>;hBN@W4##%s%CU4J$Uh1_1UoIV,Z0WG8LA;Cs-h:%+MYLQ@LJ=FjC3#Q@LsI:>mfEtN.3l@JJVP>m8fdMs@+*oC<H`L<-"
    "-81X%A+xSquiAhh2Lj/%enmqQVK>6#j/OtZf%%#%uP=th>i$%M3fXw8jTMs@=wXLp*Mfp8&:xXsH*gBS=50(c;4FRA5#s(9hwMGsu)xfLOEJ>MQ^]:L3Y<LcuBFd`r.DP-v?dq8eYX]Y"
    "Rl1<-7^Du$S7R1TmRs#ZQ)/-)8dTwY[%W..Il,&M?Y$msB2QEjHcx6[Tt#<&qn[R,5pcp%&(MQ8O3S`t1n:m&1u4u5wbb9#'jOtZCR)Y$Nx9-MH2mpsRLF@laUMC)<<5't0+2/))U2E-"
    "MM[e->2WtLA(3UcChP,c#[R$k9tnC,6ig-->Zj--v^RtZ>E>&YdOu%Mh/uOscDKC)#oVhB:=s?Y6'2/)9Ap:&IrC)lJ+WS,_HY[s+$p3]Xo+u#BTV<&HnrC&.'Qx6PIx0B#:$bsmU>92"
    "@@*<>_HZ<&BSKh,)Y[IpLSH@+^D2pfq/3Z4mU=RgRCVQlciqDsf4Pd1%Pg>a]F=R*59D.C&@C5g2kGf_'(N>flx5rsSHU4/[#wX5EH-k.#w?--papUNV,7KLi>%/dq<.<+'0-a<Pd0r:"
    "<R]]eirJP%(sJJoi9%f>^+L2[aCU4</bAP'V/wo]]ed=0Iq'd.)sp2@t%SqM:)[*)DoUcqT_t<YP'3u->v`'Pj3u:&t4>^b<+$*RkIEXE?fRQK#@,9&]A`U65x5s%J)Lnd)U4-)faR.*"
    "dUnQeEa/661&/h;0LL19CPL8G.`sd+F[ZlLW[BGNwX:/)2o]3ZHEwxP$NI:0/0r#gHGvYs@TK6sLEhPl.]c'tBlY.<G;nuTGce_tb.SQGiikvD/oh[YE'YLedb-9T7v+4OrJdAA51u_t"
    "?dT`t[)3$mU:DGsb+t]s>ala5C%A+k:$Ml$ck1S.3oQCdWB-N%(w1SlD0H71ich/1m_8`,DK/DOf$'(r(31OltrE=,V+J0uL+W8&F+O2eW]r[N4PesLTep9ZTwe8.CYFU$mFfQg$uk?k"
    "?V*:&qUW6uCA1@-V8'40Lq'd.,,$n?Ukot(J$C*Mck(Ap^d/LlS<8C<Kk<9.,W4P&#Rixl-Zpoo5qlwB$%3u-l&7Au7Yb?/:dg4g?eI&MpwKdq)eLbNYI=r6[ib>HX6GLMd0PK.9h+=n"
    "Rp'dWkC_mL&8vq$I'dk+Je.BdN[OGspf1?MRV<;9ng^/)Pt.=Ude5Pf*>X;R)YA:M-ka(tR=c'8p=tLB6GX/UAB50b[1*ZscAww48mupk9L_lmNpH$tRcUh_Lq;bS_[Xj$qlFsLZo+ZK"
    "VKFrk*x-4-27ZATcV5u]W9Bc?u+Xl0Ha<8-#)<p[)1CE4/[@`t:WT`tx@c=ndfur[87V>U])ue$OI#8CB.h:^rN>`t4#lr)l[5Sdu6QXb@E&2Ll.]I,o?Ig_Wo,Um/`CH&TkK;&AZhDu"
    "Z3q4.)l,&M(TZms>c0hLYD;R[x[:mL[@3Gu8Snr7EL4q]Ya'fKRvdIC=g0D)-Ow>%Y6hIt]@1/)#UZI%klckUnhk=,90cPlpvge(mpAD,'CRX:s%qFs8-h9DC=[?\?mGWGsOZ7v3`BGwN"
    "`1XedcNOh,D^Ktl0v%+/I85/)E5N3XB_p>Gg9hYsc0EI:=)VQlZDj7$@%tu)]&qO/;lst,oKg%s.G7=5ej*V-/(c;9(?2cBQa(u`M)qRo,E8%-Kf64]1xh3]-v+.<.go(>TfdP'U%rFs"
    ")o+Ae^f1E&V6qCCiPtB)Jn.IU?[.r.c&TMuHA8([lo?Ysf1cmA'->2N,Pt0tCPnFs3XgP6Dw:@%e$33SaBtscNj3@$KLTq<S]###vI9V-$*te-&qE3SaH99d44iiU`+#gLRJ:Mu&RGYH"
    "Le*9.j6Y*j3lf)3LcQ+6[OW3pU.N'kSkV3pJuL>9De*#HOR*'mx*B[sx%d.QWntGqh+dOn:KOC)LAmr<BsY>6,gYlpDNR`kT4[lp`3GGsrBi*.U'SWl>NY]YQ]IGsk;m(/`na8.hQ?NI"
    "pJ<d2Ua'19a@NrtJpC89/i'8uV;O+HA>>rNfnY*iR)2[s][eb*J%VX.jrPD2BE+Fj@gPW-Y(R4E0^eQKuFRH%rDUbVb;ZmO`@H@kOw/N,a?a$G0YZ=>:;pHs+GR&*.qh<d@8YDM)5A#O"
    "tU8b@=84O+RSpqQ@lQ>dc-A;MnNkh';GV>pb$s@pZ/u(9?=H@d:]VxFt&WwtCvS+_/b7k/ON;Rst<nq$U?o*`IT2/)*sV-HU4IA&q%[F4ojYGsGO'i82^QGsObGTM(cJEdxOn$N1W]p]"
    "2#-t---Q*N`]QSfV+9UZ%&a-6#i&f(]A*#.Cw9PMGVjpB,lMp^/*riR2=IC)^1NLUC)(f$Ih:pCTT<PK5+nVsUhD$u+j$qR<roAXk&,Ums)KC)FR?FsfQ3/)<2/-)Hp$%tw4g_#E9,Zs"
    "P?b.iubt5pd,Kwkd%R<-B4.M.kwk7*GG#WIK64(NGZpA&$6Igiu,O.1U%gkL^f6HjaKJUH;j=C,ifAu,-f-F%K.<4&RnstH:YF`40glH5s.BqD''1C&9cF7$9XZ2NhhQ^t@?*'/xk%D&"
    "A7nX1^kwiLas:Y(.M/auBJBY%,ZMF&$%PPsl8E$l1`mE)LO$_::I1/)tY&$fa_:>6;u*XCJU@.*#i=+D8rmt>R(Z20H9=atq6(]$fS>.6qDp,m^&$N'ZFLc$:AHA&peWf/p3GI22PF19"
    "OAL>d/PgHQ8BVP'^uS@g;+1UDs/2F)1c+F[FO=LsBSe>KeRW>.oj#3fCo_#?SN;Rs4nNF.OD[#*>Yj&PokAF;Q-$.Mb1+cq;ho<cD4<D&,0]VMm^T_Mnl&E#332Ds'UF:7n:ofqWT>N%"
    "X9@Dsu4%.q>2#d$;5v>W],YF%Sr%Lb(A3N'9ai;m]Gc[jegxwQ3jAdsACgsLI-(]t)gC%*vlY0h*CFR-IdOZ-Fva'/%cY*iW^DQ0<YMkon_%%#/<NvOmdLF&&hN$T2ZIKg[,#O-8MuK."
    "PWvpe`lF>He3GGsIrPt:2w+UmnLCH&+oclL8?mU?lpRb.cCdb.v.#kE1A#FsrC8[sm7mEj<>LC)`<r-*xvOVe_Fw0)Cos-tP'WP'^SS;g,/it8Yv#`Mo)w]sjZWm)qoG3[ttKcseOfe("
    "IDCv6VE&cG76d,)Ep-Q1N4$S%Z;3J,]V.>%qS<tS6NM^shjP(MR3=VRV484oO&SiLRKO2eX`i@N>7ktLVn5UZhaf8.CYFU$JlSp]ZdtIKH@49Mv;]v9jQ_k+J$C*MXeu@phJ5MlTES_<"
    "EJ0hL^ErK,wfViLLRtcq7eLbNAWZ)lbU0Ps^A=Z/TCVQlBm;Oqt%))X&?8HM4p'19vB<>.B$;Ya=]L>?$TG9.tJ0fULsOAcl$Db.^0-OfQQv50^P&t:]25w3XhPL,de5Pf##=vQ3@G;M"
    "sr(m9w=mw-s$pG,hjl[sIG8Djc<RgL=(sC&#fUa,cWbVRL#fHu4Bdp@wEcXX?4vvHX=HJl$jL's9opGMGY;O`tPuB86MMB)k-K.t@r(V$EMqHiVJHn]`VqHiVhZ*7x,G*g2/wMMHw'w-"
    ":k788p@8HsVje;&Fm,PgAhF[9f;djor5S>-jV+<%&kLU6O3fn<+HGn<pjFn<4)Do<tpBC3?S_[-TD5E5DJfCsW9FgLr;)pL0.o$=)F?gLMfqEs&hfT.RQNlth;L<.x0b%ffAqJMA@t9M"
    "Ml$##8QeCsXHJx=ftr$#>C&%#5GXsM8-&8#EFncWxOee-ISi6Nt^l6e1<TQKU<Er$9jAdsf(h)&P8p58+1S`t_Jbn-nt$:iOYJ8:mnt?0?.q.M0,)qsWkt@lvQ/D)P[w0:fuv--igeQc"
    "ImBp.n,UU..pV<SmD,2:vt-BlBB1PIf9qDssEZa&0InFsFM-Zs_s__3b[2h$,.cPl<qUlcr0pO9g<NGsfeAX:JXCbtlEA#*ni^EgL):u]D<T(@df)tof#ed3axDJ2(oKHFMaWJhi[MW3"
    ";s(m&h$>+hwO#2-[U,FYPk/w+8$Nof]Gc[jrDqDsTV>92sZ)?<r==7AUMvCktLgXs8BpWjotigV?V*:&.Al0u8=B`4O3PF&Rw.()L&lHj'UKF)qTFB,ts<,fP1b%ZDCX#cBiCC3q8nn/"
    "l2/N,PZ[&*74NP,nK9R*h(Y*i<]`=-500k$Ofb_sI+Tj04]A.C#%G8f&#l&)Dn[[4ar0C)RQNlt'tuB&$_PEr2o<in4IMm`5J5k,'&Ln5&#6fbWd/4+papUN,C%(<Y[MP,=ih[iXswc<"
    "(CEs-J2:tMZnk4qw.OeQu?56e:N;RsIa%v46$2g1=(Y^fMG[[=NvNVfd>KDj^[p&j*-VvG6VH_sHOX*Co,nk,,wvV?0.2t`a@_<W^g5OKOaLDsc#WutBLdY68@*^,[LMdkGaLws$gmK^"
    "n0>,_ANt+N[Lp%O`4IF+c-n0k>+8YMgV&UbecBuBhfJkEutNQVak@A>9ATu]J4%u+d*wPdFeO9@Kc.T%rHcWe`>-ZsZ6Xxk7H1Qllvw.*Ox9RRZJ-ct'H^pTMXf'bXQfiLal6;B;Pdf;"
    "0cLDscZUZs52k<6B:vqRuVqJsq6irt*6fHs@V4`*,+a'@1,?Y#m3dXPj=eh9(rb4gVVpDj^XIFgC+_h9QbvHsd9;2g?DOR.UwiGDU-je,KqvttNXw?ebn&W?mY=o,#lP^tf#qDs$_bEs"
    "v?+^r[CdU$(TTB&)aJ/uck9>&&8ni0/nObDX,c[jV>&8$R;;9.%s;#%,-.U%QMi;mW)5[jep==R4qPms=T>92;p+#uB`3/)6$TP'N^r5eZh5=F(kHOjQ%p6[i_%T.JEl#u%Bf>-OP+Z-"
    "Yj?b7$`Y*iT?Z50Oq'd.KSxYgZ)J.->Eegq3n@<IY@$6I+7BPsO<n:&a]64uAgF?-x]c</OvGlg6aSgL=jl`qQ:`*aT:=r6]+lDsAl(mLsjg_M4GAJ.e+#1ir-5Hs:Q'i8LL6KuJJ&/."
    "vMk%MWh]b24Gs--#jU?7Rr<LB9Zp?gxL7=>.88'P_;)Kj%iFkfhKEnLK2k4u^jAj-br0I-ex8UZqocd0TJ<X?^LgPsA4q1N$5kgtbiuq-osmAIL06IsHO]TjQ0FRIZ0-x5HXi20N<Bx="
    "6B_'>MfnE?Gk%hCi7rWsPF[w0Q9lXs^C;Sj.cKQs`%G$*Z4NL'R=[0>_ijTM4-sn%6*5[jeYel@'usOsXS3/)8axZ(Q@$VTX/?6uLx6A-V$I]-t]9e?MO@Rj@dBqMmW4o%'U*V$/0'c."
    "T5x=Zk%_1&e^2p/p/,T.-D8XY][-t.@^@MCo]u?g1sadhFDicNof/gLu?X8tVFu?-ER;A.e+#1io#3p/#,'gL6.T`t]j@h>O&pr7r4h<LjA3WSY]-x-#p0AP-hH=.>.b%f_@tGE&ElqL"
    "k8=H&T^r<uv3OU.:)P0P->1QObNdVFM[M#uUL6asTlva$ev/uGDQue2s$=fEUvgC&qYY+u*jqDsar'd.vx7st,I=8Ni3kh't#7L,JKb8mH#KHsgo#E&2?ZUds8+C//5wiC4FR6'G[G<&"
    "*i`Audkch1`Lj;m5_bps;9M%_Uoa(NX>I3um0QgL]=JrLx(0Y,=#hV@[gwYHUC04+]p=O+@>u3Fn-W4F;o<r?B_>atS/spq+*Ps(K6vat<O*1:r78r6hd_7%o9q'8-R&5pnbNGNTfRGs"
    "H./_Mg:9ICvBNu.,^mn/h;PI+1bLDNt@hAX@Dk(u-mNi]7,Qq5hf>JAWA7:&MJRG&s#v;ZnE1%OdU%5pB9qGshgC%*@:r0,0L_UM4cA+Mt,]rsY@/-)wJN1u)Bou-Feg-MSe$YqjuGpJ"
    "P'3u-B.[;@i?Qh,U`f<&lBq%IaRb]svC6bsfP/024:P]std4r)^,f<&RUmA&19p<comV*i?W+r.B-fu-J=fVM7+K[s&4ou-'^D'MW@7Rt=CPk4`,w:62SEGsdbTk+qF;Wl4I-FsTtau)"
    "Ye7C<];k*.K)W=Y,]#pSb<&v0Gq5IMX,=UIcXoq6Bp,OOG#^0MD`Ckfk?[mL0Sje1=T`k+_2:Dd>IZPTStO(.PRQo@Z>WVn(IEIHV1R&MOMJKsBck;.'MHk?N5W2:6bGe6TFG.CBYm]1"
    "LmlvsvT7`sFU()W1QH%>baC+XB&(f$JnCpC'k0`tNo$`$,Ken7nQB%>hEnn9Ur[F&kTkhK(//-)fpkQuvV#93:h5asRN(%*K+0dtaVKd$_?FSN8h`XM(1>3f0CgBS;3,UmSE57&-r7CW"
    "[kJ(=_S9j(>X#%.gGLXMeExTcZlmm/e<LC,dl?_W7EIM.vb0%t+Y#pSo6XG%J1+TqbQ7M,w;M:6j%/-)@X&)8;CFD8&^^U$9w$Un2M:lS&'?Gs-r7R2Ls&A)X+1Nt2th;mv<te2dxeQ-"
    ".0]/2/.CqsT*kLsCV[$t>dkj))Np;-UBSb%`(k,k4R4dtO/IA-KZqpMLDEZs]_jF0oe.2)[`*Xt9@Y9`KxUl/+EGN2[tubD)F^Gs'.qFsw92dMiV:@MZ&NU60'F_$EmDm9x4mAMq+6$c"
    "v.>W%S[7(PrTdgd]o`3F:tJtlSE57&'9OA&(cf9&fL=LG*aLA#3s[7$-@*%G972DOo<oT%+?CC39nA=>;f:C,q.1f:.2$%Gt-msh''VE_KT'lmW@bH&kOBL,GVuKu6tOfMYUNuNAvC'Y"
    "WJ-%+?R':&oX&Ru1DXr.<CEXs-pIh5T1b05Wfx5$bFI.(<[Ii@E[%d*eZ#7/4W[.(%'>-?*7EKr`dW+^x3F9^DXOl*RY6=-8-uu$X/;CJJx%Tt^@1/)?`f.:5sH5ggNChL+GRFsZ^&Hs"
    "Gfj0,6+qUZY7;8c=-gC&PdCOs%ov(M0c]bM%'^stYb_s)m8/jKS1h#?8_o#>v>^$0QVDC3#nB@4Ww.WnZf*`NWKm3+PPC&)V2%Ps]<qEs,djR%A1V7RQCD_A(oY*i->n+sf=ZVnCZU'A"
    "`L0p]9+=%NtI^VM.pP]sq1MMMT@T%s<0]SJHAc;-Ap[n&O4T=Yrnp(k,D_Vn(Wv^fl6s^f>&T_E^Qq3+Q?;r6]QTZsK_O[FDJNN2/q5IMo3>r6xr.-)LO;OsOi62M*n,<Mo_Y'as'a8."
    "si=P/0MEGs=ql34;`P@d:>4&G#CjW-R`hwTde5PfiwiPKmrf*.NkIE5;p._swHF_Jd@EXs[Bka%s*nuk29Z5$q@K=#Avte$Kq1TCVh&MhC_;OSo;mstXcVOsuvc77TN5G,G5-TnG#^2r"
    "D0eQ8o`Kv-<mPtUt^l6emGW.E&LGI)xhqkSbE`S49j)OH?E[p_x_ROt^a&Q8KB.&cH)?^?s#@Q1_LH(u5geb*4BMK,c%9O+KKHb@igeQc%NDHs$HLI),beUHV;8b*3V]h1MX)Q/^js@&"
    "krb$OdOu%MDlp@%Ta%-=/.+`u$T2PqJu5FM`Mxvhq8;xsZ27>ct[C_tIUJ]tLq[nL=]ta$Ovx6IVkQGs0%Ketodc;/5xg#E8ppxh27cFs<wSbt(P#.ujRxE`N5B_t;9Q]sBTJos6mk]t"
    "HPvat<65<mcU?`bVn'lmPw2lZ6n>QshECs)GON[=KKdhLHh;:4c5?p/:Ht>c<c]*i?7?p/CnnY.WtN]u+w9s.cR2?b:@8_8g4?Nq.oZC)kLxd:uWnFsD&Ue-@=?C8v</T.B6<DsFJSb-"
    "qje<CX8b_8:S;bVH3sbh8aGYs:4+Po&dAwk4)0;*hdm&g5jsn%Z)#^t'$Bg1-+U8uw.o<licf]t1lfhL194wt-^%0q$'a3_bRgC&**ETs2:rC)xMg@MBF[6oZo*hYI->E&HmrC&uJ>C3"
    "ALnC)jd_VtSb1/)PBI@ux`WE,N+O2eXns<cj4Xk+1A17R?hPgL*<,Au'`08/JjeVs.P$6Mc+UKMFVClm]q6O4<Aw*)D&0lk<@`b3s8J?&s(WQu]vor/mP-h(;4ZU$H6QD-.8u(.e(YwL"
    "$u'iBs&63f-_IFgH4?Nq*O&=Qp+LWAWinNsR(`MMo.sC&ej$f3XOmA>lHw2MgELiui,d11:M,&)[JUCMu`&Hs.9]Z&-/HUHdI+Hs#`JwcdaI+;%j5#dak@5M.4'PuF*7E.n*ge135?.3"
    "3hJucP1d(%22jG;WIXb?Bu=x>FUS=%hwc;u#D-N':FcXXa_.Um5W9X1oR?Fs_5h5f;iPt:_wmshd57U#Kmx?b)3;ehNg6J,+J=GO7]DC3%XT[sQ[S@63Qscs0R/02*^1bt10#lLii?Ys"
    "Pq6J,61>s/SD#D&__@C3(m;i-&WSb.LX:o7t]91,bw$MqT/4lLlel'E?J.T%Y)ShtpxC`Xn8IC<v_nukv*fE)'Y`c::I1/)D%=ve6`us-QFFqG==$Zn].BrMvSh<l?wGLLTw7>-,Qq12"
    "-9on/5<tnI9eAnfh0j_a87,atHUp`hnWQh,C^1L>36LL>/h]AMsdeXhu6Y^sbSJ;1LO*1:7d&I_F,Cut-JUV%6w*uhC[$K#@HM`s'_B_tRQ;)%00dl$Be(f:N<KwkYD-S%R]P/:%@0j$"
    "x/ue(7v2-C_GYWn`bPFsjqaC)/#*RMcEEZsHf2i-0FIh,pjgC&)*NTs8s;L,Zg+4SB2Y0hoKR4S_mn+Nr3B[s4.kb)N(KV-kB8J.@JIueK'>mB^.:xLp>^Ss0uah]Z-Y2;`5dCWMXLd2"
    "IQPZs2:JA#.4tErB`ak8E3QqP5YmF2?Ya77&-U+)bUP1%A;:_he^VN54*kF%nfa=?OFZv$B[el90Bc)4rLhom4K?/DZv5KM?-N=-uxZW8Z)CXsGrRk+w8ak8a.@2hGh9,MX:[tN<^3G_"
    ".OM?MqBFg$tbTcuTIUl1vmk?cPt?Wl,ETD)$^ga8g+`Pg1Le#M,.3P,8uaGslxHF2@N-T-0FAZ$8f%(3nw]*RZxDC3.a_*@jDa_NX2Y0hQK'D3sjGGs8Wn-66$1Ys=0J>-<DhT&*MW@N"
    "xLOWsAn'<.dJR,TAOTdsQJmj:]25w3&Nn'8.gY&M'sZYs;@Sk+NZi0tpExBAp9Xl&x-pO9?WH/;c#+)+snXng*U'lmd[n1]kB6asOB/68@Go'8'rd$M7Ugb29OMB)Ii*=tLt(V$Z`i,)"
    "qM2/)doZ6NpQ2/)7Ir#l8=3#-@P/dupMQG,;;9uLpB:@4e+RDM7o6bsr@`5/RO@Mb;%DxLh^qX%OMZJMAZLx$fNTkJoQU[BiYQE,ve0[BjcdE,GvW^BF?%gL?fqEs@QBp.+0IQ.k<-XL"
    "mI*w7-(<I-H#hT.<#7stZ8L<.l0b%fnh_Ga(XR[MFG),)[0B@4os7G>-&NU6`E2E/JqME,_XOfL'hcCL`9m?ct_?-.sk,&M[<lssT6PD):wM[tmv/-).RE`3tx]%YJZ]C%Kh'UqCqND)"
    "QdCl/q#GUm6NOeDji4dM.R1is1vX$9mEJvJ.:TZ.>ul6[C(AB.d^qOH2fscsDT(m3kX;pJdOu%Ml9fVsRNUi]G8@2H5QO7%EDwcs*_-_;MO7W-ErD_/>9:hp2R>Mt8Jfst@TnSsO-HFi"
    "n=@ko;*u>%3=2vC5'@h+#ZBo%Wo;.Lofm*MU;LX>)pAA]b,ec[UUWot_8B_tfGkOBqjk,2[_db2;CewKE/dUZql?[A8%DP'k3rC&pud[sh-TD)d8hFtSb1/)$_R%upa[#Y+8-<-ur.>-"
    "UaOg$/Wh86'4l;&NPs4qMmqi(u+%dq<L5IMR)Tk)J=t6JA1-u#GnOR,#3Psdo0EC3KKnn/pTo0>RcuKukh_+au*qH&,9:BQgj5,acx,C,bB50baOavsB(Wrdxq;>fW;2DsN4Ne),Iie>"
    "n+N4Bg2I,a1*Cvo3QR&4[QuB&%;duQ8cqh'IB+(dY6al,2CR4BY+F^,wrd+a0RojLEc,B,eh^cuE'kF.<cW_hhMuj(OYCcqT:D4@_r#@Hh,.C,p*]aCC[?.g<u6'M0#*OsLF(x+?$m2t"
    "O43m);,UOh?;Xi,PKYe:fJ'&kUEsC<N><Ds_3@(<#d@s6X[gb_M(+)F=#$.8e`b+=EsvZDGe6a$cTiVF55[[+BUf_*eU<SVDRCPee7+U#6-i7$+Vd6n8oa=5w(@X(CG?+8Qq2uGFQgKs"
    "uhdukm0=N'HG(2tXimvN)Un0:Gps?m?R':&qD.Of/q(K,=ZT>&TL.Y/Y,btP8to;IKiVU%,.&+2Ad,W-B-cLO^<h^>u$SCuP;owsO@gt>f[V#DD/cA%.N4U:DW3&%dVoDssuWbb7jAds"
    "64;m$5('Q=1a7I,sODwlX[r<c(v@Y-Q529^*qdXP1h^o/#XNvlb8[*ivu.>-.)Nb$S$Pqtf1fu-@5V*Ml2F$q%iink8Ic$OT^YN0f7aR0OU5/)$;QJ9Subatxdlh0']%EaUvKI(HQ7Dk"
    "9%e8.N67%FoEW>?4gG@.kw<`huY[YQ<7nW-OH8LGl(7=>N.,m^58irLg@D7%6lHs-/kjOMiV9Rf3k&dWHgIxL[)dq?PVb;6b.gL9'pRstbiuq-%PZ9I5#[[ssW=4&9UX=t`x7Edk_2/["
    "iP[l,3v2g:CJ%.>m;73HN?_Z,B[Qt&7oFwUojbum'4fZs)Rfb2g1i&n6`8I,k_*%+iRc[0P>xn%GWsC)2H'#=xwjYdZo=0M@ToZX$79xNmZ/lk^g.+2Kh%+ix*2.MKo7ukMXpBS+dkr7"
    "2**vQ@:jf%]%)oRcEm1Ule_q7n@KvHIjD_shp+H=;t@Y-.9j)>$4pst0toO3hopFsFW*HM:[%v&SC'Y5Jms'/2_Q5HL/PK.k7RcTF,a?TaLO[Xt%u_$k1arttN`C,MCoH,nXi,Vm:gCE"
    "LUZwb;f:C,A_Y77CDVI2C-SY-+0)1,La*J&s+N:6_6ou-MD./N]r#u#>*Lh5>x?WlJ#KHs89L9&Si(oRwLLC/..tuZ%:?0;GWD<&`UTcu^$JY-16l3+#.7Ssc#Ibk8-0+2dt@>dF&@E."
    ">JBI&<,eGs`MwstC%JY-Q?B_Jgx<tL#^%D,#oflqT+oi(oXhH1I)/Ss]T7f$[PqVs2(1L5`L=HsVnskk>@`b3Fx:(S;u`oL6uI/MXioKsg^$F@hB&FR3?ge1R1r*7,2-&)h&:%Nu`&Hs"
    "3j+d&tPQI(qB*i`'F#G&cF)Um%NpC)G[ZbsZUq)Em#1_JY=2DOg#JUQERSBos?ppf0)Nr6b[4R,qd#4+?awabe'$?tX?/-)RvtQu..o:.mP-h(lsj`bi@qKGjs,F,#m.bO2Ts>&GH=$I"
    "NDHEC1C,oLo><jjWR/o@gi@=%rAqYP1xG0ZT'H(kCek?V@Nd,)+d@?&2?q,)U?BLEvogp_Ha$LgoMTB%o4@S,8,=O]HQNbsYYe]t=T$W$_Xm[FxfW`sq+[$uo4#.:fFH5ST2v)M0iYne"
    "[Zbb)4bfok_oZqLkt=*t?q@PuA1anetKGCsa;/oeYax`t'u,+M;Jkps3^XbDBe;_tP.b$O(A2&bZgx*%q*iehh7f#%e-No%^ok]tfIwS[eWNA%bXY]O9gp%ixVVxsW57>cLA8pr[;gb8"
    "a#Q:n>dL?s?WCC*a+c^,jdxS8&wmf,2HI#gD-0H%*x6EqWQRFsoSNU65Z)iB3dQctY@33hN[^d,09f=%1jVG&VVpDjlkB7$C$][=?Nd8u^1N:uG#`[=4ms=#B1r=D_n7IghiP^tx9*e^"
    ",t)UGWq_Rs5Sa-hwI5=5nVR'jep91TnG[I&5=<E,XbN@SKPE$5j]ph'BiBc`w5iAf55UR%g5i:&&K5D,]Ig%5x<qDsm4==&g^nSssoS(tP6(oIjdnFsxc'[/$K[<X6#6U5E&ZIWHdTS,"
    "Gb_OJQ(iWsAR%r$1%qsNmj`[%',uEiIB4hq+b;no,S-1FPUr0a*Q(GsH04hq#T4o7+hSp?X@X2-n7WSX;^2UXI8clK#*X(I+lugLDtI?XF;wC:KW/WIWWTfMs1WDO&neM.OFs*0GsP))"
    "nCZmOBc5I84:QPE)2rfM*gcML$;',.nn+lNx[L'^ns=-mvqv?^wp=$*]//E%XF%U.9JlF&#9XN.@.?SSQlG8J)6SQ-raIQ-/lem-Z&N<h8SGe-.cc6XQ*3<:'ac*VQ#I(IK:t*Tg],B)"
    "t;]$N*o;K,jAmkoP32/)J0N_5]%CtMo[EPsj4H=#6xo5]_b^&#N-8_Sb-:k4CRB*#./T@F_;C[-95dt17`@u1si(Q,)TGU2VfMU26R4U2?*lU2v[GQssGXk%(.>UVwphWs?IgEIP@KK,"
    "MSd3gUdDL,*1D%F#M&CAQdd'A[0s'/L'9XNJjDZQ^a?xcA<>,tnnbZ-ROb$04d3/)@%Xv1oXPNY'./B&k;7U[J=>.85j&eZWA3-)V;e]P@CqTVD.)F.3m]k]4_pS8V%dAPiWxKYSlvWs"
    "Dr4G-GE_w-q<j7O7Le5Le8*.hKwrP'aqMQ'lq?P,_?.2.h;GhL5GQ6u2h5:H,.tuZCC7Q';?#68I;/HsnJY0IE&u3`j<3f1/=SYs-D^$YL?.jMGPu^]NnS:BO32/)5Xma$s]$u1xg=XN"
    ",xp7^fx;nMH#Q*t(^FkL5Xhw-k3xS8^JYIsq5GPs<R6$J&Pt+b4CC48**7URtshhL?>eIsK<oFs1qpDs1HLE&MPfL,0EE%tx@[@&Ejfg=[dH>QdM>s-u`TiL@Cj'8,OHJVT^`B#GbdWs"
    "n%pDs1k$]-lvMh5nXFksUM)8__B_FJ49HF%rWhc?=5_3Y*a=mD=*x*]bD&]l+9)vax3wAR@Qm,)#7/D,P=-UN2-fiLo+epMTOEmL6muwPf:nTM5,u7MolgFIMh34Zi&s']G@Q']SbH']"
    "lTI']R[?']42/tLePE$#)BrFs.6GPslqqn.S7]`55OlWe=G03tf,3<:vGb1#s.cF;O32/)@wDb$$+2]VOG++)B(c%trhtj)/NH4JqcLctEB>+%7K?ctj=D$:Z]?0u$JM4%3n_+TJ.pCs"
    "aiV%$U`G#*MeObM_Vx1aU]BL#IV'N?V'((&]RT;aO]6L,XtTj$_A%Db%Yl?&l%nqLbFAdsPkL`sfFEm)7gD.CS*-rlZD7PO*TqxtDS^eL+JOsPKT>Gl*Jbk$FO0)tpDScM#.odHWHNw9"
    "+ZgB&o%jqQ9a$I,M=p/1N6LT#,3)6$M:5[tirldfa-1dM,LbFeLFmvt3M)qtBs2k8,[9ce_/jh'SDGO-gvvt.u`.6$EOEU2EKlh'j>42P6=)etS_R;@DP_[-(ZrKP>UV]t5W;gLsG]^N"
    "bh>'*Kk4st]Q=kM1M=NeJ@I;@(d7at,lQgN;B;&MqB>0Nd8m5Mi*Ydtj@)v:UT@qgUcL;@&^7atjH+IO`]uuLl'B3ME6a-IJ)IT#$VM3b=gv-LFZl'EF95vsw$?G]2H2Os5c44AFGHL,"
    "xP-qLdqRrsg5T4A*1CL,w6]?T'Vf057_gE&5Bb7/EagMs40XiLo6Fr.4I,6$tGVn*aoNm9dQUqZ.MWcsM[L@-1eVqL;dA+M5IA#MvH%fMi-+V.bmr$:0PD6M4@=_0C#*6$1Ox-LfT8+M"
    "xVT,.kgW8O0;]Y-th0L,]-14AVTFnE]`*H&hAiqQI9uH,uu8xOwHi?&WH#Bb52g?&q]jwO-X2885g=j).2`?`HJ=;^[kZ&N$:t.%bLBoNGcL+4A5v-C7o'fD9I]`tUBO0YKx'2t:Ki6&"
    "3LAH8w6J`tias/<W&41^(/5f%XaUPe:5HXfh7+6$gR4kHTIUDs/`b[+::077_Br_t=@$3%/JPGsZHVgt,=cC38/wx>EF65TLOXcsXH]CP7=]CPMo+S0%iJT#=s`6$s:1mPJF&0%(#)pd"
    "3eJI(n3j:$b(,mfrVn(5=sD'-0Z8qir/bDb2-iqQbvC+EPAIctp7`N;Z#44e<PESfUeI/1#Zl^tdr)LsFQIk]TBYT%EB9Es'1husU)=]kB:O_fND*6$.$JC<irldfYtK)Nx4qRaZ:128"
    "M=V.(PF%`thUY7.I-xO&*$JHsN,t2n-OU3a1Zuw7oxFC+0*NO/%CVb;'_QR.kiMU6K;cJiNpwu?o2m@gW<:7%B4YREf67UD9YX#$In6REJX<7%nG+2Pob3b$acYv6i;k_twkn`tG4*C6"
    "0YrGss%RKs<Lm3/v_(F?w6vha,$sA+PBxO&)*tF.8nq+rQ1-B4R9rkm8pGb^a$>=:O9dPfb3;nsP:?JDSvn<u&Yj<uG@(IVj-rdMTCFT#IT6UZLj'+tpYRgLe&*+rF:#Yq-X]tsHcjuL"
    "HoS+M$'s6pEj2DsR`mg#w+3H,GMsqcDN&t:cj$esPvUG#.$BC3JZb*ti+uX(&h>X(G.sssqJKk=>6+O,5'nqQ^*A>-k-D9.(5Pd1x,'Wnee,10%_MnEiX.r.YM72hn(K'JW7*o/LE9vd"
    ">sQL0B`1hLo)4Gq-T`uLO>-u<sD.*tbLrCs*2N`s<Se>KgS/ctA,uCs;b1Dse/Bn)YZ_pjU<tSI,#)f$rWUV%hbY5$COBJ,qW#G&8h*UmTOYnAeiN&tV$7]t@7F2$JT#f(8ZTasm#:;?"
    "dDj<u_kJtu5GX7.;0@9$HBpS%(^R[s,2sL'Df8;ZWe3F,UVI[s]Th5M;%u%tec+x's%+gL)n2nskme6<nNMO8MjOIsPjp.t%fe--qJ;x'>0LkosA)m0)n6]tesv4$.J>p.n*h4hWEWI`"
    "C@f>cASw4$s(:m/4$B=>dM7Md3+9*-k3VasV&UV?JOIo<ljknIrcYUr8wsnI3,p;//9GVQ5qW*C3U*(5$X#d`1PO2-RA50beg$6$(&?M(W4`MMsU8r6HCV:6a(pR.`wEB,^#Wbtq&B/:"
    "3$u?kk^I+25XL`sNAtB,e@$gL3*A:.OifA=0pW)kCWDEcgCxae%8_,3vJ^oL'obqcU5d*7WC<^M74:G&:>@E6&rMgAP:qR'9TRO&g5s=&4afs7hgGf`t$chLC*'PB%=>^t&m&EsDBHOs"
    "@9KY$g4$.M=tv2t*aNiUl*8E,AZo%Mtgr;&.[5tLwm-IsN,?/tHs(V$U`&Osv:ne(XtSot'g;p@8[0kLJ:Y6dYCVF5iOx6ex=en7MpS9tfDq6*C6()rW&7]tmG2Kp<Rr(thuMm]C]6E,"
    "T+$)L6]V:6gC-%#mWc#RuQg$1f<V3t6YS(tjQOdM&5aG&a*V^?JJ?e8cL0/`S)831ta_?#Jc^U$()n_:$u=S0fuvCsla>+q%%Q3<+B)s@BJ@dNl3-l/07S>-PYD#&@P/02YgP^?w-Y_N"
    "eZ@Y/dirFsbmnY.$,trRrPqiqdCY0M0Su`8L1/)tt9b/MbP36NHbx9tcK'0MN^-5Nt>t6BSK+Q'jndsA.71w%,Pt$OsSbg$)noI$l`#u#pUp=&rTpGN9'#G,E0dMr2u:7BTi]q)ct2^t"
    "uX)A)V.gT1ovbH(*E^C</&9_8)SFtY*voAtPJ3r$;RIRR@/;=5;8pTt;x4A+Ep(^YJE`$t:/_8.t;mHdF9P;7JQP,NbT2xL1+-IsO5ZJt8isG)qew/M96LK,8(PA8I-ADt/ag3O)6CpY"
    "D[7Dt>har$7&bO8B+/n/T]'IVd(m#u6*5)3`.)5,gSObs(-SU-twTU-49$gL(0.mKc]pdsb6wq-lbl]tO.bttTq:o$0oBr-spcP1DI?>uGp&f-iV3r)@w6_sbJehLp8xiLi0J`t*K>g)"
    "]&NDs03cFqv3,;=^)8oY8m+X+.a2ut3tBLp^)fv$$?\?:6bSpP'e<6fs11/#*UjdDNeZT<MW.'lS%:__s(uk?ko#gG2]KZh_NI(B%_XkVP9f%5uDU`Q29ME3Y^rsXG?O`cs?doDs5oR?6"
    "sni1'g>0O1$,.-%*FiYUTj:#u*&qis:%9cswO-M25?-gLTR7'K)/uF)::DGsuxH]-c&sT`6xE/Uj.FCSw.Jx$<1uq-Je&u$9q@?UJ,p;/wC?@FHI)@MpcKI&+k#OX-89#Y4P5k>.<=6%"
    "N];.LjxIx$v-VuUebvX5r9#ftZ=081iK>g)7tnd@<@;Gs5j*R/Qh?l)VbuJ10%cA#qjASo.I@RTcjI+2%MKa$]l>%=@)UMsFfOZ-76/C/.sZ&*`.J>-J0J>-m?`T.QoHDsHJ76/=,*&t"
    ":6nqLBHkrn0CZLK8pKX1Hcg&%N4T?g@<B4([DJa$.<kBPCgrrnn_&iK2+m<-ie><-:h><-Ie><-W5]T%Gq9>$/5CPoK(KkfAauYsCa6Ksa<1wtep-cth%)@OKmQ&.[n3H9#^$KsEJ<@t"
    "Q&+Fsi7K7dLoQ:FS151-3-J%_&J&[<?Y9`t6O6t.u76VslTJR3KMqq;.Tf`s`A^n)5Q'7%wP;,MOZ];1;sC=5XiXsZ@*N#u%<^;-A>pV-jZ#:)DDcb$MEP/%dRUIDuF<jKE1oq-L0BU)"
    "D5aU)B)NU)kJxU-,EWG<iu:&5?/XG<ccVTf26A_t`9ek8q(ocMm-F=cE@bk8m;1$%+<GF%YUp*Dl)&Fs:@V,[cLxx$b`9u-/qF>un)]O,d7TVW<8ix-jTS5SB?<W%OG7+Mx]:>$tj&.M"
    "2''`t_9vNWjAE'.(`;&5fD&(uqHMGW_raQD2b>U$D7Wv$%Jin)kUBs06^;5*Wr:WD6ta;H(mNM_#Rl59egDg)llt$H,Me%[Kk/u$t_>ATO^:#uoO8e%3_fP]Y/HqW<#C7%i5^n)Z/GN%"
    "XJn^S+Q#97bh/.(:<K@TZu<`t24YUZ^[Cbsntw$T[G.FRBw/u$qi-_sQv%5E7'v_t7X@stm+.O]AdgDSCgjd%BwLDsEl)FsUerFRRtSQ+0V^]tok:g%-YsotbcZK#`gU($pEx$4,$nqW"
    "459wTP1F`tgu$ptNZW(RTtBo+h-CCXB_6.Rd@:7.k'^]$p]&_Jr%tkWV@&GsuK&f)Pl[Nsgl0F$mkfs7pn$rW%(=_8.8)u$Q5GCKuIOkS2;)>K.p-Gs`.g$Lf5Xi+#Z#.V-B).V6+3S%"
    "aCNh$PHa)%7#ihU^hkGMH67[[bFNh$]?'40KCJbIS?aj)'hQb%N@lhLu.=XIgpW10N$Hu#;Y0/2_41Lh_kNh$/S%+2#j=U)e:mN,3]n0%Q.0d;pXQ@M_GY&%vc02pVZFZ-gL1_AqSg:$"
    "<@x63RacJU#2Tq)(jK*%FnWu)Q15TVFHXWVQl5-I]Th)IpS_h_M+7$`Hs3^@o`rS%gRLn,u&mwc<GX^::VV:BTZQG<gUh#f2DjF^TVF`t+i(h)%>%;CH](J%T@2DZ6l;(?v<e+-N*lMZ"
    "fOLr+>k+)M=Fqjn46XKf-[V')(I=ZsqWh^iR+lmsm'O'*TrPFgmXjoDgn7D_H8b3<.%u_t.V)btihE8nw6)mfcS,.]L79k$.A;*H4QOH=.FH`t@;'Fs4VcI:7MkatO.&f+4GQ<cDQXJ;"
    "0wqZ@Lx6b+[+'Y,V&2:61(5F%FWO;lFvd3-?G:8nO^vqt=w[C<iY4D5TAa,-TJ`,)mMq-L)sQ;dkXvwbGOuigtBu^]40`prrv%u#oaMX#:0EOSj?S$lwr)IqH_2QO1;YVIYRmT.v;+ZG"
    "B*ZNs7]g'%dM]wq$Mx^+ZBeRRH4;Gs#)B;-h,l?-f/1[-&dmw'OTA#-AK8ps(,tq-31nVSgEc]tgd6FVSB[%'?KxDjFmC&'(DHgQ([Y*rVd+%#&4>>#xDNcD=^OlSNjLrdWW.]t4xor6"
    "L%:DE-FHYP@K6Se0'FD<UqruGqdLYYp$0g(.i;8I/.(&OlPFYuJe5s6.e0s?:b-#P9+h;-N'vS7I>$jT$$M>Y0Q#vlog0Z>l&uSIPWlYcm`0?#jm'H2'+S#>SkIvG&p?mJPm&WQS53pe"
    "gG0vuo9[p.Jq:T7wn[#>S@?aEos4WH;FuDN(QU8ngHhJqT;w)*qw1<-u=Qd;IM4KCR)p8RrB>aW8)OsZ]&2Zc0X;ghQrDsmhKqJq6IA6&boaa*2;w&4b5bj9Q`x/CvDCTIB6avPe.r2T"
    "8MPm]c&?^b@^X&kj^Z,r:IjT%sEwp.HF^Z5kPkj94_XZ>VC?EE=Xq&Op*TdVFr>Q]vR9QfOS?Zl$j=^tRa(k'lCTB+4gYK1TeK?5nSXN94e'w>an)'F1A33Ktu^EgK#1u]*s.b-(17@F"
    "li[=<De`jtX(j`$G503g#u,K%9U[,.loAWZ$^e1-8$bFJd+0@JUY(2LZg:*Je*od)7W)]+HOqk&xhmk&,j+QY3?Z[+Hgd@+;AQmU;px)EaeZAF:tUG?^iDpV0$xuHcxYS&?>DU%j'ic*"
    "`1FT%ul2,%i3=jb<Q1-YS2>I%$V7.nt.fx$k4XE60)B>a)-%Q?x4xB?$qE0a&B@J`Nf68VD8GI%nOkH[T1sSD&]uGD@_Ve,jgl0(2Bk;-+t33&`5HXf.^hd,bAwU%*[$)4c]H5Jcp-eH"
    "m/4)RU1mS&+03DsL#6otYGoDsE3?OsJpGZ$$6L<(1`qC<hcY_FA<5+%-BwI(O8Iv5tKP+u2PJu#]$gs]?T6[UQ//GsXMEX$]hKTn'6cYsPhu+%ge7,nSLWSnc1*-t]SSx$:EX]<E3v`N"
    "Y4m0T;F%p$.HLbt=XVLT_mO&=xd@mLFk9x$PDpC<)j+6%3*Ex^i1T$9>KL(3/AYf$-gVX,OR#T7AoL:Ho%0sLp'gKsM6(:Hh]9g:dK8OMNBcLs,x>X(I).N'$`i.G#(#>Df%$.'Su><e"
    "?4MigTKvxV8_4Q&a6[H3Yl>,H8^9fVl[s8&U@xMf+%?2-/fGQ+0dSMgm(#./@7:E43Md4.n$gP'2x3T%6)4ST8GLGstsoNZGEwxP=r0B>U3G=AOjke,:XM2BBMshk;$1ak?KW#-rM)x$"
    "&'UE,'$eK,d/hE,:dS916Woi)]u:_hLW4':$iViF&N(1Pn),@`$a;`4hwsa6s=b+3:=sr,X<^/F/&=>Q0^e:W`j+MH,<QF%w_j*%D6R$t8tf3kx>2d2#M_:>gNh[,:Q_,)+R#NEKo1oJ"
    "#0WGs%OO^rxd,S&N8D;R]m_lF=b;noY[3/)M)9;K@/G1L#tFat'*Qw)?/%d<H^98/tQa_a].ugL47u42V6H><C'B`9av[S>/'qw4HEbat/Do5%jQ0UdmD-X`MUYFsGhKstKk#uYU4c4%"
    "vf3Ds_UNA%VSw6;AP_,;V#Eb,33/^tl.#9&bU2`tSpRwk<caQ/3ZOB,2m/?&ZKXW-:6dJ=Ilfn7]-0Fa3w/7735*Q&u?b:%P*9^&tF3Z>aiS2;=n9eXRPbi,F+vx)V)b,'W4`_tmIW78"
    "a<wDEen5`*/<0w@L--+%C`Mqhr(7M1<0j+Gcnha$7:>pXEFS-^C3A;@r%qO/bNZN1-4b^s/Xu)_o#E,%/MKH9r/U=m2`?QZM+bj$Wj3jEr1@Ej053(dO%[G`1SX2-BrRFM35]K,Ixe-I"
    "u].T%V'u<mP,G+4V&w89#,N#a^/G%U)R'cIBW'Q2q<a4%_8.tkP4>r-_o^/F0/XYQA>1<-NRgs$(Qh_=:[t_,geXfE(vG`T,<D0-=tMDjg4D*-WO?\?&HC4Q&q6sD,5=QjgC`_Ls?&i]t"
    ";->f$]U,Y82%[<$7SFDs56[`$2^A^b4vn&JX@xVLjH5#Tl@JMCGpU>Tv]8r+(2$S%8Bnd)6I(aE$u:jb?^r7os,Bog452:_P,vLAb[7^$+W`SUWpAu,OmZ=5S>7CZu%7e4%A4ceGn)@S"
    "NH-GsKcKj$/<72GF:2?%p_cb>Du6l/<$3LhA%x:d%jsMZ2$6l,Gn:4](7):NHE=P'FI<cflAjPf%]1gfU$3b,AJ3tA,[rS&ZuET%k`*PZ7/]1-k8rVHIUW6EO9StT=p^fV412-@n_ZbH"
    "uE4>Z(M)OYv>%p+&af&Jx#MV_%Taq+w^6QXb'A%cK2q(5B]a:Bjp>aK0I=Q&?8[:6WBKvsameEscnAUqvFp'EqTFB,VWG1fjnc2gBj/EsLh&<-4kDl%sps7%f(ld)o5LM1:eFsOX9CHe"
    "f:+U#+Y@hp6@uPR#XEO6/hk]ks3)FsJ@s:eRa&:HaYn3WV^IJhI;NWq*O&=QcCBXC%vrNs#OkZIOI5UZLaB/%n6l[U`UV$u+#610]_1,isf(gsa4?l/sT_87m,:N'%f64&4^OB,^$*:X"
    "ZGK4Wdn)3O&+bcNm8ReK7t_#V,MloSIjP5W(7kGH/b^E%T#1>^^>U$b@L2:6VbPFsN8uA8lrV:6CE,dtWxtit0sk@+Jgs1h3K9Vcp$QTcFZ:`tkd*h<Mx^P'<+oQ/kuM8uZ<pgMOAmc%"
    "(6QhK[VqJsuMaDsh5Ljt9'@ctT5whKN[[4&_l$T8]bIVF1I=Q&@&JECth8BF%Y.q/.LP$c4J'(EXCeP&3YX9&KWJCgVc@8A[4rd)M'VNdOF+J@qvMG%?v<_8q-r.@frr8&PbXX57frh1"
    "v`YB&#E:N'A,;'D^tNp@@C3@tV#>>lu;DLBF[M)%SnL]tgJ*A%F)vKp;>Jkp^@qh'W3BvflIBW96[sL'1_F9-YKC%I<)IVeMbnB^YS*CB2'/@%BcQK=H*gt>CtHO]*ZUQ&I.vFsv9s-$"
    "(@^qt^Po]$:W.FMgFxT&vFl<gS%Wvsd)UEI1DCqs0(^ls5+])sbBT$KCsv_tUE$ctWtlftU_4F%wXIU?r5Z1P)9MR%6.WNO0gId`2;JK,HK.X^1xD@eirJP%#Q7ZjoGA0PE%Q##$Zf]t"
    "7KVGs#i)l$[oXFD&H9atb*VnsX,pC<)9A*%9`efL5qhp1GFD(<OD6Q&PdmV&oOqdNWH,:4l/7a$2/,^tJ+6?&(,<^tfft[+2BC4dWE`ubF4LG3O_Gl;(fm#u`X,o)Db%-D@fHA&d^<?G"
    ".b4>Z3D0b&PkD[]P;xDs)$$'=H8oT&psWQ&Q5IA&Q`Vj;%D.1KKTi<dV'ektL5j8&g[rS%,<<d&exoDsC(cnIYa?s6G7UJux%nVshO-<cvK-x,iDQN'BiBc`*_bEsTnX6ubC@asB]f<t"
    "F(UUd,pi^'uO4**d-5?GNSFECn]4sg7rug(Bd$gkd*16J(]4=t^5LI_q^.I_l8jeF9A$S'e1XQ&p)lWV#sWmFA,0UC:l-o%FM#W6M*jI^*r3rMmIcjBQ3QRAS,7h(gUw9ZjpjWs/r.C&"
    "pvlXsnNxTjb,&f&e8*.hG@XS&(Ag]tq']atji5?G-tBm/q8&h&.(`slw/n=.&;4c2g=`/)-V3uHcEV/Uawh,`G^bWsl97P]6OlWes)EX&Lg[&)E/_@e4cCUH*aOitcslw=?$$Rb7Xj9`"
    "D%QU6:+0]tpSV,qli8Rf*Fl?%/#Ivs/rXpeobeEssqMA)9Z$Ns;.1CEnbnYsL3nBb+P*IVguGUHoWe'Ng-+ReFtfR&sYb@&/h>ZY/M-t$aLBoNHiU+4M*&N'eR5OO]B/Qlw#Uk$t9B_-"
    "s/BZJQZbA#K#75$0RNd`o1mFIMh@ds`?N#f4OfptSOe@#RO'6$n-YHsF]fT&`3AVkH>=wN#xxs6FvmCsWxZx:r,aG%Avin7(G]RfRZ)+D=wbb)dOu%M#0.+[eK4Q&-cYS&ohA.LO)Id`"
    ",MeR#=cL&M?7o&O0grrnvw39IL[hsH8q+Zs&C%Es`NRJ48jm*`ADK')>vn0k&Wh'W&hCG#2hk*MpVPi&8n6]tE*&&$1(<C&Y+*C&?]ja&lV=o,1####u4T;-8`/,MI@6##:(m<-,G5s-"
    "@]>lLOH95#&15##M=#s-Qi[+MIFH>#%=-L,td9B#J2G>#LZajLEpdF#rSGs-DDBsL`cVR#6UGs-*M'%M?)xV#d=cY#.n7-#75+e#Ag(T.3('2#N&Ll3b]Q(#1Bpl8`Yx+#twCEHXxw%#"
    "L37e-k%CkFtNj-$0lbA#Xs@?$*>;MFrf^e$8.T=B'k05/TdA,3O3dA#lvhe$R9dA#[dD#$Q^.`&XIjl&q2i?B@t&/1cdQF%pbqtMS1-Yu*%h>$vD(t1?Fw1*.e@(#<c<q6NoRfLsd7iL"
    "t5$##NbPlM=[S(#`AU/#x4n0#:(02#RqG3#kd`4#ZDI#MsxZ6#Qoq7#jb39#,UK:#DHd;#];&=#LlIfL`<w>#CF7@#[9OA#t,hB#6v)D#q3K01Y.^*#d]YF#*Tu>#K9-9/:k7-#/OZ&M"
    "3mGoLpVorLl7W*MMCH>#9Gg;-VGg;-6Gg;-3Gg;-If&gLn;$##t5EJMOm-9Mo'VS.x'acW4vIP^:#)d*X@rlKipt1qM&@MhJGH,*ucm+sS772L'VF]u[*gcNg9EVnk[J88Um]G3u/SD="
    "RQaJ2q&d`O/GdV@/l[PBL*4>dY=1Al)Bsc<LqhP0dA88S.c@5Br/)&Pmn+p8@Yw],t&8)=6V#,aKhL50gIN?6(I*7#0l:$#6f(T.=ke%#5_&g1j^''#,Q?(#DDW)#67xNM-NWU-Nx-A-"
    "hIg;-4N,W-X@[_&Zk[e$5SERM)XlGM-))PM,m=ZMSM+G6]08'.'YajL16rtmdpdT-NnU;-o8(A&,^e&HNnbl*NSu>#>7jr(QU_Y,CFt-$4p#)<r[Ge-/w^w'aZdlAM.U`E?jE^G^4>fh"
    "DBa'/j&qx=$&###vl^e$0>Qe$$BU_&5UHIM6Q7IM9dRIM,k1HM>B4h#n3s5Ka2F>#w5MR*B#_e$:a^e$V`_e$W;1@0SXee$sSVX(BGV_&u5__&S&Z_&wmfe$safe$sxNR*c&De?RYPLM"
    "urlOMRO6LMO=qKM0.]QM/(SQMd;I]M/XlGMda&NMa]NUMjb,WM.xIQMBDOJM@8=JMtlcOM6SC[MK%LKMl<vWMhwHTMZ$f.ME^FD<qI7^5#twI8J&U3(7k+/(Ucsx+2XTq)f%K9rIs80M"
    "*rHxk&4v;%Y+=R*'KU_&BILvL4m?(#@W,)3=xLYGcR&g(?5ffCXT%At;kfM')QorQ/ov(aY2&gCw6prdu@Qs$[+,Z,R?V58MxB)ED6nYPeLwfUO/<5fZhOMp-.s5&0;nA41B[Mgp5_)3"
    "T4J)W2j1sm7f6ZGOH:B4ksM)sAe86&jUYZ,mC7B=*ck)N=+Ig_PI'Nph*<6/oogg:Aa26AjQSZGu?p)WG1;N^px[sdBj'BkkZHgq=F<t$f1TB+8#vg1aj@681DCN^e#+Bto/4*E0DWNK"
    "X5#tQ+'DBXSneg_&`06fNPQZlwAs)sI'^6&rn([,u[[B=2%:*N^Awsd;q0O'oIeB=D6sZPWTPBb-.x6&8;[*3*%dBO@1ugqFXUt6bQKtHi@wNTxklNgUuT[,#n88%C5SY,VTj-$wWj-$"
    "V735&`2,F%-Sdl/FWCG)b#6A4X$(Jqg&Uiqh/q.ri86JrjAQfrkJm+sv:8Hs(tc`*Mr]CsBrl-$]CR+`09<xt]/WY,I(75&:0CG)[#,F%%',F%xZj-$kASY,[aj-$-WC_&'kj-$GIK1p"
    "jF;ul)T[CsllC_&/K+cijM,F%3Q,F%@ls9)IbLk+;3i?B:<7@Bx]+29$UG'S9Ln-$6rl8&K,0X:1-*XC%ej-$'kj-$+wj-$Y%VV$`X3B-aPqw-&R-iL;j@iLQOqkLF]-lLC^6'vN:4*v"
    "^bErL:qOrLcDHuuF%]fL8/gfLx3pfL#:#gL$@,gL%F5gL&L>gLCRGgL,XPgL$]Q>#3`DW#aCP)Md;MhL&_.iLd/5xuJf&V-Lqe^0Z<5'vVb.-#lYhpL^U5*vn$(/#+$Sv&:9'T.P%M$#"
    "-1'C-'E^$.k$]fLOp)vu,rLvuIc,t$BxeM'&qam'JYD/(5K'N(9['edOOB,)ht]G)b]#s$1_JK):3CG)WWj-$%ej-$'kj-$iWCG)e.$L-Up,D-g<.6/',>>#[x*GMqVZ)Msi;aMCX9=#"
    "Qs:P-S5T;-UC^$.9HIbM^K5*vB9s..-l3$M+N;n#pUgp7a_i8&bMC_&#_j-$@9x;-&7aM-3(m<-*G5s-*DQ&Ms#Yp#/M#<-t=Z-.Ql4wLkBSo#g?*;NA0r%MNA#WOQM>gLTebjLTebjL"
    ",7VB#*WajLh0VB#2`Ys-vrhxLD7AYM0E?)M:dn;#5.LhL#9>h%589p7<IL]l4@hi_FN4W-w*W9`k%75&X;8p/igpDc3>]^.bX6,v0&?D-h8F,%Gl958;Idv$A_eS%h%3#hs])j'437wg"
    "d^7wgim7wg3iA,)B56K)CWxr$.dXW/HcbjLZR'(M.iU:#MWf;-wm-I/K%6;#Y4UhLh<6L-%jB)2:2Guu#85##4Jluu;=+gLSb)vu8ZaB/8c:vuVhkgL<3DhL=9MhL:?VhLsUUp7=SV8&"
    "'Zg<-TS]F-7U]F-<#i^+ghwwuM]d$vd7>##1gKs-iR,lLK`Z##nLOgLqAm6#-].'0g3u6#nOI@#q@;=-s#?D-c%?D-wv*O'BI[s-bN]vLNl-3#iecA#2nsooiVxQE]0k'&6bPfL<;Ab-"
    "PWkEIY%e'&/Wu'&4)X%O)Fn;-&F?5.)2xfLeN)Y$=EUM'[Esr$6uUV$cvj-$,$k-$CUK1pg,k-$]1VV$'+-L><;%@'(]wK>w/48.G<vE7ol^-6'w?/;YncJ;mZ;,<uMk]>vV0#?)@ZZ$"
    "#WrOf++r-$K.r-$L1r-$GSxOfhmXc2;21H3*sU`3XD(E4Z,AfhK2w.iL;<JiMDWfifgF5p;a$MpdaXlpejt1qfs9MqwVUiqh/q.ri86JrjAQfrkJm+slS2GsiuRq;opxjk@WZ^,pgZY,"
    "-1r-$M4r-$JMqP'volJ)Kb[D4%hcihK2w.iL;<JiMDWfifs9Mqg&Uiqh/q.ri86JrjAQfrkJm+splVGs@[no%LfA*/?PmJ)DM7hZFg9PS->S[-:uN,scZdCsG*s-$h-s-$i0s-$j3s-$"
    "k6s-$5ndCs=Bf>-8Bf>->#_8/j;89vB,M*Mj7W*Mk=a*MBf^9vb<_hLxiEr7jc*^,oQ[R(.-U9V]pV+r7#sFrjAQfrkJm+ss(sGsD=Q>#<eql8DlCp/i%[w'@`>M9c8,F%0Vdl/.9K]="
    "#$,>>9<hc).pt?0mUmY#7LC#$xh^>$/JC,3MOr;-=Saj$efK9&XYw%+R?BD3%2tj:P<(5A7G<PAR#M?@-3.;#KYwm/:[xt#isrs#v4T;-w4T;-H7_M9EWoooeVC_&IKFs-IUOgL^'3$#"
    "s_bgLPllgL0qugL1w(hL;(2hL7-;hL19MhL[T/%#G<$LGeC;;$$i1^l6hUY,'&6eHIo0L,iWigLaL>gLIYPgLx5Z`-eL<L>kH#j'Zfu2(EKYJ(Wu-0)_3&/1M_:3t)qj-$*tj-$+wj-$"
    ",$k-$-'k-$o@m-$pCm-$qFm-$rIm-$sLm-$tOm-$WnuQN&^gl/jmF_&.+n-$1a48.x)0F%rKs-$E#,##xFp;-wC#W-_GC_&wWj-$K/###Y^j-$M5###[dj-$&hj-$'kj-$(nj-$)qj-$"
    "*tj-$+wj-$,$k-$-'k-$/-k-$00k-$13k-$26k-$39k-$51%##UOdl/CKCG)Xn-@KjF5JC>aB9TjN[j9][%_6fw_RfZPs1BUAEq0;pNQ.JX6,v)oQ>P*UdA%[u5J:lpMs%a^t&-1)Bfh"
    "Qm[+iL;<JiMDWfi/V,-MNi[f$%T(H.Phwwu:[/g:8D.&cj/_w'/.n-$JMrRnts/F%S+O1p..O1pfjOp.u@YuuC1He48)ks#%@]5/p_1vuROFgLHXPgLZOqq-pcE1)Ft35&:V9xt@QP4o"
    "7I%]kO6^w'h$GiKcQ.ciYx4C#wGKU##Qgq#0r_Z#rva7%>B_KcUlUV$Bjpdm-'k-$[h)qi$s=k2R8J0)[CjZ:0(m;+4d#W7L1pj;3S.P0Ce[%#Rqn%#8DpkL8Fx$v.(M$#QDcuu)`1vu"
    "'OM=-CLE/1;%6;#G;7[#n;Jt#kU0%%E*We-8;2p%0n%9&dP'v#apB_8EhP&#64Z<-rapC-qmpC-kjpC-S+Gc-$v9kF)MY8&ZXx],u>r`t><CG)E(Y^#)go=#s+UK-aP40M=0:w-QObGM"
    ";EJBN93jaMMp_xMO:uZ-3M)Y1@FK1pEQ(,);>%a+Ob%Xqnc7KDnlkY#LbL3tU<9-m$uoE@2`nRn2P^;%kb:W%f5a9DMPL'SE]vo%_Yr+sxI@MqKX=eQT>Gm'6g-F@f^2p/a9`-?ET<%t"
    "NmQ`tl)7&u@gv%+1j0H3WudM'F)1j(m4U'At=0:#kFDs-hXXgL42WvuH<qHMLd<s#7`Ys-PTLANnj@iL`;n]-7i-F%pW_`tcKPF@]7f34iZCG)<cQs-WbK)N)GSo#(Zxt#3NvD-mVvD-"
    ",0A>-*0%I-_/A>-RWf>-0+NM-'>E/%3FMe-S:x%4i'%RW<(oRni]LR1#.gfL/KBt#ax_AMk6Q%N0'kB-Z4@#NtwO(.R['HMik@iL#me$v+?9C-&7u(.^t'hL;J/wum9aM-cK,[.KO;20"
    "/D6*2-dgj0X&U'#E&>uuB%XCN<VT9vZh);nbeU]lFUUV-4n4R3L1r-$-fS1pmoDrdb#/Z$uC>8.95vRnCvt3+gHFF%,%ZL,rCd9M<7cxuJ=o+sM-D(&n-45&Gn(c%dl4wLtS'(M.:#gL"
    "v0l5vZ<ip.<<CG)ZfBX:ux<R*#_<R*xIkxuxd:aNg=L1M]q)$#UoC&M<,qXuG,2=(1-Fm'iV>F%i%8^,(l[w'G]Ne$']0#v=(>uuCcfxu22oxuW./b-WB<L#WmrQNAo;ulIPd--kUX'A"
    ")(Me-i0s-$hvcw'-p,@Bxw8,sj+t48OZkxu(=V9.]01fq6;oxu0hr=-2rY<-,5:9OqgRfMtt]+MYk%Y-op/R</5UR*R'XKl^SF.%`Dh9DLV28f%XfwBv4b(NYZ]'NO,uZ-pjUqMY#e+M"
    "tuqh.lBK6vJ;2iL%:#gLe&gGM+'i)Nq*D-MjW?T-.lJGMuex8#44`T.Pe[%#YJc%MtOL1MEJT;-OkLaNL?a*MNnPhL2lcS.fD+##*ne)2AXVW/g<=(M<e.YOMxMa-ujs9`Sni;-5LpgL"
    "5*rOfV/OJ-4Ag;-%]W#.>CT58r:Lk+=KgJ))iv3+9d`kkAj4:vVhu#vub($#Vxs5vb,^-82D$Z$J(x],.VWh#@_L_&wLkxub/%I-&(.m/`0Oxu/_^:#EbcHMK.(58CoEd=&]8+Mf0MeM"
    "vqJfLOpobM:XZ)Mu'4<#fdV8v$L,hLMMK;-8c9=)':TcsGO0DteOc-?Qs3kOaU#PfKi7.Q[UG#$CVpE@>X5/$2l=eHvF+atBx`=c/K2WSbU$6%TK&q7.^),sfb`pB=fC%Mk'4<#PF.%#"
    "jIwA-]?P)NO'g+MHI&FMJ[V6MWtLdMY3/_%BFJ)+ZIgxuXL$^lkXni'm#t%c0*82UUKdf$qZde69aVw00$1F.r_,Z$u7>qVs5t%cG3%Z$ZbMQ8J7ARNr8RF%iZYh#n)i*%7TgJ)j_Zq)"
    "x<t(3b((N$7gg@MBpK(PfBc^-w)iI6U<'Ll=]NX(pf]GMsQiM99s-^lbvP1pHj144j>.FMY;DG),x'#vitcm8-Dew'M'4X-C$FX1T(V$#5rpGM]@P58?c$1,Th](MM<'9vRO_p$ppB8f"
    "2Z=:;@q$Z$5n7F@@/*X_:er*.F&>uu8tY<-bl5pLh#Ao[u&[Y,0HgJ))Epoo&6wE[r^IqDo1?@^$Ue'A=;;Mq&cc]lN`eq)as?4v2c<Z%*a%f$IgW@t664m'=#nW_Cmr'&H&>uu36UhL"
    "m(em87%Y3F.&Mjr=.5%Ow2@A-271Q8P+/3MtSN.hN4x],C@LgLs='9v03i*NtKMu&%/fU)Yet?0n$Pe$BG;5&=i`=cGPlWA6S/cMpZ7JMa,NjO3CrP-H#5@M'dJwufZ0I9F(&73uNLF%"
    "_9/ri@Q4m'=I5f$.B,9OhWh,ML&g+MP8L1p&u7kO'k<68Y4*e+Ri1&M`]XR-Lxh]Mp&^%#Y`FlRj[tk%DhuF%BJ`_&Y05)Moaw7vC,j<#/Ag;-x=aC8<KF&#?l6'cp:%)3VJ^@kar:5&"
    "Z5fGM#//rLp8?uu4*A>-H@pV-goHW]N&l?-%#4;0oKel/j,E%taAfqDVGapB]/k_OY/^_/]d;<#2AP##l:0#v;5rEM_rT=u$]b&#H>.FMM7R^O2a#pL%j;aMm5?uu=cH39W+=W8TeHj9"
    "ceV8&JD_/:ts<wp/vnxuVd'-%$a%_SHu?q0&xK>,r=G##m;(g.'R?(#k&>uunhOP8:^Fo3SFFm'[Z3L#CY/]Xa>@'f'kCG)Q6qOfR0dCs7(VV$L_OfL5ZRvL+G,L8HTe--kc]oQJ+W'M"
    "=YT9vLErR;cM(C&.jUH;);*I-pJkxuB=#s-g-W'Mb@*$#*p=:vb//9vH'6;#@%KgL2Kg8%j$:M8r6q<1+C7_89+l<OOO42vWr$8#6Vb#v]gV8v/xW9#+)B;-*:.6/$04&#$c)'M3=-i9"
    "I?q#v/8:3v8Fluu>V@+MnUeAML'rOf4>bi_+Z.5^?QN]l$B9m8;:S]_1Vf+MrqJfL+8l5vxhwbM=ZoC:k<HZ$.Nli'pVRJiH@mER%Qnxu6V'HNQX9gLp*J38HBVLPiw&-4<o_@MUvw%+"
    "uruIUCd>MqghdAOj8CP8Dbu&#Q@K6v$0+(&5u#I%,wE$M1qT(McIsY$?WAxt/U)W%t+9;-Ep6V9-RY'AP*-f$9sl'&%(t+st4)F79]B.Z%M(9&7(VV$4Z#K)#Cl3+6s&-MhojfLVPC6v"
    "jIAb-N<EL#4=ep0H`=%tn]+##Hah3X;V2eHfPvRn_[AF.):HZ$2735&G'D_&MqTe$0]O&#F?f>-Akl-MtqJfLIgC%MWwx;%IkKi#orj5v$],d$2g2DtHTjW%(2###%S$s$G9v2r&@GGM"
    "#`^W&p(+wpi#w%+C<@gLb4)=-P./rLsmjR%-UV8&o$Ye$o0D^mh=:1MN1mS.)j4:vq[iR9pqgJ)g^Le$hUIK)A[co7@:e'&OGc;-+Rfd;LjQ>6YV>R*J=UY,.uF&#w18RL?mD+raT:i#"
    "P0:jiIcbjLW$;'#BCIs-wdtkMI-]o+nS@+Md>HI:X*J`t#_lDYqnHMMNOQ##<SjDY&.-a$L_0I$YF35&Oek-$NPUV$9I#PfTP*M1TaKM'7:RS%Xg[=u6U#.6@Qs[tCr_m'9rYY#&QD'S"
    "4iWh#6=el/3:l'&9sGK)uO_@kY@5kOde35&kjoooqG=L#sv.D;.W+C/$`#7v_+B;-e[jfLX)>$M#sJfLSGx38.epJ)R$f+%:9A9v`S&7.c_kgLop^=#:sL1.^fwbMUfXO%w1FcM&_YgL"
    "oW/%#<toj-2,<.F7?VhL]$=J-pE*nLv_66%9?$C/#V'#va6UhL-V/%#M[i;-DLM=-3rfcMX:nF<Zvr'Qs](/;dFGR*`J4_JO(35&?W4m'v,L)+W4n9D]YPn*KV'#vjaLr=@#A_8o1x*%"
    "+MkxuQ`x,QHh.C<Ow-5+Y1:#v*iW5v4tUH-?fOk%Z29xthjH(s0J###E*s-$W4n?TrowFMPVjfL^3i*Ni%u4%8SH&mC`T9vGOOgL>k1eM=VN.huQqWfZYh[-&<AF.eQu-$aaCaM@tN8v"
    "`O#<-+Q6*MJ2:[O:=a$#]ABp7KB+44M[Oh#mg_3X+a+@9^8EX1D)hJ)<w<68R/4m'b1iJ)-w3m'rCwd;O$OYda+,gL9IKC-FH(t%=lae4Ej)fq_+T,b3IcH&CT'F7%,S:):S,.MQGAF-"
    "jC't)]_rM9Z>[dFZwEc%_L%=-OHHf,QdZdFaM2qL:W3G'M?Qj#D5?t-eLOgLKXPgL;^5<-J@#-MK@/Z$:(bh#R<Zb%E#l3+ap$8#M.`*%Ee^i#9aGT.%l4:vrJ2&.805)M>Uh[-rJ^w0"
    "&j*#v>+ofL+F5gLkJ5&#7s2%v/he6/Kj9'#[+W'Miv:.9M_gJ)XRx],wW#^,r&Rt1/Xn;-AIg9:doCp/vcj--uP%/v`Th3v4*Q7vqsll$>Qf9vC(@A-@AX0&f#M*M%%f$=mMF&#(g:Mq"
    "%?M`tnx0w.?*s+;c#Uw0*,&:)>%=,stPs[tXKd'&p02'oaO;qMkvw_8<@T.$`dq]N:tT=u7Ttn*]0.dM(%g+MtrF_N-M#<-NX>hLN:$##oLOgLQ2,1:Ex-@'8$W8&]TQd+.],<-o1*j$"
    ";7Lk+qGn&#$*j<#:njk+[8vPhvYN.hq^;K3vnRvQ[>R>%]FHQ:EGOHWcx)_=%]V(?TWwE.<pm(#Yp@,MSZR^$M])C&Cfw],C-ew')IQgLq[#p7#wkkM07<uluLSQ_.Gx38(gbxu_O7<-"
    "3@&j-D&Ep^h&^%#[Nc)M0Q[;#SrL*%k#w%+xqs9)#]b&#Qx<cM6/6:vESak8[T_]lO^9dOjRq1MSujfLeAh<.B@=rdMj>Mq8/TgLPGEF&0MJfL-pD+rba%gLh']iL93QD-T,UW.pGA=#"
    "g>#m9ob*wpur&gL5:-)MDk;aMMYZ)M$VoYMRv>98u-?&m5liu$_tfi'aBA<-AU'5'=:9.$K^3dM=?VhL>Y/%#rgZq&FDO-=F+7'&Jj_2iTYFo$?<G)+BIoDcdi`3vG'N'M'q0o-(%g-6"
    "[(VV$v'VqiNrv?TXf6T.]Vh3vQX;Y$0DU'<E^aJi9ci=cA3C,s3dO+`]l<JiS:U]lbmXd=`v:]$4lk3+`#mi'43M&#[wRekIhOW%+Qf)NcS5W-;q=qV?U#/$^KIHM<LYGM*wZxOp-2IM"
    "6(#GM$)`=%@3nw'1_rwKARG&#etwP/A/Cul`91fqU(;<-*(m<-,wXM>GSNX()S]&v>I5-MEYka>Imi'6*AR.%K_'.$^./R3Bo*(/.F)8v6NM=-(W@uLr=B[%-_FqD2xO&#oC,1PeJj'N"
    "j:Kt9ZSF&#&k3r)F]^W]7jp4&JqQHM.DcCjC^8AnqkQiMG%qXuO2#;)FvT-=m_vI'_<6X/r6WfNvP:q7_V6dbP<M.MJ@%F%)(W/4gaQ$Yts5`-xN2.b:p3<#g)>I;I2LwKKbZgL;fSFM"
    "O@(/?l%kDuJ`t[taoTk4XCs9D6Xie-0&cdMUr=p7jZm+sfcWrpZJK`O_w<_M<vDvuhUs+M+x)$#CvUEM0.,h%j,W-Z=8a`t/E+(du8Jw9XSkxugcDK<#u);no3M]l5j^oofbNe-Tet5V"
    "+0KOMCi9xt#Taq2=49I$5(Np.?u>i^#h'=((j0X:8ne'/6A3/(6]###BJZ7n&'flDsPEU-c3jq/3+>>#pJ]9vc$)t-)6UhL<;'9vwBg;-WB;=-JH;=-wi7p@Lcb&#A#+298)J9&Ft35&"
    "=+YY,_wvumKaoDYR/Sn$<UwDl>Rr^<I[0x7(G0wLBZXk8xU[Kun^2gL1XpgLruXP8;*k630**XCCF<<-(,,A9NrDe-%AtKlAAL<-r`EU-V;9G.uY8xtdm9MqP(hrg6j;A.>WJ%k80N2B"
    "Ca8q'm76L-+Te<@xsm+s6:lxuxCo^.u(>uucI2u$A=?_8^V5m8dUg9;+rkxu2J9c$De<F7n?]Q89RkxuH1;AMfO4&=ie__&QFCgL?(',Ms1fr%r@hAO4hxg'cX0I9WfvG*VK3L#M_?o["
    "g+:L#NU2ci(FEF%SJ[3i'Y_@k0KQV[<0-)t66w?9jZCG)xh/k$1#Mp.q/uRn*EZq)MH&F.2LZY,P/K9&QD:5&VdM.h`V`lg1mt%c$N'&cLiXo$C&BqDsT<eQd2P<-#B998g]]oocgH&#"
    "lS&L>9P'Kar03g*21?M$ZitMB^O=@9h9gi'ZY`g=Dv`Fr3<J?px8sgL5tLdM`.g,M?0*39M_'#vWXk,MLZb%=nMV8&sS+hL:rJfLID)qLs(V#v-6lR-;o6mLA*@q7-V_]la)C?[Go9xt"
    "nTC_$DBxY-]?adXsfZi$>a+##gn9O+hKCG)fIni'2NgJ)7C3W-/G8:)sAw%+I=o+sUI>#v4*Q7vWh(T.H@0:#rv<J-/4RA-Mk8p<)04kOuTs-$6^K%M;o=5v%35)M=.r%M8$qCMawtoM"
    "w=a*MeJH)M+3SRMDP@b%dl4wLAC8#Md_/%#$^K%M`g2i-[_aw'j2^58,3ZV[wF#s-q;@#Mkc&gLa;#gL$RC`NTvof$fZ1.$^,Sw%j9]c(H0%Z$>l8eXURQ##RqbW%FKW8&-]u9);10.$"
    "u=0:#OW4?-@J@e%Y(VV$0+Yd+?_e6/(;cY#os-qL-M6##-),##-lls-hXbgLZ3N$#&[FU%2OAJ1aKx+2,*JP/vanl/N/>D<OxZc2SBb%b>?g7n?UAD3SEvA#h<Ne$`m;^#>n>e6B&TY,"
    "l6.5/WwZY#&'^]=/B&##UkVYG^1&/Lxc@A+@aO2(PHM^#'G:;$^a7R3RPuu#6INP&9KFm'prc'&O%rKG/T4ciNQsJ2Q?<L#0*ul&$VqKG==ro.e[g3O0`g3OTf`oI)$:A4Mte(au)X'S"
    "@-X'SA0X'S+.Ne$T]UV$Iv?D*D'4eHn<I9icem5'w<l3+xPw%+Ku7a+<3,_J.Dpu,e:4Z-I.7X16u18._Gj8/PC7X1NH^f1_QXc2i5K6'Wa%29,RVf:6sl,*vq.>>x,#W@?rAe6^NZlA"
    "PhPfC=cN#-ew6L#-$bxFas3X:[Vg1^K&O&l(dSS.4=;L#$/;B#(8>##RY(?#T#_B#8i1$#](`?#`;-C#NO7%#;DaD#<NHC#%;2mLDqcI#w3J/0'8n0#CW,N#tuvk%7UCYY_;w:Zoa%/:"
    "FkE`a5dUucE43>>K-r@t+HS>#e:j.Lp=5m0`W,a+fV<<..#;D3;33_]ECBS@6E?PA+6HjCLuk(E331REcXx5/XCcY#,MuY#0Y1Z#4fCZ#8rUZ#<(iZ#@4%[#D@7[#HLI[#x3LhL#)^fL"
    "NM-##'####oK$iLg&g%#78,8.5qZiLuPG&#8_?iL#M=jL#p('#o'7*.Sp/kLnLr'#e_.u-dJ#lL?7f(#/wK'#5u7nLXZ$+#YE-(#+<#s-Km-qL3n'*#@NYS.h+85#otT55pE%%#`YR@#"
    "b:]P#Pg53#?.mN#VF6C#w*B;-g=sb2<kAE#+OKU#dY^U#0;o-#u/QD-',$L-sKn*.>WajL7ec&#)v8kLIF?##Q;#s-5P,lLx?,gL:+:*#G[imLNem##i;#s-(<:pLQw2$#'<#s-uMUpL"
    "`v]%#)<#s-xx?qLgJG&#2<#s-';eqL_pS%#9<#s-2fNrL2i)B#G;E)#sIjD#s=.@#%.b9#WRTU#YO(Z#'/,##':n0#mf`c)-S1v#NnM;$Kckr-1lhV$:=.s$8U/2'5.I8%L*fS%JA8>,"
    "9F*p%]:]fLjUN)#E(iZ#Y,$_#=r:$#H4%[#VdK^#A(M$#W@7[#B4.[#E4`$#DLI[#T?k]#I@r$#VX[[#AlUZ#ML.%#/8,8.VmRfL+w(hLRF6##f(ofLV?a$##;#s-h4+gLV?a$#%;#s-"
    "Z@=gLIF?##';#s-lLOgLIF?##*;#s-(`kgLXKs$#1`:5/T.`$#u-LhL+4DhLcW/%#>.;t-%:_hLmdA%#@.;t-)FqhLdqS%#Lw37/Wke%#rdHiLLXZ##;;#s-/qZiLM_d##>;#s-6?<jL"
    "^jJ%#D;#s-@QWjLNem##J;#s-@&BkL^jJ%#N;#s-I8^kLOkv##P;#s-NJ#lLPq)$#T;#s-=]>lLi>g%#^1g*#2=#+#CS'4.@7]nLPb?+#wx(t-DConLKmQ+#.n-I/shc+#[Z=oLT3N$#"
    "s;#s-^gOoLV9N$#StboLrGE,##V4G/+=M,#q/(pL_pS%#$<#s-6(trLZ?<$#&3A5#(7J5#-`87.B*N'M_T1:#X$)t-bT8(Mx)r:#gH`t-))M*MLc0=#u7>##GY@+Min6>#7L@@#/i1$#"
    "QxU?#DC7@#rt0hLoa/@#ZHkA#+U-iLm55A#C4@m/tYH(#jimC#k9pg1A5<)#vCaD#c8P>#w.vlLjvJw6ja<;$h@rx=9j5>>,ZXV$3<HPA5JalAGh')*XN'5JQA?PJfsCD*RGYlS%=wnT"
    "=%YY,tZG]XAw^xX+kWS%R,7Seg.8qflT?>,+[Dci_VtCj[X5L,3rb.qvvuIq'rVP&oAs@tPDqdm>#NfLU/oo%&i'^#-.C#$Jcp8&UH3L#V%rr$+ke5'XQ3L#[@RS%+q3m'[Frs-kRXgL"
    "ZW/%#*;#s-n_kgLWEj$#L]4589%?0)DBGJ(IWcf(3^>g)ev3L#2s$@'vS]j2cjJ%#4;#s-*FqhL`v]%#4)B;-[eh).3w)JMc5,&#<S*^,u=#s-<9NJMgMP&#@xAv-#c:5/lQk&#DKNjL"
    "kcl&#E;#s-HWajLmo('#G;#s-Np/kLo%;'#L;#s-R,KkLq1M'#N;#s-W8^kLs=`'#P;#s-]J#lLvO%(#T;#s-c]>lL$c7(#5u7nL-Q$+#[9W6.B=fnLI#R+#v),##q;#s-UZ=oLNem##"
    "s;#s-WgOoLPkm##StboLpSE,#(05##x;#s-X/(pLH@6##$<#s-a`qpLKRQ##-<#s-v'trLG:-##u<#s-cthxLbj&%#>c_7#Egh7#IL$I1-I9:#bLB:#dh1$#T=#s-?eS(M^d8%#(#D*M"
    "Yc'=#-O7%#hSJ=#1k;'%Fp+Yu7c>uu&6_s-6/>>#=7%@#<%M$#E4r?#FC7@#F7i$#JF7@#BuU?#x0LhLggS@#IK1a3Utn%#U-=A#aUR@#$6h'#jD6C#@7fT%6<DD3f&_`3r@AD*B:tu5"
    "%Mor6)ux%+TE258rOKP87O>A+terx=?&6>>C*vx+3<HPAL:blAOZVY,V<FSIhx_oIlror-voZiKpes.Ls+TV-<<&8R@lo/1MOg2#8^5N#5V'B#faV4#Eo%P#N=-C#w#&5#J1JP#JiBB#"
    "(<J5#QOxP#3m8g1EYL7#egqR#j%_B#:4-&Mtb+T#C15h3m_*9#vlNT#$2qB#*F0:#-Y^U#f$eB6Nd2<#AqVW#DJ?C#dPA=#N^fX#OM6(#T?*1#s(B;-C(MT.,Mc##w:#s-?)ofLLXZ##"
    "#;#s-`FbGMQkd##Fww%#&;#s-mFFgLM_d##(;#s-oRXgL1bk)#*;#s-I`kgL^jJ%#,;#s-Jl'hL9*(*#D4i$#;(ChLLXZ##1;#s-?4UhLM_d##3;#s-?@hhLvO%(#5;#s-'L$iLd8,&#"
    "7;#s-&X6iLNem##7)B;-K.PG-QrO(.5kQiL&@5&#SrO(.;'niL&el&#Jx_5/p^''#7QWjLR'<$#H)B;-@`oF-e'xU..E-(#eXkR-(S'4.`>gkLZiI(#*S'4.dJ#lLnv[(#2x^4.hV5lL"
    "q-o(#WU4G/Vi8*#+oINM70L*#Lc=41c7p*#S=#+#uAP##l;#s-%=fnLKRQ##trql/O[P+#7`R%#q;#s-hZ=oLa&g%#s;#s-jgOoLc,g%#StboL,TE,#c8E)#x;#s-F0(pL-IF)#$<#s-"
    "IBCpL4t0*#'<#s-kMUpLmo('#*<#s-Mg$qL0[b)#.<#s-K#@qL(+o(#9<#s-TfNrL*7+)#RFCA4<+85#A#x%#>c_7#Wgh7#XOc##Z6a'M2qC:#_IY##jgS(M:97;##[wm/hSJ=#0WS=#"
    "/nw6/vx+>#;j[+M'S<j107WP&rYul&Is?D*C3pi'u%7/(cH]f1Kphc)'c/)*v7D_&'pa]+Dx3',^Hsr$:Y@A4lLIb49)$,2HR9;6(PSV6c_v?0A'uD#$PI@#gwu+#B0DG#/^1?#2e.-#"
    "9sRH#;jC?#W]$0#VkHK#[&`?#)U]vLNqnN#4WR@#smi4#A%8P#/wU?#):6&M:IPT#e^[@#*F0:#WX^U#<O^`4X&W<#%3&X#t:P>#nc]=#7AcY#SDH_##x0WHMe2v#Te%/1/`L;$T0jV$"
    "Lfkr-3x-s$L$J8%UU)207:eS%VN+p%MVou,;RE5&TTbP&lYiu5?k&m&hDC2'^$ai0C-^M'm`$j'U%PV-GE>/(kfZJ(m>6A4K^uf(`P;,)l):D3OvUG)G@?>#-(35&@q^9MM)P[&3P2i:"
    "n8O2(<U(^#0OQv$*W=_/pqJM'EKcf(M&%)*UV<A+^1TY,Z(x5/'5>##/Mc##7f1$#?(V$#G@%%#OXI%#Wqn%#`3=&#scjjE4bg[M'./)Cegx.#P*`5/@-2iFMx4`MHS<)DZIV-#m*`5/"
    "W4+7D]mnFMTnD;$<u%fGc5^e$;N4fGc=1)*?MI&,8xG`-%)_e$N^eoD'Tif1]DYd3l]gh2=r_e$axLe$j9IM9oNZJ;U#pG3Ue`e$#lMe$.o+5A1(=2CW5P)4nWae$MNXcH)]frHO%VpJ"
    ":GViF0Kbe$%j'##UTj-$-^1G%iNcf(C_GF%L]qr$QwTe$LDuu#r?_k+(ftA#n.%@'*x#Z$=@ge$3vie$C=GR*+=vV%8*jW%[&F>#_PX(j$c0^#.SGs-fLbGM4?VhLX9sZ#0Gg;-2Ag;-"
    "H6]Y-xe=R*B:GR*mai'8/Q>f$1jU_&`0w##bvK'#:,3)#Z7p*#)Oi,#IZO.#jf60#4rs1#T'Z3#u2A5#?>(7#dX3wpAK<`sBjr&#05<5A.oO&#lZ#<-ZR8n.+If5#HB)=-F[#<-X[#<-"
    "w$9gLW7'C-)CQxLg+p+MB<$##E'x/N9#;1N#/M1N)S.2NM;3,#*h]j-B>UF%)f2LGaU&m&v;PG2iC%#>RZ7_#cL1v#?/5##6sarLgpS%#7[->)//RqLgW)v#L5`P-AHToL3K.IM<'2hL"
    "8w$aN^b2v#uD9TR3SmY#OJuI;J;1^#9F`@-uQ@x-Y<7#MSQ?>#'AlY#jGqhL(5+bbXjp=c],QucaD2Vde]i7eiuIoem7+PfqOb1guhBig#+$Jh'CZ+iQZ*##4(Fk4.5ZV$&aE%tpJ/]t"
    "a.&:)J2>>#U/1O=v%ffL.LD/#7oYlLG=?>#6X`=-O(xU.@lb.#QhI:.f%*)#$Z:@BZ.vlLbmGoLkL>gLLLH##f2'C-L>Ib3o5>##^8q'#*<G##=.`$#W3X]#d4I*.gAeqLOY&d#9TGs-"
    "mGnqL&A2pLX/<$#%L$iLFwL^#KG`t-kfaRMZwtRMW_4rLSrJd#;Bg;-xU9C-XkF?-UkF?-VkF?-Uk'W.(2G>#L7T;-cEPh%fF5Dk4oF&#e`DPgZWhPq37)=-ZH`t-r<k(NXCH>#X+MP-"
    "qC3Y.;L.%#XH1v1@O4T%#S-g#k`?g#olQg#sxdg#w.wg#%;3h#)GEh#-SWh#1`jh#5l&i#9x8i#=.Ki#mkMuLIjx8#)&:K*NF6##dLOgL`o+)#cGuY#4M$g#D9P>#M#x%#*<KE<p61^#"
    ",IvD-SmHpL>;-##4SGs-6cGlLLF$##Lc/*#cx?qL&XPgLM:-##M@FV.)AcY#'8,8.SlRfLAR7IM0-nxOKxSfLS1x(#mNKC-=E4v-nbg[M:lQ##w>/=#hMA=#1UG-M2uQlLNCH>#+qX?-"
    "DEf>-<?f>-qn?2M=s-A-SodX.6qw@#'X4?-^Qx>--x_5/+Bic#-2Z/=W&3eH&NhPM+lj#.e.eQM2ecgLR]mY#/t@-#o4d;-ARM=-%tkU%/WSY,Zt$##Q4:DEci_oId/[]+vc$2K#$*>P"
    "$)=>,N#BSR<OF`W=+uu,Mte(a[[[%b]?VV-(nda$n>q:m/hf7n0#TS.4jY8Al*:@BGP+(fd)Zfr)^UH*ei,)<owx+MC8`^#S/1/#w^nI-f,E6.$c.NM-(<$#p6,</0f($#Z;1pLH#DHM"
    ".L&[#C0]-#k$uk12####;r2`#h>?_#r>gkLKDEG#$A,LM(,v&#Bl8*#R4`$#W.m<-;.r5/[$),#B3SnLVndY#3-DG#6?%)$97`c#5C1^#/v-A-H31[-1xOk+:NrA#m=Ps/WelF#%Xnw#"
    ")n'=55XR@#)@.@#mS0^#il*]#fhRd#x>j`#mN#<-f5T;-M5T;-CA^01E`7H#FBaD#@6X]#O5`T.$sn)$o<DiLx*bxbC@E8A;F5F%MK0F%xpFM0xI&)*$5.;?ERFG)aKGG)M-_`*.822'"
    "$=CW.eK1;6[#>)4lwF59HkhP0DFP8/lQvf;bLfS8m+r]56,]P^1l9_8oPbuPD%v-$GI,+#Vu:$#)jZ(#/Q]N/?Ub&#nTsJM>`XjL(]7(#ohG<-u).m/'.pE#b5$C#SO#<-TbYs-093jL"
    "wl(B#O?<jL)+4`#fB></YfmC#kabOM*XP9#']T:#5XG+#r[c'v51vlL@9wlL=J<,#jvboL5pS0#W$R0#4H#/vPAv3#.%f%#J/3)#X:YD3g]9G;;KY`3e#6A4)&ll/-S9;-FaIp/WknEI"
    "_N.58*F)REKau(3w7T;-<.KV-)MXR-<(_4.Eun/N1:kJOJ*/>-FCIbN=hHM9mOgxO6Qb>?Y_%x0d-?YY[iT>mbDJfh=@eWnLhDv#'iHD<gTnY#c(>BGDE0Vmwsf]=B&c^>bkoo@w?>GM"
    "[iX>HJE1:)m,mi'Lwbo@YHno.m_4R*,t[i9Cf65&w9+kb/<WY,t.C_/>x`f1[lk0Mgxs,#da%-#$`<rL?^x0#dvm(#ogFoL`gA%#sP<5$qxs$Oe-tV$>h'>c9awENFYtlA+/u-$?NA=#"
    "]o5s-[>IcM7?V'#24X2_;>^$P?U&%#,[s-$2N,F%d#0^OYKx(#s(<%vR`]=#xYlS.oax9vY7T;-P7T;-p*m<-i5T;-*5T;-S(m<-jO#<-)r:T.@&W<#k5T;-#)m<-'l1p.gR*j'oRO?%"
    "K,nF#6-DG#.5T;-3ZlS.MLR[#h]Du$,(_#$lDLq8MYaJ2,LU0$K_28.kT_;.nLL)OOm3FNlh=DWnMO]Fb^)aF<x-F%m^g7n/`6/$v-[0#H,BkL'Mg-#Nd.-#AMpV-H<rE[8ZlEIcr^#$"
    "#sX`O>)W+r%]20$8nVV-&]P8.i#DD*vr^#$634,Mdsw%FFR@/$FMao@.QLJ(hq'm9Rw.p[$M@_#nms%c63il^LqjYd0F629`6w>Q:&';Q%pk-$WPl-$o'x/$DCYcMVXm-$Fw.Yu7^`?%"
    "U]sG#t+6X$#Dx%uM@j-?i:d9MwfK#$)kTO.XhRd#lqb594gjuP05P$M+nsmLwNF)#qsA.$7Qv9)@I6wgcfK#$'B:p$,uB^#p&mmS8':*#)tA.$;2.>>/qx(3nuCG2/_kf(@t^Ee63WY,"
    "vw)/1W/SV-Kgnr-6s(d*6g5K)O)OS.]Wip/=*ru,@n$.-jfP_/u4[b/A$&F.2o`<@BgOD<*>3#5NePJC%mK>?#*<,<$Ko`=7hjV@o$,p8&8)s@J(RML3X5,EF6qfDrdd/;a2'#GhME?Q"
    "5t;;Q(wt`35Jl-$Cx?.$'_X]YMiuoJ7FYF%SvG_&]lmY6;Fg.$AXbw'ag7R3#rWw9?+5R*/4'.?2#5&>*vtA>W,.GM[mvuQj@HJVnfi(th.hiU'BMS]tFbxu/5'/`T-c`Ob)WVR0&+2_"
    "w1SDtUl1/1Q8@Mht9fDX&B:V?=^E>HA-gre;Ke]GP_JGNGd_lgXQ$#Q(J`SA5FTJDYB(&PvW9>ZC<58ft1TV-ebI_&U<qk45Y2F%'lm-$7_a.$*OF_&5@n-$5qxF%TK&##hbo:)DnBlb"
    "uGdw'DL5nbHZqERGb+RE7M*.?SHuRE5P$Z?K(Z<_(@v;%1/#hM^?aV$8o_V$d:t.C0T7VZ)6&##FE?,Z5N)##";
}

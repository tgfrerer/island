#include "bitonic_merge_sort_example_app.h"

#include "le_window/le_window.h"
#include "le_renderer/le_renderer.h"

#include "le_pipeline_builder/le_pipeline_builder.h"
#include "le_ui_event/le_ui_event.h"
#include "le_pixels/le_pixels.h"
#include "le_log/le_log.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE // vulkan clip space is from 0 to 1
#define GLM_FORCE_RIGHT_HANDED      // glTF uses right handed coordinate system, and we're following its lead.
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include <iostream>
#include <memory>
#include <sstream>
#include <vector>
#include <stdlib.h> // for random

struct pixels_data_t {
	le_resource_handle_t handle;
	uint32_t             w;                 // let's make sure that w is a power of 2
	uint32_t             h;                 // make sure that h is a power of 2
	uint32_t             num_channels;      // channels per pixel - by default let's have rgba, that's 4.
	uint32_t             bytes_per_channel; // bytes per channel - that's 4 for float channels
	bool                 unsorted;          // true means this buffer needs to be re-initialised.
};

// State of playback if we're slow-mo'ing
struct slow_mo_t {
	uint32_t seen_iterations = 0; // tracks progress through algorithm, if we're visualising progress
	int32_t  delay           = 0; // zero means slow-mo disabled.
};

enum DataSourceType : uint8_t {
	eNoise,
	eImage,
};

struct bitonic_merge_sort_example_app_o {
	le::Window     window;
	le::Renderer   renderer;
	uint64_t       frame_counter = 0;
	glm::vec2      mouse_pos;
	pixels_data_t *pixels_data;
	std::string    dropped_image_path;
	slow_mo_t      slow_mo;
	DataSourceType data_source_type; // whether data should come from random noise, or a loaded image.
	bool           source_dirty;     // whether source needs an update
};

typedef bitonic_merge_sort_example_app_o app_o;

// ----------------------------------------------------------------------

static void app_initialize() {
	le::Window::init();
};

// ----------------------------------------------------------------------

static void app_terminate() {
	le::Window::terminate();
};

// ----------------------------------------------------------------------

static bitonic_merge_sort_example_app_o *bitonic_merge_sort_example_app_create() {
	auto app = new ( bitonic_merge_sort_example_app_o );

	le::Window::Settings settings;
	settings
	    .setWidth( 1024 )
	    .setHeight( 512 )
	    .setTitle( "Island // BitonicMergeSortExampleApp" );

	// Create a new window
	app->window.setup( settings );

	app->renderer.setup( le::RendererInfoBuilder( app->window ).build() );

	app->pixels_data                    = new pixels_data_t{};
	app->pixels_data->handle            = LE_BUF_RESOURCE( "sort_data" );
	app->pixels_data->w                 = 1024;
	app->pixels_data->h                 = 512;
	app->pixels_data->num_channels      = 1;
	app->pixels_data->bytes_per_channel = 4;

	app->pixels_data->unsorted = false;

	app->source_dirty = true;

	app->slow_mo.seen_iterations = 0;
	app->slow_mo.delay           = 2;

	app->data_source_type = DataSourceType::eNoise;

	return app;
}

// ----------------------------------------------------------------------

static void bitonic_merge_sort_example_app_destroy( bitonic_merge_sort_example_app_o *self ) {
	delete self->pixels_data;
	delete ( self ); // deletes camera
}

// ----------------------------------------------------------------------

static void app_process_ui_events( app_o *self ) {
	using namespace le_window;
	uint32_t         numEvents;
	LeUiEvent const *pEvents;
	window_i.get_ui_event_queue( self->window, &pEvents, numEvents );

	LeLog logger( "app" );

	std::vector<LeUiEvent> events{ pEvents, pEvents + numEvents };

	bool wantsToggle = false;

	for ( auto &event : events ) {
		switch ( event.event ) {
		case ( LeUiEvent::Type::eKey ): {
			auto &e = event.key;
			if ( e.action == LeUiEvent::ButtonAction::eRelease ) {
				if ( e.key == LeUiEvent::NamedKey::eF11 ) {
					wantsToggle ^= true;
				}
			}
			if ( e.action == LeUiEvent::ButtonAction::eRelease ) {
				if ( e.key == LeUiEvent::NamedKey::eSpace ) {
					self->pixels_data->unsorted   = true;
					self->data_source_type        = DataSourceType::eNoise;
					self->source_dirty            = true;
					self->slow_mo.seen_iterations = 0;
				} else if ( e.key == LeUiEvent::NamedKey::eUp ) {
					self->slow_mo.delay = ( self->slow_mo.delay + 1 > 10 ? 10 : self->slow_mo.delay + 1 );
					logger.info( "Slow-mo speed set to: %d", self->slow_mo.delay );
				} else if ( e.key == LeUiEvent::NamedKey::eDown ) {
					self->slow_mo.delay = ( self->slow_mo.delay - 1 < 0 ? 0 : self->slow_mo.delay - 1 );
					logger.info( "Slow-mo speed set to: %d", self->slow_mo.delay );
				}
			}
		} break;
		case ( LeUiEvent::Type::eCursorPosition ): {
			auto &e         = event.cursorPosition;
			self->mouse_pos = glm::vec2{ e.x, e.y };
			break;
		}
		case ( LeUiEvent::Type::eDrop ): {
			auto &e = event.drop;
			if ( e.paths_count ) {
				// only take the first path
				self->dropped_image_path      = e.paths_utf8[ 0 ];
				self->data_source_type        = DataSourceType::eImage;
				self->source_dirty            = true;
				self->slow_mo.seen_iterations = 0;
				logger.info( "Dropped filename: '%s'", e.paths_utf8[ 0 ] );
			}
		}
		default:
			// do nothing
			break;
		}
	}

	if ( wantsToggle ) {
		self->window.toggleFullscreen();
	}
}

// ----------------------------------------------------------------------

static bool pass_noise_setup( le_renderpass_o *renderpass_, void *user_data ) {
	le::RenderPass rp{ renderpass_ };
	auto           app = static_cast<app_o *>( user_data );

	if ( app->source_dirty && app->data_source_type == DataSourceType::eNoise ) {
		rp.useBufferResource( app->pixels_data->handle, { LE_BUFFER_USAGE_TRANSFER_DST_BIT } );
		app->pixels_data->unsorted   = true;
		app->slow_mo.seen_iterations = 0;
		app->source_dirty            = false;
		return true;
	}

	return false;
}

// ----------------------------------------------------------------------

static bool pass_upload_image_setup( le_renderpass_o *rp_, void *user_data ) {
	le::RenderPass rp{ rp_ };
	auto           app = static_cast<app_o *>( user_data );

	if ( app->source_dirty && app->data_source_type == DataSourceType::eImage && !app->dropped_image_path.empty() ) {
		rp.useBufferResource( app->pixels_data->handle, { LE_BUFFER_USAGE_STORAGE_BUFFER_BIT } );
		app->pixels_data->unsorted   = true;
		app->slow_mo.seen_iterations = 0;
		app->source_dirty            = false;
		return true;
	}

	return false;
}

// ----------------------------------------------------------------------

static bool pass_sort_setup( le_renderpass_o *rp_, void *user_data ) {
	auto           app = static_cast<app_o *>( user_data );
	le::RenderPass rp{ rp_ };

	if ( app->pixels_data->unsorted == true ) {
		rp.useBufferResource( app->pixels_data->handle, { LE_BUFFER_USAGE_STORAGE_BUFFER_BIT } );
		return true;
	}
	return false;
}

// ----------------------------------------------------------------------

static void pass_noise_execute( le_command_buffer_encoder_o *encoder_, void *user_data ) {
	le::Encoder encoder{ encoder_ };
	auto        app = static_cast<app_o *>( user_data );

	std::vector<uint32_t> buffer_initial_data;
	buffer_initial_data.reserve( app->pixels_data->w * app->pixels_data->h * app->pixels_data->num_channels );

	// srand( 10 ); // use the same random seed so that you can compare outputs and debug synchronisation issues.

	for ( uint32_t y = 0; y != app->pixels_data->h; y++ ) {
		for ( uint32_t x = 0; x != app->pixels_data->w; x++ ) {
			buffer_initial_data.push_back( rand() % UINT_MAX ); // R
		}
	}

	// We must make sure that there is exactly the number of bytest in the buffer that we expect.
	assert( buffer_initial_data.size() ==
	        app->pixels_data->num_channels *
	            app->pixels_data->h *
	            app->pixels_data->w );

	// Upload content to buffer.
	encoder.writeToBuffer( app->pixels_data->handle, 0, buffer_initial_data.data(), buffer_initial_data.size() * app->pixels_data->bytes_per_channel );
}

// ----------------------------------------------------------------------

static void pass_upload_image_execute( le_command_buffer_encoder_o *encoder_, void *user_data ) {
	auto         app = static_cast<app_o *>( user_data );
	static LeLog log( "app" );

	// we must load image from disk

	le::Pixels pixels( app->dropped_image_path.c_str(), 4 );

	if ( !pixels.isValid() ) {
		log.warn( "Could not load image '%s'", app->dropped_image_path.c_str() );
		app->dropped_image_path.clear();
		return;
	}

	// invariant: pixels must be valid

	le_pixels_info info = pixels.getInfo();

	if ( info.width * info.height < app->pixels_data->w * app->pixels_data->h ) {
		log.warn( "Could not load image '%s': Too small: w: %d, h: %d", app->dropped_image_path.c_str(), info.width, info.height );
		app->dropped_image_path.clear();

		app->pixels_data->unsorted = false;
		return;
	}

	// ---------| invariant: image was loaded, image is large enough.

	// we should find a way to scale our image to 1024x512 pixels.
	//
	// for now, we just grab that many pixels and be done with it, if the image is larger,
	// or complain that the image is not large enough, if it isn't.

	le::Encoder encoder{ encoder_ };

	uint32_t num_bytes = app->pixels_data->w * app->pixels_data->h * app->pixels_data->bytes_per_channel * app->pixels_data->num_channels;

	// upload pixels
	encoder.writeToBuffer( app->pixels_data->handle, 0, pixels.getData(), num_bytes );

	app->dropped_image_path.clear();
}

//----------------------------------------------------------------------

static void pass_sort_execute( le_command_buffer_encoder_o *encoder_, void *user_data ) {

	LeLog log( "app" );

	log.info( "running compute pass..." );

	le::Encoder encoder{ encoder_ };
	auto        app = static_cast<app_o *>( user_data );

	size_t n = app->pixels_data->w * app->pixels_data->h;

	size_t max_workgroup_size = 1024; // TODO: Calculate this based on *queried* hardware limits.
	size_t workgroup_size_x   = 1;

	// Adjust workgroup_size_x to get as close to max_workgroup_size as possible.
	if ( n < max_workgroup_size * 2 ) {
		workgroup_size_x = n / 2;
	} else {
		workgroup_size_x = max_workgroup_size;
	}

	// Tell shader our selected `workgroup_size_x`, which
	// will become the shader's `local_size_x`.
	//
	static std::string defines_str = []( uint32_t const &local_size_x ) -> std::string {
		std::ostringstream os;
		os << "LOCAL_SIZE_X=" << local_size_x;
		return os.str();
	}( workgroup_size_x );

	static auto pipeline =
	    LeComputePipelineBuilder( encoder.getPipelineManager() )
	        .setShaderStage(
	            LeShaderModuleBuilder( encoder.getPipelineManager() )
	                .setShaderStage( le::ShaderStage::eCompute )
	                .setSourceFilePath( "./local_resources/shaders/compute.glsl" )
	                .setSourceDefinesString( defines_str.c_str() )
	                .build() )
	        .build();

	struct Parameters {
		enum eAlgorithmVariant : uint32_t {
			eLocalBitonicMergeSortExample = 0,
			eLocalDisperse                = 1,
			eBigFlip                      = 2,
			eBigDisperse                  = 3,
		};
		uint32_t          h;
		eAlgorithmVariant algorithm;
	};

	Parameters params{};

	params.h = 0;

	encoder
	    .bindComputePipeline( pipeline )
	    .bindArgumentBuffer( LE_ARGUMENT_NAME( "SortData" ), app->pixels_data->handle );

	const uint32_t workgroup_count = n / ( workgroup_size_x * 2 );

	auto dispatch = [ & ]( uint32_t h ) {
		params.h = h;
		encoder
		    .setArgumentData( LE_ARGUMENT_NAME( "Parameters" ), &params, sizeof( params ) )
		    .dispatch( workgroup_count )
		    .bufferMemoryBarrier( { LE_PIPELINE_STAGE_COMPUTE_SHADER_BIT },
		                          { LE_PIPELINE_STAGE_COMPUTE_SHADER_BIT },
		                          { LE_ACCESS_SHADER_READ_BIT },
		                          app->pixels_data->handle );
	};

	auto local_bitonic_merge_sort_example = [ & ]( uint32_t h ) {
		params.algorithm = Parameters::eAlgorithmVariant::eLocalBitonicMergeSortExample;
		dispatch( h );
	};

	auto big_flip = [ & ]( uint32_t h ) {
		params.algorithm = Parameters::eAlgorithmVariant::eBigFlip;
		dispatch( h );
	};

	auto local_disperse = [ & ]( uint32_t h ) {
		params.algorithm = Parameters::eAlgorithmVariant::eLocalDisperse;
		dispatch( h );
	};

	auto big_disperse = [ & ]( uint32_t h ) {
		params.algorithm = Parameters::eAlgorithmVariant::eBigDisperse;
		dispatch( h );
	};

	if ( app->slow_mo.delay > 0 ) {
		// This branch only exists to visualise the sorting algorithm. For the fully
		// optimised implementation of the algorithm, take a look at the `else` branch.
		//
		uint32_t comparators = 0;

		if ( app->slow_mo.seen_iterations == 0 ) {
			int q                 = ceilf( log2( n ) );
			int total_comparators = q * ( q + 1 ) / 2;
			log.info( "Total number of steps: %d", total_comparators );
		}

		for ( uint32_t h = 2; h <= n; h *= 2 ) {
			if ( comparators++ == app->slow_mo.seen_iterations ) {

				if ( app->frame_counter % app->slow_mo.delay == 0 ) {
					big_flip( h );
					log.info( "step % 5d: big flip: % 5d", comparators, h );
					app->slow_mo.seen_iterations = comparators;
				}
				return;
			}

			for ( uint32_t hh = h / 2; hh > 1; hh /= 2 ) {
				if ( comparators++ == app->slow_mo.seen_iterations ) {
					if ( app->frame_counter % app->slow_mo.delay == 0 ) {
						big_disperse( hh );
						log.info( "step % 5d: disperse: % 5d", comparators, hh );
						app->slow_mo.seen_iterations = comparators;
					}
					return;
				}
			}
		}
	} else {

		// Fully optimised version of bitonic merge sort.
		// Uses workgroup local memory whenever possible.

		uint32_t h = workgroup_size_x * 2;
		assert( h <= n );
		assert( h % 2 == 0 );

		local_bitonic_merge_sort_example( h );
		// we must now double h, as this happens before every flip
		h *= 2;

		for ( ; h <= n; h *= 2 ) {
			big_flip( h );

			for ( uint32_t hh = h / 2; hh > 1; hh /= 2 ) {

				if ( hh <= workgroup_size_x * 2 ) {
					// We can fit all elements for a disperse operation into continuous shader
					// workgroup local memory, which means we can complete the rest of the
					// cascade using a single shader invocation.
					local_disperse( hh );
					break;
				} else {
					big_disperse( hh );
				}
			}
		}
	}

	// ----------| invariant: sorting algorithm has run to completion.

	app->pixels_data->unsorted = false;
	log.info( "sorted." );
}

// ----------------------------------------------------------------------
// Draw contents of buffer to screen
static void pass_draw_exec( le_command_buffer_encoder_o *encoder_, void *user_data ) {
	auto        app = static_cast<bitonic_merge_sort_example_app_o *>( user_data );
	le::Encoder encoder{ encoder_ };

	// Draw main scene

	static std::string defines_str = []( app_o *app ) -> std::string {
		std::ostringstream os;
		os << "BUF_W=" << app->pixels_data->w << ",BUF_H=" << app->pixels_data->h;
		return os.str();
	}( app );

	static auto pipelineFullscreenQuad =
	    LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
	        .addShaderStage(
	            LeShaderModuleBuilder( encoder.getPipelineManager() )
	                .setShaderStage( le::ShaderStage::eVertex )
	                .setSourceFilePath( "./local_resources/shaders/fullscreen.vert" )
	                .build() )
	        .addShaderStage(
	            LeShaderModuleBuilder( encoder.getPipelineManager() )
	                .setShaderStage( le::ShaderStage::eFragment )
	                .setSourceFilePath( "./local_resources/shaders/fullscreen.frag" )
	                .setSourceDefinesString( defines_str.c_str() )
	                .build() )
	        .build();

	encoder
	    .bindGraphicsPipeline( pipelineFullscreenQuad )
	    .bindArgumentBuffer( LE_ARGUMENT_NAME( "SortData" ), app->pixels_data->handle )
	    .draw( 4 );
}

// ----------------------------------------------------------------------

static bool bitonic_merge_sort_example_app_update( bitonic_merge_sort_example_app_o *self ) {

	// Polls events for all windows
	// Use `self->window.getUIEventQueue()` to fetch events.
	le::Window::pollEvents();

	if ( self->window.shouldClose() ) {
		return false;
	}

	// Process user interface events such as mouse, keyboard
	app_process_ui_events( self );

	le::RenderModule mainModule{};
	{
		auto pass_noise =
		    le::RenderPass( "initialize", LE_RENDER_PASS_TYPE_TRANSFER )
		        .setSetupCallback( self, pass_noise_setup )
		        .setExecuteCallback( self, pass_noise_execute );

		auto pass_upload_image =
		    le::RenderPass( "upload_image", LE_RENDER_PASS_TYPE_TRANSFER )
		        .setSetupCallback( self, pass_upload_image_setup )
		        .setExecuteCallback( self, pass_upload_image_execute );

		auto pass_compute =
		    le::RenderPass( "compute", LE_RENDER_PASS_TYPE_COMPUTE )
		        .setSetupCallback( self, pass_sort_setup )
		        .setExecuteCallback( self, pass_sort_execute );

		auto pass_draw =
		    le::RenderPass( "root", LE_RENDER_PASS_TYPE_DRAW )
		        .useBufferResource( self->pixels_data->handle, { LE_BUFFER_USAGE_STORAGE_BUFFER_BIT } )
		        .addColorAttachment( LE_SWAPCHAIN_IMAGE_HANDLE )
		        .setExecuteCallback( self, pass_draw_exec );

		mainModule
		    .addRenderPass( pass_noise )        // initialise buffer with noise data if requested
		    .addRenderPass( pass_upload_image ) // upload image data to buffer if requested
		    .addRenderPass( pass_compute )      // sort buffer if needed
		    .addRenderPass( pass_draw );        // draw current contents of buffer to screen

		// We must make sure that the engine knows how much space to allocate for our
		// pixels data buffer - this is why we explicitly declare this buffer resource:
		mainModule
		    .declareResource(
		        self->pixels_data->handle,
		        le::BufferInfoBuilder()
		            .setSize( self->pixels_data->bytes_per_channel *
		                      self->pixels_data->num_channels *
		                      self->pixels_data->w *
		                      self->pixels_data->h )
		            .build() );
	}

	self->renderer.update( mainModule );

	self->frame_counter++;

	return true; // keep app alive
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( bitonic_merge_sort_example_app, api ) {

	auto  bitonic_merge_sort_example_app_api_i = static_cast<bitonic_merge_sort_example_app_api *>( api );
	auto &bitonic_merge_sort_example_app_i     = bitonic_merge_sort_example_app_api_i->bitonic_merge_sort_example_app_i;

	bitonic_merge_sort_example_app_i.initialize = app_initialize;
	bitonic_merge_sort_example_app_i.terminate  = app_terminate;

	bitonic_merge_sort_example_app_i.create  = bitonic_merge_sort_example_app_create;
	bitonic_merge_sort_example_app_i.destroy = bitonic_merge_sort_example_app_destroy;
	bitonic_merge_sort_example_app_i.update  = bitonic_merge_sort_example_app_update;
}

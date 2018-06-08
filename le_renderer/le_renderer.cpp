#include "pal_api_loader/ApiRegistry.hpp"

#include "le_renderer/le_renderer.h"

#include "le_renderer/private/le_rendergraph.h"
#include "le_renderer/private/le_renderpass.h"
#include "le_renderer/private/le_command_buffer_encoder.h"

#include "le_backend_vk/le_backend_vk.h"
#include "le_swapchain_vk/le_swapchain_vk.h"

#include "le_renderer/private/hash_util.h"
#include "le_renderer/private/le_pipeline_types.h"

#include <iostream>
#include <iomanip>
#include <chrono>

#include <atomic>
#include <thread>
#include <queue>
#include <mutex>
#include <future>
#include <unordered_set>
#include <unordered_map>

using NanoTime = std::chrono::time_point<std::chrono::high_resolution_clock>;

// ----------------------------------------------------------------------

struct FrameData {

	enum class State : int64_t {
		eFailedClear    = -4,
		eFailedDispatch = -3,
		eFailedAcquire  = -2,
		eInitial        = -1,
		eCleared        = 0,
		eAcquired,
		eRecorded,
		eProcessed,
		eDispatched,
	};

	struct Meta {
		NanoTime time_acquire_frame_start;
		NanoTime time_acquire_frame_end;

		NanoTime time_process_frame_start;
		NanoTime time_process_frame_end;

		NanoTime time_record_frame_start;
		NanoTime time_record_frame_end;

		NanoTime time_dispatch_frame_start;
		NanoTime time_dispatch_frame_end;
	};

	State state = State::eInitial;

	le_graph_builder_o *graphBuilder = nullptr;

	std::unordered_map<uint64_t, le_renderer_api::ResourceInfo> localResources;
	std::unordered_map<uint64_t, le_renderer_api::ResourceInfo> externalResources;

	Meta meta;
};

// ----------------------------------------------------------------------

struct le_renderer_o {

	uint64_t    swapchainDirty = false;
	le::Backend backend;

	std::vector<FrameData> frames;
	size_t                 numSwapchainImages = 0;
	size_t                 currentFrameNumber = size_t( ~0 ); // ever increasing number of current frame

	std::vector<le_graphics_pipeline_state_o *> PSOs;

	le_renderer_o( le_backend_o *backend )
	    : backend( backend ) {
	}
};

// ----------------------------------------------------------------------

static le_renderer_o *
renderer_create( le_backend_o *backend ) {
	auto obj = new le_renderer_o( backend );
	return obj;
}

// ----------------------------------------------------------------------

static le_graphics_pipeline_state_o *
renderer_create_graphics_pipeline_state_object( le_renderer_o *self, le_graphics_pipeline_create_info_t const *pipeline_info ) {
	auto pso = new ( le_graphics_pipeline_state_o );

	// -- add shader modules to pipeline
	//
	// (shader modules are backend objects)
	pso->shaderModuleFrag = pipeline_info->shader_module_frag;
	pso->shaderModuleVert = pipeline_info->shader_module_vert;

	// TODO (pipeline): -- initialise pso based on pipeline info

	// TODO (pipeline): -- tell backend about the new pipeline state object
	static auto const &backend_i = ( *Registry::getApi<le_backend_vk_api>() ).vk_backend_i;
	// -- calculate hash based on contents of pipeline state object

	pso->hash = 0x0; // TODO: -- calculate hash for pipeline state based on create_info (state that's not related to shaders)

	self->PSOs.push_back( pso );
	return pso;
}

// ----------------------------------------------------------------------
/// \brief declare a shader module which can be used to create a pipeline
/// \returns a shader module handle, or nullptr upon failure
static le_shader_module_o *renderer_create_shader_module( le_renderer_o *self, char const *path, LeShaderType moduleType ) {
	static auto const &backend_i = ( *Registry::getApi<le_backend_vk_api>() ).vk_backend_i;
	return backend_i.create_shader_module( self->backend, path, moduleType );
}

// ----------------------------------------------------------------------

static void
renderer_setup( le_renderer_o *self ) {

	self->numSwapchainImages = self->backend.getNumSwapchainImages();

	self->frames.reserve( self->numSwapchainImages );

	static auto const &rendererApi   = *Registry::getApi<le_renderer_api>();
	static auto const &graphBuilderI = rendererApi.le_graph_builder_i;

	for ( size_t i = 0; i != self->numSwapchainImages; ++i ) {
		auto frameData         = FrameData();
		frameData.graphBuilder = graphBuilderI.create();
		self->frames.push_back( std::move( frameData ) );
	}

	self->currentFrameNumber = 0;
}

// ----------------------------------------------------------------------

static void renderer_clear_frame( le_renderer_o *self, size_t frameIndex ) {

	auto &frame = self->frames[ frameIndex ];

	if ( frame.state == FrameData::State::eCleared || frame.state == FrameData::State::eInitial ) {
		return;
	}

	// ----------| invariant: frame was not yet cleared

	// + ensure frame fence has been reached
	if ( frame.state == FrameData::State::eDispatched ||
	     frame.state == FrameData::State::eFailedDispatch ||
	     frame.state == FrameData::State::eFailedClear ) {

		bool result = self->backend.clearFrame( frameIndex );

		if ( result != true ) {
			frame.state = FrameData::State::eFailedClear;
			return;
		}
	}

	static auto &rendererApi   = *Registry::getApi<le_renderer_api>();
	static auto &graphBuilderI = rendererApi.le_graph_builder_i;

	graphBuilderI.reset( frame.graphBuilder );

	frame.state = FrameData::State::eCleared;
}

// ----------------------------------------------------------------------

static void renderer_record_frame( le_renderer_o *self, size_t frameIndex, le_render_module_o *module_ ) {

	auto &frame = self->frames[ frameIndex ];

	if ( frame.state != FrameData::State::eCleared && frame.state != FrameData::State::eInitial ) {
		return;
	}
	// ---------| invariant: Frame was previously acquired successfully.

	// record api-agnostic intermediate draw lists
	frame.meta.time_record_frame_start = std::chrono::high_resolution_clock::now();

	// TODO: implement record_frame
	// - resolve rendergraph: which passes do contribute?
	// - consolidate resources, synchronisation for resources
	//
	// For each render pass, call renderpass' render method, build intermediary command lists
	//

	static auto &rendererApi   = *Registry::getApi<le_renderer_api>();
	static auto &renderModuleI = rendererApi.le_render_module_i;
	static auto &graphBuilderI = rendererApi.le_graph_builder_i;

	// - build up dependencies for graph, create table of unique resources for graph

	// setup passes calls setup for all passes - this initalises virtual resources,
	// and stores their descriptors (information needed to allocate physical resources)
	renderModuleI.setup_passes( module_, frame.graphBuilder );

	graphBuilderI.build_graph( frame.graphBuilder );

	// TODO: allocate physical resources for frame-local data

	// at this point we know the resources which must be created for the frame.

	// Execute callbacks into main application for each renderpass,
	// build command lists per renderpass in intermediate, api-agnostic representation
	//
	graphBuilderI.execute_graph( frame.graphBuilder, frameIndex, self->backend );

	frame.meta.time_record_frame_end = std::chrono::high_resolution_clock::now();
	//std::cout << "renderer_record_frame: " << std::dec << std::chrono::duration_cast<std::chrono::duration<double,std::milli>>(frame.meta.time_record_frame_end-frame.meta.time_record_frame_start).count() << "ms" << std::endl;

	frame.state = FrameData::State::eRecorded;
}

// ----------------------------------------------------------------------

static const FrameData::State &renderer_acquire_backend_resources( le_renderer_o *self, size_t frameIndex ) {

	// ---------| invariant: There are frames to process.

	auto &frame = self->frames[ frameIndex ];

	frame.meta.time_acquire_frame_start = std::chrono::high_resolution_clock::now();

	if ( frame.state != FrameData::State::eRecorded ) {
		return frame.state;
	}

	// ----------| invariant: frame is either initial, or cleared.

	// TODO: update descriptor pool for this frame
	static auto &le_graph_builder_api = ( *Registry::getApi<le_renderer_api>() ).le_graph_builder_i;

	le_renderpass_o *passes          = nullptr;
	size_t           numRenderPasses = 0;

	le_graph_builder_api.get_passes( frame.graphBuilder, &passes, &numRenderPasses );

	auto acquireSuccess = self->backend.acquirePhysicalResources( frameIndex, passes, numRenderPasses );

	frame.meta.time_acquire_frame_end = std::chrono::high_resolution_clock::now();

	if ( acquireSuccess ) {
		frame.state = FrameData::State::eAcquired;

	} else {
		frame.state = FrameData::State::eFailedAcquire;
		// Failure most likely means that the swapchain was reset,
		// perhaps because of window resize.
		std::cout << "WARNING: Could not acquire frame." << std::endl;
		self->swapchainDirty = true;
	}

	return frame.state;
}

// ----------------------------------------------------------------------

static const FrameData::State &renderer_process_frame( le_renderer_o *self, size_t frameIndex ) {

	auto &frame = self->frames[ frameIndex ];

	if ( frame.state != FrameData::State::eAcquired ) {
		return frame.state;
	}
	// ---------| invariant: frame was previously recorded successfully

	frame.meta.time_process_frame_start = std::chrono::high_resolution_clock::now();

	// translate intermediate draw lists into vk command buffers, and sync primitives

	self->backend.processFrame( frameIndex );

	frame.meta.time_process_frame_end = std::chrono::high_resolution_clock::now();
	//std::cout << "renderer_process_frame: " << std::dec << std::chrono::duration_cast<std::chrono::duration<double,std::milli>>(frame.meta.time_process_frame_end-frame.meta.time_process_frame_start).count() << "ms" << std::endl;

	frame.state = FrameData::State::eProcessed;
	return frame.state;
}

// ----------------------------------------------------------------------

static void renderer_dispatch_frame( le_renderer_o *self, size_t frameIndex ) {

	auto &frame = self->frames[ frameIndex ];

	if ( frame.state != FrameData::State::eProcessed ) {
		return;
	}

	// ---------| invariant: frame was successfully processed previously

	frame.meta.time_dispatch_frame_start = std::chrono::high_resolution_clock::now();

	bool dispatchSuccessful = self->backend.dispatchFrame( frameIndex );

	frame.meta.time_dispatch_frame_end = std::chrono::high_resolution_clock::now();

	if ( dispatchSuccessful ) {
		frame.state = FrameData::State::eDispatched;
	} else {

		std::cout << "WARNING: Could not present frame." << std::endl;

		// Present was not successful -
		//
		// This most likely happened because the window surface has been resized.
		// We therefore attempt to reset the swapchain.

		frame.state = FrameData::State::eFailedDispatch;

		self->swapchainDirty = true;
	}
}

// ----------------------------------------------------------------------

static void renderer_update( le_renderer_o *self, le_render_module_o *module_ ) {

	static const auto &backend_i = Registry::getApi<le_backend_vk_api>()->vk_backend_i;

	const auto &index     = self->currentFrameNumber;
	const auto &numFrames = self->numSwapchainImages;

	// if necessary, recompile and reload shader modules
	backend_i.update_shader_modules( self->backend );

	// NOTE: think more about interleaving - ideally, each one of these stages
	// should be able to be executed in its own thread.
	//
	// At the moment, this is not possible, as acquisition might acquire more images
	// than available if there are more threads than swapchain images.

	renderer_clear_frame( self, ( index + 0 ) % numFrames );

	// generate an intermediary, api-agnostic, representation of the frame
	renderer_record_frame( self, ( index + 0 ) % numFrames, module_ );

	// acquire external backend resources such as swapchain
	// and create any temporary resources
	renderer_acquire_backend_resources( self, ( index + 1 ) % numFrames );

	// generate api commands for the frame
	renderer_process_frame( self, ( index + 1 ) % numFrames );

	renderer_dispatch_frame( self, ( index + 1 ) % numFrames );

	if ( self->swapchainDirty ) {
		// we must dispatch, then clear all previous dispatchable frames,
		// before recreating swapchain. This is because this frame
		// was processed against the vkImage object from the previous
		// swapchain.

		// TODO: check if you could just signal these fences so that the
		// leftover frames must not be dispatched.

		for ( size_t i = 0; i != self->frames.size(); ++i ) {
			if ( self->frames[ i ].state == FrameData::State::eProcessed ) {
				renderer_dispatch_frame( self, i );
				renderer_clear_frame( self, i );
			}
		}

		self->backend.resetSwapchain();
		self->swapchainDirty = false;
	}

	++self->currentFrameNumber;
}

// ----------------------------------------------------------------------

static void renderer_destroy( le_renderer_o *self ) {

	const auto &lastIndex = self->currentFrameNumber;
	for ( size_t i = 0; i != self->frames.size(); ++i ) {
		renderer_clear_frame( self, ( lastIndex + i ) % self->frames.size() );
	}

	self->frames.clear();

	// -- Delete any objects created dynamically

	for ( auto &pPso : self->PSOs ) {
		delete ( pPso );
	}
	self->PSOs.clear();

	delete self;
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_renderer_api( void *api_ ) {
	auto  le_renderer_api_i = static_cast<le_renderer_api *>( api_ );
	auto &le_renderer_i     = le_renderer_api_i->le_renderer_i;

	le_renderer_i.create                                = renderer_create;
	le_renderer_i.destroy                               = renderer_destroy;
	le_renderer_i.setup                                 = renderer_setup;
	le_renderer_i.update                                = renderer_update;
	le_renderer_i.create_graphics_pipeline_state_object = renderer_create_graphics_pipeline_state_object;
	le_renderer_i.create_shader_module                  = renderer_create_shader_module;

	Registry::loadLibraryPersistently( "libvulkan.so" );

	// register sub-components of this api
	register_le_rendergraph_api( api_ );
	register_le_renderpass_api( api_ );
	register_le_command_buffer_encoder_api( api_ );
}

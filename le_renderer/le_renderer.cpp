#include "pal_api_loader/ApiRegistry.hpp"

#include "le_renderer/le_renderer.h"

#include "le_backend_vk/le_backend_vk.h"
#include "le_swapchain_vk/le_swapchain_vk.h"

#include "le_renderer/private/hash_util.h"

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

#include "util/enkiTS/TaskScheduler.h"

using NanoTime = std::chrono::time_point<std::chrono::high_resolution_clock>;

#ifndef LE_RENDERER_MULTITHREADED
#	define LE_RENDERER_MULTITHREADED false
#endif

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

	size_t frameNumber = size_t( ~0 );
	Meta   meta;
};

// ----------------------------------------------------------------------

struct le_renderer_o {

	uint64_t      swapchainDirty = false;
	le_backend_o *backend;

	std::vector<FrameData> frames;
	size_t                 numSwapchainImages = 0;
	size_t                 currentFrameNumber = size_t( ~0 ); // ever increasing number of current frame

	enki::TaskScheduler g_TS = {};

	le_renderer_o( le_backend_o *backend )
	    : backend( backend ) {
	}
};

static void renderer_clear_frame( le_renderer_o *self, size_t frameIndex ); // ffdecl

// ----------------------------------------------------------------------

static le_renderer_o *
renderer_create( le_backend_o *backend ) {
	auto obj = new le_renderer_o( backend );

	if ( LE_RENDERER_MULTITHREADED ) {
		obj->g_TS.Initialize( 4 );
	}
	return obj;
}

// ----------------------------------------------------------------------

static void renderer_destroy( le_renderer_o *self ) {

	static auto const &graph_builder_i = Registry::getApi<le_renderer_api>()->le_graph_builder_i;

	const auto &lastIndex = self->currentFrameNumber;

	for ( size_t i = 0; i != self->frames.size(); ++i ) {
		auto index = ( lastIndex + i ) % self->frames.size();
		renderer_clear_frame( self, index );
		// -- FIXME: delete graph builders which we added in create
		// this is not elegant.
		graph_builder_i.destroy( self->frames[ index ].graphBuilder );
	}

	self->frames.clear();

	// -- Delete any objects created dynamically

	delete self;
}
// ----------------------------------------------------------------------

/// \brief Creates a pipeline state object on the backend
/// \returns an opaque handle to a pipeline state object
static le_graphics_pipeline_state_o *
renderer_create_graphics_pipeline_state_object( le_renderer_o *self, le_graphics_pipeline_create_info_t const *pipeline_info ) {

	static auto const &backend_i = Registry::getApi<le_backend_vk_api>()->vk_backend_i;

	return backend_i.create_graphics_pipeline_state_object( self->backend, pipeline_info );
}

// ----------------------------------------------------------------------
/// \brief declare a shader module which can be used to create a pipeline
/// \returns a shader module handle, or nullptr upon failure
static le_shader_module_o *renderer_create_shader_module( le_renderer_o *self, char const *path, LeShaderType moduleType ) {
	static auto const &backend_i = Registry::getApi<le_backend_vk_api>()->vk_backend_i;
	return backend_i.create_shader_module( self->backend, path, moduleType );
}

// ----------------------------------------------------------------------

static void
renderer_setup( le_renderer_o *self ) {

	static const auto &backend_i = Registry::getApi<le_backend_vk_api>()->vk_backend_i;

	self->numSwapchainImages = backend_i.get_num_swapchain_images( self->backend );

	self->frames.reserve( self->numSwapchainImages );

	static auto const &graph_builder_i = Registry::getApi<le_renderer_api>()->le_graph_builder_i;

	for ( size_t i = 0; i != self->numSwapchainImages; ++i ) {
		auto frameData         = FrameData();
		frameData.graphBuilder = graph_builder_i.create();
		self->frames.push_back( std::move( frameData ) );
	}

	self->currentFrameNumber = 0;
}

// ----------------------------------------------------------------------

static void renderer_clear_frame( le_renderer_o *self, size_t frameIndex ) {

	auto &frame = self->frames[ frameIndex ];

	static auto const &backend_i       = Registry::getApi<le_backend_vk_api>()->vk_backend_i;
	static auto const &graph_builder_i = Registry::getApi<le_renderer_api>()->le_graph_builder_i;

	if ( frame.state == FrameData::State::eCleared ) {
		return;
	}

	// ----------| invariant: frame was not yet cleared

	// + ensure frame fence has been reached
	if ( frame.state == FrameData::State::eDispatched ||
	     frame.state == FrameData::State::eFailedDispatch ||
	     frame.state == FrameData::State::eFailedClear ) {

		while ( false == backend_i.poll_frame_fence( self->backend, frameIndex ) ) {
			// Note: this call may block until the fence has been reached.
		};

		bool result = backend_i.clear_frame( self->backend, frameIndex );

		if ( result != true ) {
			frame.state = FrameData::State::eFailedClear;
			return;
		}
	}

	graph_builder_i.reset( frame.graphBuilder );

	//	std::cout << "CLEAR FRAME " << frameIndex << std::endl
	//	          << std::flush;

	frame.state = FrameData::State::eCleared;
}

// ----------------------------------------------------------------------

static void renderer_record_frame( le_renderer_o *self, size_t frameIndex, le_render_module_o *module_, size_t frameNumber ) {

	auto &frame       = self->frames[ frameIndex ];
	frame.frameNumber = frameNumber;

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

	//	std::cout << "RECORD FRAME " << frameIndex << std::endl
	//	          << std::flush;

	frame.state = FrameData::State::eRecorded;
}

// ----------------------------------------------------------------------

static const FrameData::State &renderer_acquire_backend_resources( le_renderer_o *self, size_t frameIndex ) {

	static auto const &backend_i = Registry::getApi<le_backend_vk_api>()->vk_backend_i;
	// ---------| invariant: There are frames to process.

	auto &frame = self->frames[ frameIndex ];

	frame.meta.time_acquire_frame_start = std::chrono::high_resolution_clock::now();

	if ( frame.state != FrameData::State::eRecorded ) {
		return frame.state;
	}

	// ----------| invariant: frame is either initial, or cleared.

	static auto &le_graph_builder_api = ( *Registry::getApi<le_renderer_api>() ).le_graph_builder_i;

	le_renderpass_o **passes          = nullptr;
	size_t            numRenderPasses = 0;

	le_graph_builder_api.get_passes( frame.graphBuilder, &passes, &numRenderPasses );

	auto acquireSuccess = backend_i.acquire_physical_resources( self->backend, frameIndex, passes, numRenderPasses );

	frame.meta.time_acquire_frame_end = std::chrono::high_resolution_clock::now();

	if ( acquireSuccess ) {
		frame.state = FrameData::State::eAcquired;
		//		std::cout << "ACQU FRAME " << frameIndex << std::endl
		//		          << std::flush;

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

	static auto const &backend_i = Registry::getApi<le_backend_vk_api>()->vk_backend_i;

	auto &frame = self->frames[ frameIndex ];

	if ( frame.state != FrameData::State::eAcquired ) {
		return frame.state;
	}
	// ---------| invariant: frame was previously recorded successfully

	frame.meta.time_process_frame_start = std::chrono::high_resolution_clock::now();

	// translate intermediate draw lists into vk command buffers, and sync primitives

	backend_i.process_frame( self->backend, frameIndex );

	frame.meta.time_process_frame_end = std::chrono::high_resolution_clock::now();
	//std::cout << "renderer_process_frame: " << std::dec << std::chrono::duration_cast<std::chrono::duration<double,std::milli>>(frame.meta.time_process_frame_end-frame.meta.time_process_frame_start).count() << "ms" << std::endl;

	//	std::cout << "PROCE FRAME " << frameIndex << std::endl
	//	          << std::flush;

	frame.state = FrameData::State::eProcessed;
	return frame.state;
}

// ----------------------------------------------------------------------

static void renderer_dispatch_frame( le_renderer_o *self, size_t frameIndex ) {

	static auto const &backend_i = Registry::getApi<le_backend_vk_api>()->vk_backend_i;
	auto &             frame     = self->frames[ frameIndex ];

	if ( frame.state != FrameData::State::eProcessed ) {
		return;
	}

	// ---------| invariant: frame was successfully processed previously

	frame.meta.time_dispatch_frame_start = std::chrono::high_resolution_clock::now();

	bool dispatchSuccessful = backend_i.dispatch_frame( self->backend, frameIndex );

	frame.meta.time_dispatch_frame_end = std::chrono::high_resolution_clock::now();

	if ( dispatchSuccessful ) {
		frame.state = FrameData::State::eDispatched;
		//		std::cout << "DISP FRAME " << frameIndex << std::endl
		//		          << std::flush;

	} else {

		std::cout << "WARNING: Could not present frame " << std::dec << frame.frameNumber << std::endl;

		// Present was not successful -
		//
		// This most likely happened because the window surface has been resized.
		// We therefore attempt to reset the swapchain.

		frame.state = FrameData::State::eFailedDispatch;

		self->swapchainDirty = true;
	}
}

static void render_tasks( le_renderer_o *renderer, size_t frameIndex ) {
	// acquire external backend resources such as swapchain
	// and create any temporary resources
	renderer_acquire_backend_resources( renderer, frameIndex );

	// generate api commands for the frame
	renderer_process_frame( renderer, frameIndex );

	renderer_dispatch_frame( renderer, frameIndex );
}

static void clear_task( le_renderer_o *renderer, size_t frameIndex ) {
	renderer_clear_frame( renderer, frameIndex );
}

struct RenderTask : public enki::ITaskSet {
	size_t         frameIndex;
	le_renderer_o *renderer;
	virtual void   ExecuteRange( enki::TaskSetPartition range, uint32_t threadnum ) override {
		render_tasks( renderer, frameIndex );
	}
	virtual ~RenderTask() = default;
};

struct RecordTask : public enki::ITaskSet {
	size_t              frameIndex;
	le_renderer_o *     renderer;
	le_render_module_o *module;
	virtual void        ExecuteRange( enki::TaskSetPartition range, uint32_t threadnum ) override {
	}
	virtual ~RecordTask() = default;
};

struct ClearTask : public enki::ITaskSet {
	size_t         frameIndex;
	le_renderer_o *renderer;
	virtual void   ExecuteRange( enki::TaskSetPartition range, uint32_t threadnum ) override {
		clear_task( renderer, frameIndex );
	}
	virtual ~ClearTask() = default;
};

// ----------------------------------------------------------------------

// returns a unique resource handle for a resource
static LeResourceHandle renderer_declare_resource( le_renderer_o *self, LeResourceType type ) {
	static const auto &backend_i = Registry::getApi<le_backend_vk_api>()->vk_backend_i;
	return backend_i.declare_resource( self->backend, type );
}

// ----------------------------------------------------------------------

static LeResourceHandle renderer_get_backbuffer_resource( le_renderer_o *self ) {
	static const auto &backend_i = Registry::getApi<le_backend_vk_api>()->vk_backend_i;
	return backend_i.get_backbuffer_resource( self->backend );
}

// ----------------------------------------------------------------------

static void renderer_update( le_renderer_o *self, le_render_module_o *module_ ) {

	static const auto &backend_i = Registry::getApi<le_backend_vk_api>()->vk_backend_i;

	const auto &index     = self->currentFrameNumber;
	const auto &numFrames = self->frames.size();

	// If necessary, recompile and reload shader modules
	// - this must be complete before the record_frame step

	backend_i.update_shader_modules( self->backend );

	if ( LE_RENDERER_MULTITHREADED ) {
		// use task system (experimental)

		//		std::cout << "RENDERER UPDATE" << std::endl
		//		          << std::endl
		//		          << std::flush;

		ClearTask clearTask;
		clearTask.renderer   = self;
		clearTask.frameIndex = ( index + 1 ) % numFrames;
		self->g_TS.AddTaskSetToPipe( &clearTask );

		RenderTask renderTask;
		renderTask.renderer   = self;
		renderTask.frameIndex = ( index + 2 ) % numFrames;
		self->g_TS.AddTaskSetToPipe( &renderTask );

		// we record on the main thread.
		renderer_record_frame( self, ( index + 0 ) % numFrames, module_, self->currentFrameNumber ); // generate an intermediary, api-agnostic, representation of the frame

		self->g_TS.WaitforTaskSet( &renderTask );
		self->g_TS.WaitforTaskSet( &clearTask );

	} else {

		// render on the main thread

		renderer_record_frame( self, ( index + 0 ) % numFrames, module_, self->currentFrameNumber ); // generate an intermediary, api-agnostic, representation of the frame
		render_tasks( self, ( index + 2 ) % numFrames );
		renderer_clear_frame( self, ( index + 1 ) % numFrames ); // wait for frame to come back (important to do this last, as it may block...)
	}

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
			} else if ( self->frames[ i ].state != FrameData::State::eDispatched ) {
				renderer_clear_frame( self, i );
			}
		}

		backend_i.reset_swapchain( self->backend );

		self->swapchainDirty = false;
	}

	++self->currentFrameNumber;
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
	le_renderer_i.declare_resource                      = renderer_declare_resource;
	le_renderer_i.get_backbuffer_resource               = renderer_get_backbuffer_resource;

	// register sub-components of this api
	register_le_rendergraph_api( api_ );

	register_le_command_buffer_encoder_api( api_ );
}

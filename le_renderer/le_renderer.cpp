#include "pal_api_loader/ApiRegistry.hpp"

#include "le_renderer/le_renderer.h"

#include "le_renderer/private/le_rendergraph.h"
#include "le_renderer/private/le_renderpass.h"
#include "le_renderer/private/le_command_buffer_encoder.h"

#include "le_backend_vk/le_backend_vk.h"
#include "le_swapchain_vk/le_swapchain_vk.h"
#include "vulkan/vulkan.hpp"

#include <iostream>
#include <iomanip>
#include <chrono>

#include <atomic>
#include <thread>
#include <queue>
#include <mutex>
#include <future>
#include <unordered_set>

using NanoTime = std::chrono::time_point<std::chrono::high_resolution_clock>;


struct le_renderer_resource_o {
	    le::ResourceType sType;
		uint32_t transient;
		uint64_t size;
};

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

	State                          state                    = State::eInitial;

	std::unique_ptr<le::GraphBuilder> graphBuilder;

	Meta meta;
};

// ----------------------------------------------------------------------

struct le_renderer_o {

	uint64_t       swapchainDirty  = false;
	le::Backend    backend;

	std::vector<FrameData> frames;
	size_t                 numSwapchainImages = 0;
	size_t                 currentFrameNumber = size_t( ~0 ); // ever increasing number of current frame

	std::unordered_set<le_renderer_resource_o*> resources; // all persistent resources we retain across frames

	le_renderer_o( le_backend_o* backend )
	    : backend( backend ){
	}
};

// ----------------------------------------------------------------------

static le_renderer_o *renderer_create( le_backend_o *backend ) {
	auto obj = new le_renderer_o( backend );
	return obj;
}

// ----------------------------------------------------------------------

static le_renderer_resource_o* renderer_create_resource(le_renderer_o* self, const le::ResourceType& s_type_, uint64_t size, bool transient=false){

	auto resource = new le_renderer_resource_o();

	resource->sType     = s_type_;
	resource->size      = size;
	resource->transient = transient;

//	self->backend.createResource

	return resource;
}

// ----------------------------------------------------------------------

static void renderer_destroy_resource(le_renderer_o* self, le_renderer_resource_o* resource_){
	if (resource_){
		if (!resource_->transient){
			self->resources.erase(resource_);
			// TODO: release resource on the backend
			// self->backend.release_resource(resource_,self->currentFrameNumber);
		}
		delete(resource_);
	}
}

// ----------------------------------------------------------------------

static void renderer_setup( le_renderer_o *self ) {

	vk::CommandPoolCreateInfo commandPoolCreateInfo;

	self->numSwapchainImages = self->backend.getNumSwapchainImages();

	self->frames.reserve( self->numSwapchainImages );

	for ( size_t i = 0; i != self->numSwapchainImages; ++i ) {
		auto frameData = FrameData();


		frameData.graphBuilder = std::make_unique<le::GraphBuilder>();
		self->frames.push_back( std::move( frameData ) );

	}

	self->currentFrameNumber = 0;

}

// ----------------------------------------------------------------------

static void renderer_clear_frame( le_renderer_o *self, size_t frameIndex ) {

	auto &frame  = self->frames[ frameIndex ];

	if (frame.state == FrameData::State::eCleared || frame.state==FrameData::State::eInitial){
		return;
	}

	// ----------| invariant: frame was not yet cleared

	// + ensure frame fence has been reached
	if ( frame.state == FrameData::State::eDispatched ||
	     frame.state == FrameData::State::eFailedDispatch ||
	     frame.state == FrameData::State::eFailedClear ) {

		bool result = self->backend.clearFrame(frameIndex);

		if (result != true){
			frame.state = FrameData::State::eFailedClear;
			return ;
		}
	}

	frame.graphBuilder->reset();

	frame.state = FrameData::State::eCleared;
}

// ----------------------------------------------------------------------

static void renderer_record_frame(le_renderer_o* self, size_t frameIndex, le_render_module_o * module_){

	auto &frame = self->frames[ frameIndex ];

	if (frame.state != FrameData::State::eCleared && frame.state != FrameData::State::eInitial){
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

	static auto  rendererApi   = *Registry::getApi<le_renderer_api>();
	static auto &renderModuleI = rendererApi.le_render_module_i;

	// - build up dependencies for graph, create table of unique resources for graph

	renderModuleI.setup_passes(module_, *frame.graphBuilder);

	frame.graphBuilder->buildGraph();

	// Execute callbacks into main application for each renderpass,
	// build command lists per renderpass in intermediate, api-agnostic representation
	//
	frame.graphBuilder->executeGraph(frameIndex, self->backend);

	frame.meta.time_record_frame_end   = std::chrono::high_resolution_clock::now();
	//std::cout << "renderer_record_frame: " << std::dec << std::chrono::duration_cast<std::chrono::duration<double,std::milli>>(frame.meta.time_record_frame_end-frame.meta.time_record_frame_start).count() << "ms" << std::endl;

	frame.state = FrameData::State::eRecorded;
}

// ----------------------------------------------------------------------

static const FrameData::State& renderer_acquire_swapchain_image(le_renderer_o* self, size_t frameIndex){

	// ---------| invariant: There are frames to process.

	auto &frame = self->frames[ frameIndex ];

	frame.meta.time_acquire_frame_start = std::chrono::high_resolution_clock::now();

	if ( frame.state != FrameData::State::eRecorded ) {
		return frame.state;
	}

	// ----------| invariant: frame is either initial, or cleared.

	// TODO: update descriptor pool for this frame

	auto acquireSuccess = self->backend.acquireSwapchainImage(frameIndex);

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

static const FrameData::State& renderer_process_frame( le_renderer_o *self, size_t frameIndex ) {

	auto &frame = self->frames[ frameIndex ];

	if (frame.state != FrameData::State::eAcquired){
		return frame.state;
	}
	// ---------| invariant: frame was previously recorded successfully

	frame.meta.time_process_frame_start = std::chrono::high_resolution_clock::now();

	// translate intermediate draw lists into vk command buffers, and sync primitives

	self->backend.processFrame(frameIndex, *frame.graphBuilder);

	frame.meta.time_process_frame_end = std::chrono::high_resolution_clock::now();
	//std::cout << "renderer_process_frame: " << std::dec << std::chrono::duration_cast<std::chrono::duration<double,std::milli>>(frame.meta.time_process_frame_end-frame.meta.time_process_frame_start).count() << "ms" << std::endl;

	frame.state = FrameData::State::eProcessed;
	return frame.state;
}


// ----------------------------------------------------------------------

static void renderer_dispatch_frame( le_renderer_o *self, size_t frameIndex) {


	auto &frame = self->frames[ frameIndex ];

	if (frame.state != FrameData::State::eProcessed){
		return;
	}

	// ---------| invariant: frame was successfully processed previously

	frame.meta.time_dispatch_frame_start = std::chrono::high_resolution_clock::now();

	bool dispatchSuccessful = self->backend.dispatchFrame(frameIndex);

	frame.meta.time_dispatch_frame_end = std::chrono::high_resolution_clock::now();

	if (dispatchSuccessful){
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

static void renderer_update( le_renderer_o *self, le_render_module_o * module_ ) {

	const auto &index     = self->currentFrameNumber;
	const auto &numFrames = self->numSwapchainImages;

	// TODO: think more about interleaving - ideally, each one of these stages
	// should be able to be executed in its own thread.
	//
	// At the moment, this is not possible, as acquisition might acquire more images
	// than available if there are more threads than swapchain images.

	renderer_clear_frame            ( self, ( index + 0 ) % numFrames );

	// generate an intermediary, api-agnostic, representation of the frame
	renderer_record_frame           ( self, ( index + 0 ) % numFrames, module_ );

	renderer_acquire_swapchain_image( self, ( index + 1 ) % numFrames );

	// generate api commands for the frame
	renderer_process_frame          ( self, ( index + 1 ) % numFrames );

	renderer_dispatch_frame         ( self, ( index + 1 ) % numFrames );


	if (self->swapchainDirty){
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

	++ self->currentFrameNumber;
}

// ----------------------------------------------------------------------

static void renderer_destroy( le_renderer_o *self ) {

	const auto &lastIndex = self->currentFrameNumber;
	for ( size_t i = 0; i != self->frames.size(); ++i ) {
		renderer_clear_frame( self, ( lastIndex + i) % self->frames.size() );
	}

	self->frames.clear();

	// TODO: make sure backend gets destroyed.

	delete self;
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_renderer_api( void *api_ ) {
	auto  le_renderer_api_i = static_cast<le_renderer_api *>( api_ );
	auto &le_renderer_i     = le_renderer_api_i->le_renderer_i;

	le_renderer_i.create           = renderer_create;
	le_renderer_i.destroy          = renderer_destroy;
	le_renderer_i.setup            = renderer_setup;
	le_renderer_i.update           = renderer_update;
	le_renderer_i.create_resource  = renderer_create_resource;
	le_renderer_i.destroy_resource = renderer_destroy_resource;

	Registry::loadLibraryPersistently( "libvulkan.so" );

	// register sub-components of this api
	register_le_rendergraph_api(api_);
	register_le_renderpass_api(api_);
	register_le_command_buffer_encoder_api(api_);

}

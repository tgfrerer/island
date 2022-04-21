#include "le_core.h"
#include "le_hash_util.h" // fixme-we shouldn't do that.

#include "le_renderer.h"

#include "le_backend_vk.h"
#include "le_swapchain_vk.h"

#include <iostream>
#include <iomanip>
#include <chrono>
#include <vector>
#include "assert.h"
#include <mutex>
#include <unordered_map>
#include <algorithm>
#include <string>
#include <cstring> // for memcpy

#include "private/le_resource_handle_t.inl"

const uint64_t LE_RENDERPASS_MARKER_EXTERNAL = hash_64_fnv1a_const( "rp-external" );

using NanoTime = std::chrono::time_point<std::chrono::high_resolution_clock>;

#include "le_jobs.h"

#ifndef LE_MT
#	define LE_MT 0
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

	le_rendergraph_o* rendergraph = nullptr;

	size_t frameNumber = size_t( ~0 );
	Meta   meta;
};

struct le_texture_handle_t {
	std::string debug_name;
};

struct le_texture_handle_store_t {
	std::unordered_multimap<std::string, le_texture_handle_t> texture_handles;
	std::mutex                                                mtx;
};

struct le_resource_handle_store_t {
	std::unordered_multimap<le_resource_handle_data_t, le_resource_handle_t, le_resource_handle_data_hash> resource_handles;
	std::mutex                                                                                             mtx;
};

static le_texture_handle_store_t* get_texture_handle_library( bool erase = false ) {

	static le_texture_handle_store_t* texture_handle_library = nullptr;

	if ( erase ) {
		delete texture_handle_library;
		void** texture_handle_library_ptr = le_core_produce_dictionary_entry( hash_64_fnv1a_const( "texture_handle_library" ) );
		*texture_handle_library_ptr       = nullptr; // null pointer stored in global store
		texture_handle_library            = nullptr; // null pointer stored local store
		return nullptr;                              // return nullptr
	}

	if ( texture_handle_library ) {
		return texture_handle_library;
	}

	// ----------| Invariant: not yet in local store
	void** texture_handle_library_ptr = le_core_produce_dictionary_entry( hash_64_fnv1a_const( "texture_handle_library" ) );

	if ( *texture_handle_library_ptr ) {
		// Found in global store
		texture_handle_library = static_cast<le_texture_handle_store_t*>( *texture_handle_library_ptr );
	} else {
		// Not yet available in global store - create & make available
		texture_handle_library      = new le_texture_handle_store_t{};
		*texture_handle_library_ptr = texture_handle_library;
	}

	return texture_handle_library;
}

static le_resource_handle_store_t* get_resource_handle_library( bool erase = false ) {
	static le_resource_handle_store_t* resource_handle_library = nullptr;

	if ( erase ) {
		delete resource_handle_library;
		void** resource_handle_library_ptr = le_core_produce_dictionary_entry( hash_64_fnv1a_const( "resource_handle_library" ) );
		*resource_handle_library_ptr       = nullptr; // null pointer stored in global store
		resource_handle_library            = nullptr; // null pointer stored in local store
		return nullptr;                               // return nullptr
	}

	if ( resource_handle_library ) {
		return resource_handle_library;
	}

	// ----------| Invariant: not yet in local store
	void** resource_handle_library_ptr = le_core_produce_dictionary_entry( hash_64_fnv1a_const( "resource_handle_library" ) );

	if ( *resource_handle_library_ptr ) {
		// Found in global store
		resource_handle_library = static_cast<le_resource_handle_store_t*>( *resource_handle_library_ptr );
	} else {
		// Not yet available in global store - create & make available.
		resource_handle_library      = new le_resource_handle_store_t();
		*resource_handle_library_ptr = resource_handle_library;
	}

	return resource_handle_library;
}

// ----------------------------------------------------------------------

struct le_renderer_o {
	uint64_t      swapchainDirty = false;
	le_backend_o* backend        = nullptr; // Owned, created in setup

	std::vector<FrameData> frames;
	size_t                 numSwapchainImages = 0;
	size_t                 currentFrameNumber = size_t( ~0 ); // ever increasing number of current frame
	le_renderer_settings_t settings;
};

static void renderer_clear_frame( le_renderer_o* self, size_t frameIndex ); // ffdecl

// ----------------------------------------------------------------------

static le_renderer_o* renderer_create() {
	auto obj = new le_renderer_o();

#if ( LE_MT > 0 )
	le_jobs::initialize( LE_MT );
#endif

	return obj;
}

// ----------------------------------------------------------------------

// creates a new handle if no name was given, or given name was not found in list of current handles.
static le_texture_handle renderer_produce_texture_handle( char const* maybe_name ) {

	// lock handle library for reading/writing
	static le_texture_handle_store_t* texture_handle_library = get_texture_handle_library();
	std::scoped_lock                  lock( texture_handle_library->mtx );

	le_texture_handle handle;

	if ( maybe_name ) {
		// if a string was given, search for multimap and see if we can find something.
		auto it = texture_handle_library->texture_handles.find( maybe_name );
		if ( it == texture_handle_library->texture_handles.end() ) {
			// not found, insert a new element
			handle = &texture_handle_library->texture_handles.emplace( maybe_name, le_texture_handle_t( { maybe_name } ) )->second;
		} else {
			// found, return a pointer to the found element
			handle = &it->second;
		}
	} else {
		// no name given: handle is set to address of newly inserted element
		// As this is a multimap, there can be any number of textures with the same
		// key "unnamed" in the map.
		handle = &texture_handle_library->texture_handles.emplace( "unnamed", le_texture_handle_t{} )->second;
	}

	// handle is a pointer to the element in the container, and as such it is
	// guaranteed to stay valid, even through rehashes of the texture_handles
	// container, because that's a guarantee that maps give us in c++, until
	// the element gets erased.

	return handle;
}

// ----------------------------------------------------------------------

static char const* texture_handle_get_name( le_texture_handle texture ) {
	if ( texture && !texture->debug_name.empty() ) {
		return texture->debug_name.c_str();
	} else {
		return nullptr;
	}
}

// creates a new resource if no name was given, or given name was not found in list of current handles.
le_resource_handle renderer_produce_resource_handle(
    char const*           maybe_name,
    LeResourceType const& resource_type,
    uint8_t               num_samples      = 0,
    uint8_t               flags            = 0,
    uint16_t              index            = 0,
    le_resource_handle    reference_handle = nullptr ) {

	static le_resource_handle_store_t* resource_handle_library = get_resource_handle_library();
	// lock handle library for reading/writing
	std::scoped_lock lock( resource_handle_library->mtx );

	le_resource_handle handle;

	le_resource_handle_data_t* p_data = new le_resource_handle_data_t{};
	p_data->flags                     = flags;
	p_data->num_samples               = num_samples;
	p_data->reference_handle          = reference_handle;
	p_data->type                      = resource_type;
	p_data->index                     = index;

	if ( maybe_name && maybe_name[ 0 ] != '\0' ) {
		memcpy( p_data->debug_name, maybe_name, sizeof( p_data->debug_name ) );
		// if a string was given, search for multimap and see if we can find something.
		auto it = resource_handle_library->resource_handles.find( *p_data );
		if ( it == resource_handle_library->resource_handles.end() ) {
			// not found, insert a new element
			handle = &resource_handle_library->resource_handles.emplace( *p_data, le_resource_handle_t{ p_data } )->second;
		} else {
			// found, return a pointer to the found element
			handle = &it->second;
			delete ( p_data );
		}
	} else {
		// no name given: handle is set to address of newly inserted element
		// As this is a multimap, there can be any number of textures with the same
		// key "unnamed" in the map.
		handle = &resource_handle_library->resource_handles.emplace( *p_data, le_resource_handle_t{ p_data } )->second;
	}

	// handle is a pointer to the element in the container, and as such it is
	// guaranteed to stay valid, even through rehashes of the resource_handle_library
	// container, because that's a guarantee that maps give us in c++, until
	// the element gets erased.

	return handle;
}

static le_img_resource_handle renderer_produce_img_resource_handle( char const* maybe_name, uint8_t num_samples,
                                                                    le_img_resource_handle reference_handle, uint8_t flags ) {
	return static_cast<le_img_resource_handle>(
	    renderer_produce_resource_handle( maybe_name, LeResourceType::eImage, num_samples, flags, 0,
	                                      static_cast<le_resource_handle>( reference_handle ) ) );
}

static le_buf_resource_handle renderer_produce_buf_resource_handle( char const* maybe_name, uint8_t flags, uint16_t index ) {
	return static_cast<le_buf_resource_handle>( renderer_produce_resource_handle( maybe_name, LeResourceType::eBuffer, 0, flags, index ) );
}

static le_tlas_resource_handle renderer_produce_tlas_resource_handle( char const* maybe_name ) {
	return static_cast<le_tlas_resource_handle>( renderer_produce_resource_handle( maybe_name, LeResourceType::eRtxTlas ) );
}

static le_blas_resource_handle renderer_produce_blas_resource_handle( char const* maybe_name ) {
	return static_cast<le_blas_resource_handle>( renderer_produce_resource_handle( maybe_name, LeResourceType::eRtxBlas ) );
}
// ----------------------------------------------------------------------

static void renderer_destroy( le_renderer_o* self ) {

	using namespace le_renderer; // for rendergraph_i

	const auto& lastIndex = self->currentFrameNumber;

	for ( size_t i = 0; i != self->frames.size(); ++i ) {
		auto index = ( lastIndex + i ) % self->frames.size();
		renderer_clear_frame( self, index );
		// -- FIXME: delete graph builders which we added in create.
		// This is not elegant.
		rendergraph_i.destroy( self->frames[ index ].rendergraph );
	}

	self->frames.clear();

	// Delete texture handle library
	get_texture_handle_library( false );

	{
		le_resource_handle_store_t* resource_handle_library = get_resource_handle_library();
		if ( resource_handle_library ) {
			// we must deallocate manually allocated data for resource handles
			for ( auto& e : resource_handle_library->resource_handles ) {
				delete ( e.second.data );
			}
			// Delete static pointer to resource handle library
			get_resource_handle_library( true );
		}
	}

	if ( self->backend ) {
		// Destroy the backend, as it is owned by the renderer
		using namespace le_backend_vk;
		vk_backend_i.destroy( self->backend );
		self->backend = nullptr;
	}

#if ( LE_MT > 0 )
	le_jobs::terminate();
#endif

	delete self;
}

// ----------------------------------------------------------------------

static le_rtx_blas_info_handle renderer_create_rtx_blas_info_handle( le_renderer_o* self, le_rtx_geometry_t* geometries, uint32_t geometries_count, LeBuildAccelerationStructureFlags const* flags ) {
	using namespace le_backend_vk;
	return vk_backend_i.create_rtx_blas_info( self->backend, geometries, geometries_count, flags );
}

// ----------------------------------------------------------------------

static le_rtx_tlas_info_handle renderer_create_rtx_tlas_info_handle( le_renderer_o* self, uint32_t instances_count, LeBuildAccelerationStructureFlags const* flags ) {
	using namespace le_backend_vk;
	return vk_backend_i.create_rtx_tlas_info( self->backend, instances_count, flags );
}

// ----------------------------------------------------------------------

static le_backend_o* renderer_get_backend( le_renderer_o* self ) {
	return self->backend;
}

// ----------------------------------------------------------------------

static le_pipeline_manager_o* renderer_get_pipeline_manager( le_renderer_o* self ) {
	using namespace le_backend_vk;
	return vk_backend_i.get_pipeline_cache( self->backend );
}

// ----------------------------------------------------------------------

static void renderer_setup( le_renderer_o* self, le_renderer_settings_t const& settings ) {

	// We store swapchain settings with the renderer so that we can pass
	// backend a permanent pointer to it.

	self->settings = settings;
	{
		// Set up the backend

		using namespace le_backend_vk;
		self->backend = vk_backend_i.create();

		le_backend_vk_settings_t backend_settings{};
		backend_settings.pSwapchain_settings          = self->settings.swapchain_settings;
		backend_settings.num_swapchain_settings       = self->settings.num_swapchain_settings;
		backend_settings.requestedDeviceExtensions    = settings.requested_device_extensions;
		backend_settings.numRequestedDeviceExtensions = settings.requested_device_extensions_count;

#if ( LE_MT > 0 )
		backend_settings.concurrency_count = LE_MT;
#endif

		vk_backend_i.setup( self->backend, &backend_settings );
	}

	using namespace le_backend_vk;

	// Since backend setup implicitly sets up the swapchain,
	// we may now query the available number of swapchain images.
	self->numSwapchainImages = vk_backend_i.get_num_swapchain_images( self->backend );

	using namespace le_renderer; // for rendergraph_i
	self->frames.reserve( self->numSwapchainImages );

	for ( size_t i = 0; i != self->numSwapchainImages; ++i ) {
		auto frameData        = FrameData();
		frameData.rendergraph = rendergraph_i.create();
		self->frames.push_back( std::move( frameData ) );
	}

	self->currentFrameNumber = 0;
}

// ----------------------------------------------------------------------

static le_renderer_settings_t const* renderer_get_settings( le_renderer_o* self ) {
	return &self->settings;
}

// ----------------------------------------------------------------------

static void renderer_clear_frame( le_renderer_o* self, size_t frameIndex ) {

	auto& frame = self->frames[ frameIndex ];

	using namespace le_backend_vk; // for vk_bakend_i
	using namespace le_renderer;   // for rendergraph_i

	if ( frame.state == FrameData::State::eCleared ) {
		return;
	}

	// ----------| invariant: frame was not yet cleared

	// + ensure frame fence has been reached
	if ( frame.state == FrameData::State::eDispatched ||
	     frame.state == FrameData::State::eFailedDispatch ||
	     frame.state == FrameData::State::eFailedClear ) {

		while ( false == vk_backend_i.poll_frame_fence( self->backend, frameIndex ) ) {
			// Note: this call may block until the fence has been reached.
#if ( LE_MT > 0 )
			le_jobs::yield();
#endif
		}

		bool result = vk_backend_i.clear_frame( self->backend, frameIndex );

		if ( result != true ) {
			frame.state = FrameData::State::eFailedClear;
			return;
		}
	}

	rendergraph_i.reset( frame.rendergraph );

	//	std::cout << "CLEAR FRAME " << frameIndex << std::endl
	//	          << std::flush;

	frame.state = FrameData::State::eCleared;
}

// ----------------------------------------------------------------------

static void renderer_record_frame( le_renderer_o* self, size_t frameIndex, le_render_module_o* module_, size_t frameNumber ) {

	// High-level
	// - resolve rendergraph: which render passes do contribute?
	// - consolidate resources, synchronisation for resources
	// - For each render pass, call renderpass' render method, build intermediary command lists

	auto& frame       = self->frames[ frameIndex ];
	frame.frameNumber = frameNumber;

	if ( frame.state != FrameData::State::eCleared && frame.state != FrameData::State::eInitial ) {
		return;
	}

	// ---------| invariant: Frame was previously acquired successfully.

	frame.meta.time_record_frame_start = std::chrono::high_resolution_clock::now();

	// - build up dependencies for graph, create table of unique resources for graph

	// setup passes calls `setup` callback on all passes - this initalises virtual resources,
	// and stores their descriptors (information needed to allocate physical resources)
	//
	using namespace le_renderer; // for render_module_i, rendergraph_i
	render_module_i.setup_passes( module_, frame.rendergraph );

	// find out which renderpasses contribute, only add contributing render passes to
	// frameBuilder
	rendergraph_i.build( frame.rendergraph, frameNumber );

	// Execute callbacks into main application for each render pass,
	// build command lists per render pass in intermediate, api-agnostic representation
	//
	rendergraph_i.execute( frame.rendergraph, frameIndex, self->backend );

	frame.meta.time_record_frame_end = std::chrono::high_resolution_clock::now();

	frame.state = FrameData::State::eRecorded;
	// std::cout << "renderer_record_frame: " << std::dec << std::chrono::duration_cast<std::chrono::duration<double, std::milli>>( frame.meta.time_record_frame_end - frame.meta.time_record_frame_start ).count() << "ms" << std::endl;

	//	std::cout << "RECORD FRAME " << frameIndex << std::endl
	//	          << std::flush;
}

// ----------------------------------------------------------------------

static const FrameData::State& renderer_acquire_backend_resources( le_renderer_o* self, size_t frameIndex ) {

	using namespace le_backend_vk; // for vk_bakend_i
	using namespace le_renderer;   // for rendergraph_i

	// ---------| invariant: There are frames to process.

	auto& frame = self->frames[ frameIndex ];

	frame.meta.time_acquire_frame_start = std::chrono::high_resolution_clock::now();

	if ( frame.state != FrameData::State::eRecorded ) {
		return frame.state;
	}

	// ----------| invariant: frame is either initial, or cleared.

	le_renderpass_o** passes          = nullptr;
	size_t            numRenderPasses = 0;

	rendergraph_i.get_passes( frame.rendergraph, &passes, &numRenderPasses );

	le_resource_handle const* declared_resources;
	le_resource_info_t const* declared_resources_infos;
	size_t                    declared_resources_count = 0;

	rendergraph_i.get_declared_resources(
	    frame.rendergraph,
	    &declared_resources,
	    &declared_resources_infos,
	    &declared_resources_count );

	auto acquireSuccess =
	    vk_backend_i.acquire_physical_resources(
	        self->backend,
	        frameIndex,
	        passes,
	        numRenderPasses,
	        declared_resources,
	        declared_resources_infos,
	        declared_resources_count );

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

static const FrameData::State& renderer_process_frame( le_renderer_o* self, size_t frameIndex ) {

	using namespace le_backend_vk; // for vk_bakend_i

	auto& frame = self->frames[ frameIndex ];

	if ( frame.state != FrameData::State::eAcquired ) {
		return frame.state;
	}
	// ---------| invariant: frame was previously recorded successfully

	frame.meta.time_process_frame_start = std::chrono::high_resolution_clock::now();

	// translate intermediate draw lists into vk command buffers, and sync primitives

	vk_backend_i.process_frame( self->backend, frameIndex );

	frame.meta.time_process_frame_end = std::chrono::high_resolution_clock::now();
	// std::cout << "renderer_process_frame: " << std::dec << std::chrono::duration_cast<std::chrono::duration<double,std::milli>>(frame.meta.time_process_frame_end-frame.meta.time_process_frame_start).count() << "ms" << std::endl;

	//	std::cout << "PROCE FRAME " << frameIndex << std::endl
	//	          << std::flush;

	frame.state = FrameData::State::eProcessed;
	return frame.state;
}

// ----------------------------------------------------------------------

static void renderer_dispatch_frame( le_renderer_o* self, size_t frameIndex ) {

	using namespace le_backend_vk; // for vk_backend_i
	auto& frame = self->frames[ frameIndex ];

	if ( frame.state != FrameData::State::eProcessed ) {
		return;
	}

	// ---------| invariant: frame was successfully processed previously

	frame.meta.time_dispatch_frame_start = std::chrono::high_resolution_clock::now();

	bool dispatchSuccessful = vk_backend_i.dispatch_frame( self->backend, frameIndex );

	frame.meta.time_dispatch_frame_end = std::chrono::high_resolution_clock::now();

	if ( dispatchSuccessful ) {
		frame.state = FrameData::State::eDispatched;
		//		std::cout << "DISP FRAME " << frameIndex << std::endl
		//		          << std::flush;

	} else {

		std::cout << "NOTICE: Present failed on frame: " << std::dec << frame.frameNumber << std::endl
		          << std::flush;

		// Present was not successful -
		//
		// This most likely happened because the window surface has been resized.
		// We therefore attempt to reset the swapchain.

		frame.state = FrameData::State::eFailedDispatch;

		self->swapchainDirty = true;
	}
}

// ----------------------------------------------------------------------

static uint32_t renderer_get_swapchain_count( le_renderer_o* self ) {
	using namespace le_backend_vk;
	return vk_backend_i.get_swapchain_count( self->backend );
}

// ----------------------------------------------------------------------

static le_img_resource_handle renderer_get_swapchain_resource( le_renderer_o* self, uint32_t index ) {
	using namespace le_backend_vk;
	return vk_backend_i.get_swapchain_resource( self->backend, index );
}

// ----------------------------------------------------------------------

static void renderer_get_swapchain_extent( le_renderer_o* self, uint32_t index, uint32_t* p_width, uint32_t* p_height ) {
	using namespace le_backend_vk;
	vk_backend_i.get_swapchain_extent( self->backend, index, p_width, p_height );
}

// ----------------------------------------------------------------------

static void renderer_update( le_renderer_o* self, le_render_module_o* module_ ) {

	using namespace le_backend_vk;

	const auto& index     = self->currentFrameNumber;
	const auto& numFrames = self->frames.size();

	// If necessary, recompile and reload shader modules
	// - this must be complete before the record_frame step

#if ( LE_MT > 0 )

	auto update_shader_modules_fun = []( void* backend ) {
		vk_backend_i.update_shader_modules( static_cast<le_backend_o*>( backend ) );
	};

	le_jobs::job_t      j{ update_shader_modules_fun, self->backend };
	le_jobs::counter_t* shader_counter;

	le_jobs::run_jobs( &j, 1, &shader_counter );

#else
	vk_backend_i.update_shader_modules( self->backend );
#endif

	if ( LE_MT > 0 ) {
#if ( LE_MT > 0 )
		// use task system (experimental)

		struct frame_params_t {
			le_renderer_o* renderer;
			size_t         frame_index;
		};

		struct record_params_t {
			le_renderer_o*      renderer;
			size_t              frame_index;
			le_render_module_o* module;
			size_t              current_frame_number;
			le_jobs::counter_t* shader_counter;
		};

		auto record_frame_fun = []( void* param_ ) {
			auto p = static_cast<record_params_t*>( param_ );
			// generate an intermediary, api-agnostic, representation of the frame

			le_jobs::wait_for_counter_and_free( p->shader_counter, 0 );
			renderer_record_frame( p->renderer, p->frame_index, p->module, p->current_frame_number );
		};

		auto process_frame_fun = []( void* param_ ) {
			auto p = static_cast<frame_params_t*>( param_ );
			// acquire external backend resources such as swapchain
			// and create any temporary resources
			renderer_acquire_backend_resources( p->renderer, p->frame_index );
			// generate api commands for the frame
			renderer_process_frame( p->renderer, p->frame_index );
			// send api commands to GPU queue for processing
			renderer_dispatch_frame( p->renderer, p->frame_index );
		};

		auto clear_frame_fun = []( void* param_ ) {
			auto p = static_cast<frame_params_t*>( param_ );
			renderer_clear_frame( p->renderer, p->frame_index );
		};

		le_jobs::job_t jobs[ 3 ];

		record_params_t record_frame_params;
		record_frame_params.renderer             = self;
		record_frame_params.frame_index          = ( index + 0 ) % numFrames;
		record_frame_params.module               = module_;
		record_frame_params.current_frame_number = self->currentFrameNumber;
		record_frame_params.shader_counter       = shader_counter;

		frame_params_t process_frame_params;
		process_frame_params.renderer    = self;
		process_frame_params.frame_index = ( index + 2 ) % numFrames;

		frame_params_t clear_frame_params;
		clear_frame_params.renderer    = self;
		clear_frame_params.frame_index = ( index + 1 ) % numFrames;

		jobs[ 0 ] = { process_frame_fun, &process_frame_params };
		jobs[ 1 ] = { clear_frame_fun, &clear_frame_params };
		jobs[ 2 ] = { record_frame_fun, &record_frame_params };

		le_jobs::counter_t* counter;

		assert( self->backend );

		le_jobs::run_jobs( jobs, 3, &counter );

		// we could theoretically do some more work on the main thread here...

		le_jobs::wait_for_counter_and_free( counter, 0 );
#endif
	} else {

		// render on the main thread

		renderer_record_frame( self, ( index + 0 ) % numFrames, module_, self->currentFrameNumber ); // generate an intermediary, api-agnostic, representation of the frame

		// acquire external backend resources such as swapchain
		// and create any temporary resources
		renderer_acquire_backend_resources( self, ( index + 2 ) % numFrames );

		// generate api commands for the frame
		renderer_process_frame( self, ( index + 2 ) % numFrames );

		renderer_dispatch_frame( self, ( index + 2 ) % numFrames );

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

		vk_backend_i.reset_failed_swapchains( self->backend );

		self->swapchainDirty = false;
	}

	++self->currentFrameNumber;
}

// ----------------------------------------------------------------------

static le_resource_info_t get_default_resource_info_for_image() {
	le_resource_info_t res;

	res.type = LeResourceType::eImage;
	{
		auto& img                   = res.image;
		img                         = {};
		img.flags                   = 0;
		img.format                  = le::Format::eUndefined;
		img.arrayLayers             = 1;
		img.extent.width            = 0;
		img.extent.height           = 0;
		img.extent.depth            = 1;
		img.extent_from_pass.width  = 0;
		img.extent_from_pass.height = 0;
		img.extent_from_pass.depth  = 1;
		img.usage                   = { LE_IMAGE_USAGE_SAMPLED_BIT };
		img.mipLevels               = 1;
		img.sample_count_log2       = 0; // 0 means 1, as (1 << 0 == 1)
		img.imageType               = le::ImageType::e2D;
		img.tiling                  = le::ImageTiling::eOptimal;
		img.samplesFlags            = 0;
	}

	return res;
}

// ----------------------------------------------------------------------

static le_resource_info_t get_default_resource_info_for_buffer() {
	le_resource_info_t res;
	res.type         = LeResourceType::eBuffer;
	res.buffer.size  = 0;
	res.buffer.usage = { LE_BUFFER_USAGE_TRANSFER_DST_BIT };
	return res;
}

extern void register_le_rendergraph_api( void* api );            // in le_rendergraph.cpp
extern void register_le_command_buffer_encoder_api( void* api ); // in le_command_buffer_encoder.cpp

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_renderer, api ) {
	auto  le_renderer_api_i = static_cast<le_renderer_api*>( api );
	auto& le_renderer_i     = le_renderer_api_i->le_renderer_i;

	le_renderer_i.create                 = renderer_create;
	le_renderer_i.destroy                = renderer_destroy;
	le_renderer_i.setup                  = renderer_setup;
	le_renderer_i.update                 = renderer_update;
	le_renderer_i.get_settings           = renderer_get_settings;
	le_renderer_i.get_swapchain_count    = renderer_get_swapchain_count;
	le_renderer_i.get_swapchain_resource = renderer_get_swapchain_resource;
	le_renderer_i.get_swapchain_extent   = renderer_get_swapchain_extent;
	le_renderer_i.get_pipeline_manager   = renderer_get_pipeline_manager;
	le_renderer_i.get_backend            = renderer_get_backend;

	le_renderer_i.produce_texture_handle  = renderer_produce_texture_handle;
	le_renderer_i.texture_handle_get_name = texture_handle_get_name;

	le_renderer_i.create_rtx_blas_info = renderer_create_rtx_blas_info_handle;
	le_renderer_i.create_rtx_tlas_info = renderer_create_rtx_tlas_info_handle;

	auto& helpers_i = le_renderer_api_i->helpers_i;

	helpers_i.get_default_resource_info_for_buffer = get_default_resource_info_for_buffer;
	helpers_i.get_default_resource_info_for_image  = get_default_resource_info_for_image;

	le_renderer_i.produce_img_resource_handle  = renderer_produce_img_resource_handle;
	le_renderer_i.produce_buf_resource_handle  = renderer_produce_buf_resource_handle;
	le_renderer_i.produce_tlas_resource_handle = renderer_produce_tlas_resource_handle;
	le_renderer_i.produce_blas_resource_handle = renderer_produce_blas_resource_handle;

	// register sub-components of this api
	register_le_rendergraph_api( api );

	register_le_command_buffer_encoder_api( api );
}

#include "le_core.h"
#include "le_hash_util.h" // fixme-we shouldn't do that.

#include "le_renderer.h"

#include "le_backend_vk.h"
#include "le_swapchain_vk.h"
#include "le_swapchain_khr.h"
#include "le_log.h"
#include "le_debug_print_text.h"

// #include <iostream>
// #include <iomanip>
#include <vector>
#include "assert.h"
#include <mutex>
#include <unordered_map>
// #include <algorithm>
#include <string>
#include <cstring> // for memcpy
// #include <bitset>
#include <forward_list>

struct le_resource_handle_data_t {
	le_resource_handle handle     = {}; // original handle -- so that we can compare versions
	std::string        debug_name = {}; // space for 47 chars + \0
};

#include "private/le_renderer/le_rendergraph.h"

#include "le_tracy.h"

const uint64_t LE_RENDERPASS_MARKER_EXTERNAL = hash_64_fnv1a_const( "rp-external" );

#include "le_jobs.h"

#ifndef LE_MT
#	define LE_MT 0
#endif

// ----------------------------------------------------------------------
// ffdecl.
static le_swapchain_handle renderer_add_swapchain( le_renderer_o* self, le_swapchain_settings_t const* settings );
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

	State state = State::eInitial;

	le_rendergraph_o* rendergraph = nullptr;

	size_t frameNumber = size_t( ~0 );
};

struct le_texture_handle_t {
	std::string debug_name;
};

struct le_texture_handle_store_t {
	std::unordered_multimap<std::string, le_texture_handle_t> texture_handles;
	std::mutex                                                mtx;
};

struct le_resource_handle_store_t {
	// this is the ultimate owner of the handle and the handle owns its data.
	std::vector<le_resource_handle_data_t*> resource_handles;
	std::mutex                         mtx;
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


// -----------

template <typename H, typename I, typename D>
class bindless_resources_store_t {

	// Currently, the maximum number of possible handles is 1048575 --
	// we allow 256 different versions.
	// we keep 8 bits to encode the type of a resource

	inline H make_handle( uint64_t idx, uint64_t version ) {
		assert( idx <= ( 0xfffff ) );
		return reinterpret_cast<H>( ( uint64_t( idx ) << 12 ) | ( uint64_t( std::remove_pointer<H>::type::resource_type_id ) << 8 ) | ( uint64_t( version ) & 0xFF ) );
	};

  public:
	// Allocate a bindless resource and return the handle
	H allocate( I const* info ) {

		/*
		 *  H is a versioned handle that contains an
		 *  offset into the table of backend descriptors
		 *
		 *  the offset is mirrored by the backend, which keeps track of descriptors
		 *  in its per-resource-type descriptor tables currently, there is only a
		 *  descriptor table for bindless textures
		 *
		 *  this is how bindless works - you bind the full descriptor table, and
		 *  access the resource via it's offset into the descriptor table.
		 *
		 * NOTE that resource names in island  link to the same resource - even if
		 * a resource gets re-allocated (which may happen if an image gets resized)
		 *
		 * the backend keeps track of this, and will update descriptors accordingly,
		 * if a parent resource gets reallocated.
		 *
		 * ------- OPEN QUESTIONS: ---------------------------
		 *
		 * Q: what should we do if the source resource (say an image) gets freed?
		 *
		 * - in this case, any bindless resources referring to the freed object
		 *   should be considered invalid. this should happen in the backend
		 *   automatically where we update image views for example that have
		 *   been tainted once an image gets re-allocated or reloaded.
		 *
		 * - bindless resources might point at the old position, which may cause
		 *   use-after-free issues.
		 *
		 */

		uint32_t idx     = 0;
		uint8_t  version = 0;

		// we should probably lock the free list for the duration of this operation.

		if ( !this->bindless_resources_free_list.empty() ) {

			// if there are any elements on the free list, then we should re-use these
			// otherwise, we need to create a new element.
			auto last_handle = this->bindless_resources_free_list.front();

			idx     = last_handle->get_idx();
			version = last_handle->get_version();

			size_t num_el = this->bindless_resources.size();

			if ( idx >= num_el ) {
				assert( false && "idx cannot be larger than current number of elements in bindless textures" );
				return nullptr;
			}

			// ---------| invariant idx < num_el

			auto& data = this->bindless_resources.at( idx );
			assert( data.version == version );

			// If we were successfull, then we can remove this element from the front
			this->bindless_resources_free_list.pop_front();
			// we can unlock the free list now

			// Update data
			data.data = *info;

			// Update version
			version = ++data.version;

			// Mark this resource entry as tainted
			this->bindless_resource_updates.push_back( idx );

			return make_handle( idx, version );
		}

		// ---------| invariant: the free list is empty

		// if the free list is empty, we must add a new element to our bindless textures.

		version = 1;
		idx     = this->bindless_resources.size();
		this->bindless_resources.emplace_back( *info, version );

		// We use version so that we can test whether this descriptor is stale.
		// if we lookup a descriptor and the version in the handle does not match
		// the version in the data, then we know that the handle is stale.
		this->bindless_resource_updates.push_back( idx );
		return make_handle( idx, version );
	};

	// --------------

	typedef void ( *backend_push_fn_t )( le_backend_o* self, uint32_t frame_index, D const* const texture_data, size_t texture_data_count, uint32_t const* updated_indices, size_t updated_indices_count );

	void push_to_backend( le_backend_o* backend, uint32_t frame_index, backend_push_fn_t backend_push_fn ) {

		if ( this->bindless_resource_updates.empty() ) {
			return;
		}

		// ---------| invariant: there are updates to propagate

		backend_push_fn( backend, frame_index, this->bindless_resources.data(), this->bindless_resources.size(), this->bindless_resource_updates.data(), this->bindless_resource_updates.size() );

		// reset update list now that we have pushed it
		this->bindless_resource_updates.clear();
	}

	I const& get_data( le_bindless_resource_handle const& handle ) {
		assert( handle->get_type() == le_bindless_resource_type( std::remove_pointer<H>::type::resource_type_id ) ); // enforce that the handle is the correct type
		uint32_t idx   = handle->get_idx();
		auto&    entry = bindless_resources.at( idx );
		assert( entry.version == handle->get_version() && "version must match" );
		return entry.data;
	}

  private:
	std::forward_list<H>  bindless_resources_free_list; // list of resources that can be re-used: this gets populated by frame.clear()
	std::vector<D>        bindless_resources;           // each element's index corresponds to a descriptor index
	std::vector<uint32_t> bindless_resource_updates;    // idx (unique, sorted) of any bindless resources that have updates in the current frame
};

// ----------------------------------------------------------------------

struct le_renderer_o {
	le_backend_o* backend = nullptr; // Owned, created in setup

	bindless_resources_store_t<le_bindless_texture_handle, le_image_sampler_info_t, le_bindless_texture_data_t>          bindless_texture_store;
	bindless_resources_store_t<le_bindless_sampler_handle, le_sampler_info_t, le_bindless_sampler_data_t>                bindless_sampler_store;
	bindless_resources_store_t<le_bindless_storage_image_handle, le_image_view_info_t, le_bindless_storage_image_data_t> bindless_storage_image_store;

	le_resource_handle_store_t resource_handle_store;

	std::vector<FrameData>           frames;
	size_t                           backendDataFramesCount = 0;
	size_t                           currentFrameNumber     = 0;  // ever increasing number of current frame
	size_t                           last_recorded_frame_number = -1; // index into `frames` for last recorded frame (-1 if none recorded yet)
	le_renderer_settings_t           settings                   = {}; // initial settings for the renderer - these will be used on setup()
	le_swapchain_windowed_settings_t default_windowed_swapchain_setting;
};

static void renderer_clear_frame( le_renderer_o* self, size_t frameIndex ); // ffdecl

// ----------------------------------------------------------------------

static le_renderer_o* renderer_create() {
	auto obj = new le_renderer_o();

	if ( LE_MT > 0 ) {
		le_jobs::initialize( LE_MT );
	}

	using namespace le_backend_vk;
	obj->backend = vk_backend_i.create( obj );

	return obj;
}

// ----------------------------------------------------------------------

// creates a new handle if no name was given, or given name was not found in list of current handles.
static le_texture_handle renderer_produce_texture_handle( le_renderer_o* renderer, char const* maybe_name ) {

	// lock handle library for reading/writing
	static le_texture_handle_store_t* texture_handle_library = get_texture_handle_library();

	std::scoped_lock lock( texture_handle_library->mtx );

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

// ----------------------------------------------------------------------

// Allocate a bindless texture
static le_bindless_texture_handle renderer_allocate_bindless_texture( le_renderer_o* self, le_image_sampler_info_t const* image_sampler_info ) {
	return self->bindless_texture_store.allocate( image_sampler_info );
}

// Allocate a bindless texture
static le_bindless_sampler_handle renderer_allocate_bindless_sampler( le_renderer_o* self, le_sampler_info_t const* sampler_info ) {
	return self->bindless_sampler_store.allocate( sampler_info );
}

// Allocate a bindless texture
static le_bindless_storage_image_handle renderer_allocate_bindless_storage_image( le_renderer_o* self, le_image_view_info_t const* storage_image_info ) {
	return self->bindless_storage_image_store.allocate( storage_image_info );
}

// Fetches the actual resources that are being referenced by bindless resources
// given an array of bindless resource handles and store this into the out_array.
// the out array is assumed to be the same size as the input array, namely
// `num_bindless_resources`.
static void renderer_get_resources_for_bindless_resources( le_renderer_o* self, le_bindless_resource_handle const* bindless_resources, uint32_t num_bindless_resources, le_resource_handle* p_out ) {

	/*
	 * TODO: we should be able to make this simpler now, if we make the bindless resource
	 * hold the reference to the parent resource in the low 32 bits of its handle
	 * then this should be just a mask operation.
	 */

	auto bindless_resources_end = bindless_resources + num_bindless_resources;

	for ( le_bindless_resource_handle const* r = bindless_resources; r != bindless_resources_end; r++, p_out++ ) {
		le_bindless_resource_handle const& res = *r;
		le_resource_handle&                out = *p_out;

		switch ( res->get_type() ) {
		case le_bindless_resource_type::eCombinedImageSampler:
			out = self->bindless_texture_store.get_data( res ).imageView.imageId;
			break;
		case le_bindless_resource_type::eStorageImage:
			out = self->bindless_storage_image_store.get_data( res ).imageId;
			break;
		case le_bindless_resource_type::eSampler:
		case le_bindless_resource_type::eUndefined:
		default:
			assert( false && "cannot resolve " );
		}
	}
}

// ----------------------------------------------------------------------

static char const* texture_handle_get_name( le_texture_handle texture ) {
	if ( texture && !texture->debug_name.empty() ) {
		return texture->debug_name.c_str();
	} else {
		return nullptr;
	}
}

// ----------------------------------------------------------------------

// creates a new resource if no name was given, or given name was not found in list of current handles.
le_resource_handle renderer_produce_resource_handle(
    le_renderer_o*        renderer,
    char const*           maybe_name,
    LeResourceType const& resource_type,
    uint8_t               num_samples      = 0,
    uint8_t               flags            = 0,
    uint16_t              index            = 0,
    le_resource_handle    reference_handle = nullptr ) {

	le_resource_handle_store_t& resource_handle_library = renderer->resource_handle_store;

	// lock handle library for reading/writing
	std::scoped_lock lock( resource_handle_library.mtx );

	uint32_t idx = index;

	le_resource_handle resource_handle{};
	uint32_t           version = 0; // FIXME: use proper versioning of resources

	if ( resource_type == LeResourceType::eBuffer && ( flags != le_buffer_resource_handle_t::eIsUnset ) ) {
		// this is a virtual resource
		idx = index;
		resource_handle = le_resource_handle_t::make_handle( resource_type, flags, idx, version, num_samples );
		// a virtual resource does not need to be stored with the resource handle library
		return resource_handle;
	}

	// ---------| invariant: resource is not virtual

	le_resource_handle_data_t* p_data = new le_resource_handle_data_t{};
	p_data->debug_name                = maybe_name;

	idx = resource_handle_library.resource_handles.size();

	resource_handle = le_resource_handle_t::make_handle( resource_type, flags, idx, version, num_samples );
	p_data->handle  = resource_handle;

	if ( p_data->debug_name.empty() ) {
		char debug_name[ 64 ] = {};
		snprintf( debug_name, sizeof( debug_name ), "[%08lx]", uint64_t( p_data->handle ) );
		p_data->debug_name = debug_name;
	}

	// Store the resource handle with our array of resource handles
	resource_handle_library.resource_handles.emplace_back( p_data );
	return resource_handle;
}

// ----------------------------------------------------------------------

static le_image_resource_handle renderer_produce_img_resource_handle( le_renderer_o* renderer, char const* maybe_name, uint8_t num_samples, le_image_resource_handle reference_handle, uint8_t flags ) {
	return static_cast<le_image_resource_handle>( renderer_produce_resource_handle( renderer, maybe_name, LeResourceType::eImage, num_samples, flags, 0, static_cast<le_resource_handle>( reference_handle ) ) );
}

// ----------------------------------------------------------------------

static le_buffer_resource_handle renderer_produce_buf_resource_handle( le_renderer_o* renderer, char const* maybe_name, uint8_t flags, uint16_t index ) {
	return static_cast<le_buffer_resource_handle>( renderer_produce_resource_handle( renderer, maybe_name, LeResourceType::eBuffer, 0, flags, index ) );
}

// ----------------------------------------------------------------------

static le_tlas_resource_handle renderer_produce_tlas_resource_handle( le_renderer_o* renderer, char const* maybe_name ) {
	return static_cast<le_tlas_resource_handle>( renderer_produce_resource_handle( renderer, maybe_name, LeResourceType::eRtxTlas ) );
}

// ----------------------------------------------------------------------

static le_blas_resource_handle renderer_produce_blas_resource_handle( le_renderer_o* renderer, char const* maybe_name ) {
	return static_cast<le_blas_resource_handle>( renderer_produce_resource_handle( renderer, maybe_name, LeResourceType::eRtxBlas ) );
}

// ----------------------------------------------------------------------
// This copies an array of pointers pointing to debug data.
// Internally, these pointers are owned by the renderer, and kept alive
// for the duration of the lifetime of the program, and therefore you can
// assume that they will never dangle, as long as the renderer is alive.
//
// It's possible that a resource gets reallocated and that the *content*
// to which the pointer points to changes, but (the address of) the pointer
// will remain constant throughout.
//
// In case a resource gets re-used, the pointer will not change, but the
// content of the data pointed to by the pointer; most notably the version
// will be different, which is how you can tell whether a handle is stale
// or not - if the version inside the handle matches the version held in-
// side the data, then the handle is valid, otherwise it is stale.
static bool renderer_clone_resource_data_into( le_renderer_o* self, le_resource_handle_data_t const** p_resource_handle_data_t, size_t* num_elements ) {
	std::unique_lock lock( self->resource_handle_store.mtx );

	if ( *num_elements < self->resource_handle_store.resource_handles.size() ) {
		*num_elements = self->resource_handle_store.resource_handles.size();
		return false;

	} else {
		*num_elements = self->resource_handle_store.resource_handles.size();
	}

	// ----------| there are enough elements in the target vector to store all our data pointers

	memcpy( p_resource_handle_data_t, self->resource_handle_store.resource_handles.data(), *num_elements * sizeof( le_resource_handle_data_t* ) );

	return true;
}
// ----------------------------------------------------------------------

static void renderer_destroy( le_renderer_o* self ) {

	using namespace le_renderer; // for rendergraph_i

	const auto& lastIndex = self->currentFrameNumber;

	for ( size_t i = 0; i != self->frames.size(); ++i ) {
		auto index = ( lastIndex + i ) % self->frames.size();
		renderer_clear_frame( self, index );
	}

	self->frames.clear();

	// Delete texture handle library
	get_texture_handle_library( false );

	{
		le_resource_handle_store_t& resource_handle_library = self->resource_handle_store;
		// we must deallocate manually allocated data for resource handles
		for ( auto& e : resource_handle_library.resource_handles ) {
			delete ( e );
		}

		self->resource_handle_store.resource_handles.clear();
	}

	if ( self->backend ) {
		// Destroy the backend, as it is owned by the renderer
		using namespace le_backend_vk;
		vk_backend_i.destroy( self->backend );
		self->backend = nullptr;
	}

	// Destroy any swapchain settings objects that were created by cloning
	// settings in setup()
	//
	// Note that we don't increment s in the for loop main clause, but in
	// the body of the loop, because we must fetch the next pointer before
	// we destroy the current link in the linked list.
	for ( auto s = self->settings.swapchain_settings; s != nullptr; ) {
		le_swapchain_settings_t* next_setting = s->p_next;
		le_swapchain_vk_api_i->swapchain_i.settings_destroy( s );
		s = next_setting;
	}

#if ( LE_MT > 0 )
	le_jobs::terminate();
#endif

	delete self;
}

// ----------------------------------------------------------------------

static le_rtx_blas_info_handle renderer_create_rtx_blas_info_handle( le_renderer_o* self, le_rtx_geometry_t* geometries, uint32_t geometries_count, le::BuildAccelerationStructureFlagsKHR const* flags ) {
	using namespace le_backend_vk;
	return vk_backend_i.create_rtx_blas_info( self->backend, geometries, geometries_count, flags );
}

// ----------------------------------------------------------------------

static le_rtx_tlas_info_handle renderer_create_rtx_tlas_info_handle( le_renderer_o* self, uint32_t instances_count, le::BuildAccelerationStructureFlagsKHR const* flags ) {
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

// Only returns true if capablilites for **all** swapchains given in settings
// were successfully requested.
static bool renderer_request_swapchain_capabilities( le_renderer_o* self, le_swapchain_settings_t const* settings ) {

	// Request extensions from the backend - this must only be called
	// before or while renderer-setup() is called for the first time.

	using namespace le_swapchain_vk;
	bool result = true;

	for ( auto s = settings; s != nullptr; s = s->p_next ) {
		// swapchain_i.get_min_max_image_count( settings );
		result &= swapchain_i.request_backend_capabilities( s );
	}

	return result;
}

// ----------------------------------------------------------------------

static void renderer_setup( le_renderer_o* self, le_renderer_settings_t const* settings_ ) {
	static auto logger = LeLog( "le_renderer" );

	// We store swapchain settings with the renderer so that we can pass
	// backend a permanent pointer to it.

	if ( settings_ ) {
		self->settings = *settings_;

		// Make a local copy of swapchain_settings linked list by cloning
		// settings objects (we clone because swapchain_settings may be
		// derived, and clone() will copy the correct content depending
		// on swapchain_setting type.)
		//
		// Note that this means that we must destroy all settings that we
		// have created when we tear down the renderer.
		{
			// make a copy of settings linked list
			le_swapchain_settings_t* list_start = nullptr;
			le_swapchain_settings_t* list_end   = nullptr;
			for ( auto s = settings_->swapchain_settings; s != nullptr; s = s->p_next ) {
				if ( list_start == nullptr ) {
					list_start = le_swapchain_vk_api_i->swapchain_i.settings_clone( s );
					list_end   = list_start;
				} else {
					list_end->p_next = le_swapchain_vk_api_i->swapchain_i.settings_clone( s );
					list_end         = list_end->p_next;
				}
			}
			self->settings.swapchain_settings = list_start;
		}
	} else {
		self->settings = {};
	}

	{
		// Before we can initialise the backend, we must query for any required
		// capabilities and extensions that come implied via swapchains:

		// TODO: we must make sure that swapchains have requested capabilities
		// at this point in time.
		bool has_correct_backend_capablities = renderer_request_swapchain_capabilities( self, self->settings.swapchain_settings );

		if ( !has_correct_backend_capablities ) {
			logger.error( "Could not get all requested backend capabilies." );
		}

#if ( LE_MT > 0 )
		le_backend_vk::settings_i.set_concurrency_count( LE_MT );
#endif

		// We can now initialize the backend so that it hopefully conforms to
		// any requirements and capabilities that have been requested so far...
		//
		le_backend_vk::vk_backend_i.initialise( self->backend );

		// We setup our backend so that the backend's allocator becomes
		// available in case that any swapchain implementation needs to allocate resources
		// via the backend. (This is currently the case with the image swapchain, which
		// will use the backend to allocate its target images)
		le_backend_vk::vk_backend_i.setup( self->backend );

		// Now that we have backend device and instance, we can use this to
		// create surfaces for swapchains for example.
		//
		// The first added swapchain will try to set the number of data frames
		//  - via the global backend_settings singleton -
		// so that the number of data frames is less or equal to the number of
		// available images in the swapchain.
		//
		// swapchain settings are a linked list - which means that you must have set these up before,
		for ( le_swapchain_settings_t* s = self->settings.swapchain_settings; s != nullptr; s = s->p_next ) {
			renderer_add_swapchain( self, s );
		}
	}

	self->backendDataFramesCount = le_backend_vk::vk_backend_i.get_data_frames_count( self->backend );

	using namespace le_renderer; // for rendergraph_i
	self->frames.reserve( self->backendDataFramesCount );

	for ( size_t i = 0; i != self->backendDataFramesCount; ++i ) {
		auto frameData        = FrameData();
		frameData.rendergraph = nullptr;
		self->frames.push_back( std::move( frameData ) );
	}

	self->currentFrameNumber = 0;
}

// ----------------------------------------------------------------------
// Applies window to first found window swapchain in renderer->settings
// if no window swapchain is found, a new one is added.
static void renderer_setup_with_window( le_renderer_o* self, le_window_o* window ) {
	static auto logger = LeLog( "le_renderer" );

	le_swapchain_settings_t* last_settings            = nullptr;
	bool                     found_matching_swapchain = false;
	if ( self->settings.swapchain_settings ) {

		// If swapchain settings exist, we set the window to the first window
		// swapchain that has not yet a setting for window.

		for ( auto s = self->settings.swapchain_settings; s != nullptr; s = s->p_next ) {
			last_settings = s;
			if ( s->type == le_swapchain_settings_t::LE_KHR_SWAPCHAIN ) {

				auto sw = reinterpret_cast<le_swapchain_windowed_settings_t*>( s );
				if ( nullptr == sw->window ) {
					logger.info( "Applied window pointer to first window swapchain found in"
					             "renderer settings." );

					sw->window               = window;
					found_matching_swapchain = true;
				}
				break;
			}
		}

		// if no window swapchain was found, add one.

		if ( false == found_matching_swapchain ) {
			self->default_windowed_swapchain_setting.window = window;
			last_settings->p_next                           = &self->default_windowed_swapchain_setting.base;
			logger.info( "Inserted window swapchain into renderer settings." );
		}

	} else {
		// swapchain settings do not exist, we add a default window swapchain object

		self->default_windowed_swapchain_setting.window = window;
		self->settings.swapchain_settings               = &self->default_windowed_swapchain_setting.base;
	}

	renderer_setup( self, &self->settings );
}

// ----------------------------------------------------------------------

static le_renderer_settings_t const* renderer_get_settings( le_renderer_o* self ) {
	return &self->settings;
}

// ----------------------------------------------------------------------

static size_t renderer_get_current_frame_number( le_renderer_o* self ) {
	return self->currentFrameNumber;
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

	if ( frame.rendergraph ) {
		rendergraph_i.destroy( frame.rendergraph );
		frame.rendergraph = nullptr;
	}

	//	std::cout << "CLEAR FRAME " << frameIndex << std::endl
	//	          << std::flush;

	frame.state = FrameData::State::eCleared;
}

// ----------------------------------------------------------------------

static void renderer_record_frame( le_renderer_o* self, size_t frameIndex, le_rendergraph_o* graph, size_t frameNumber ) {
	static auto logger = LeLog( "le_renderer" );

	ZoneScoped;

	// High-level
	// - resolve rendergraph: which render passes do contribute?
	// - consolidate resources, synchronisation for resources
	// - For each render pass, call renderpass' render method, build intermediary command lists

	auto& frame       = self->frames[ frameIndex ];
	frame.frameNumber = frameNumber;

	if ( frame.state != FrameData::State::eCleared && frame.state != FrameData::State::eInitial ) {
		return;
	}

	// Move the given rendergraph into frame.rendergraph - this means that `graph`
	// gets consumed in the process.
	frame.rendergraph = le_renderer::api->le_rendergraph_private_i.move( graph );

	// ---------| invariant: Frame was previously acquired successfully.

	// - build up dependencies for graph, create table of unique resources for graph

	// `setup_passes` calls `setup` callback on all passes - this initalises virtual resources,
	// and stores their descriptors (information needed to allocate physical resources)
	//
	using namespace le_renderer; // for rendergraph_i, rendergraph_i
	le_renderer::api->le_rendergraph_private_i.setup_passes( frame.rendergraph );

	// Find out which renderpasses contribute & tag contributing renderpasses as `is_contributing`
	le_renderer::api->le_rendergraph_private_i.build( frame.rendergraph, frameNumber );

	static auto RENDERGRAPH_SHOULD_GENERATE_DOT_FILES = LE_SETTING( uint32_t, LE_SETTING_IDENTIFIER_SHOULD_RENDERGRAPH_GENERATE_DOT_FILES, 0 );

	if ( *RENDERGRAPH_SHOULD_GENERATE_DOT_FILES > 0 ) [[unlikely]] {
		char label[ 9 ];
		snprintf( label, sizeof( label ), "%08zu", frameNumber );
		le_renderer_api_i->le_rendergraph_private_i.generate_dot_diagram( frame.rendergraph, label );
		( *RENDERGRAPH_SHOULD_GENERATE_DOT_FILES )--;
	}

	{
		// If there are debug messages to print to screen, we must draw them
		// onto the last graphics renderpass *which is contributing*.
		//
		// This assumes that the last graphics renderpass is a renderpass
		// that goes to the screen. If there is no last graphics renderpass,
		// then we must warn about this, and discard any messages that we would
		// have wanted to print to screen.
		//
		// Find last graphics pass, starting at the end
		auto p = frame.rendergraph->passes.rbegin();
		for ( ; p != frame.rendergraph->passes.rend(); p++ ) {
			if ( ( *p )->type & le::QueueFlagBits::eGraphics && ( *p )->is_contributing ) {
				le::DebugPrint::drawAllMessages( *p );
				break;
			}
		}

		if ( p == frame.rendergraph->passes.rend() ) {
			logger.debug( "le::DebugPrint has messages, but no way to print them. Discarding messages." );
			le::DebugPrint::drawAllMessages( nullptr );
		}
	}

	// Register any `on_clear_callback`s for the current frame so that
	// for example any object with frame lifetime can be cleaned up when
	// the frame comes around after the VkFence for clear().
	//
	if ( !frame.rendergraph->on_frame_clear_callbacks.empty() ) {
		le_backend_vk::private_backend_vk_i.frame_add_on_clear_callbacks(
		    self->backend, frameIndex,
		    frame.rendergraph->on_frame_clear_callbacks.data(),
		    frame.rendergraph->on_frame_clear_callbacks.size() );
	}

	// declare any resources that come from swapchains
	le_backend_vk::vk_backend_i.acquire_swapchain_resources( self->backend, frameIndex );

	// Execute callbacks into main application for each render pass,
	// build command lists per render pass in intermediate, api-agnostic representation
	//
	le_renderer::api->le_rendergraph_private_i.execute( frame.rendergraph, frameIndex, self->backend );

	self->last_recorded_frame_number = frameIndex;
	frame.state                      = FrameData::State::eRecorded;
}

// ----------------------------------------------------------------------
/*
 * push the renderer's current state for bindless textures to the backend
 * this will push into the current backend render frame.
 *
 * additionally, we pass a list of indices that indicate that there are
 * changes to the current bindless table that need to be applied.
 *
 *
 */
static void renderer_push_bindless_textures_data( le_renderer_o* self, size_t frameIndex ) {
	static auto logger = LeLog( "le_renderer" );

	ZoneScoped;

	auto& frame = self->frames[ frameIndex ];

	self->bindless_texture_store.push_to_backend( self->backend, frameIndex, le_backend_vk_api_i->private_backend_vk_i.frame_set_bindless_textures_data );
	self->bindless_sampler_store.push_to_backend( self->backend, frameIndex, le_backend_vk_api_i->private_backend_vk_i.frame_set_bindless_samplers_data );
	self->bindless_storage_image_store.push_to_backend( self->backend, frameIndex, le_backend_vk_api_i->private_backend_vk_i.frame_set_bindless_storage_images_data );
}

// ----------------------------------------------------------------------

static const FrameData::State& renderer_acquire_backend_resources( le_renderer_o* self, size_t frameIndex ) {

	ZoneScoped;
	using namespace le_backend_vk; // for vk_bakend_i
	using namespace le_renderer;   // for rendergraph_i

	// ---------| invariant: There are frames to process.

	auto& frame = self->frames[ frameIndex ];

	if ( frame.state != FrameData::State::eRecorded ) {
		return frame.state;
	}

	// ----------| invariant: frame is either initial, or cleared.

	le_renderpass_o** passes          = frame.rendergraph->passes.data();
	size_t            numRenderPasses = frame.rendergraph->num_contributing_passes;

	std::vector<le_renderpass_o*> contributing_passes;
	contributing_passes.reserve( numRenderPasses );

	for ( auto& p : frame.rendergraph->passes ) {
		if ( p->is_contributing ) {
			contributing_passes.push_back( p );
		}
	}

	assert( numRenderPasses == contributing_passes.size() && "Number of contributing renderpasses must match" );

	// TODO -- you should only use passes here that are contributing.

	le_resource_handle const* declared_resources       = frame.rendergraph->declared_resources_id.data();
	le_resource_info_t const* declared_resources_infos = frame.rendergraph->declared_resources_info.data();
	size_t                    declared_resources_count = frame.rendergraph->declared_resources_id.size();

	vk_backend_i.acquire_physical_resources(
	    self->backend,
	    frameIndex,
	    contributing_passes.data(),
	    contributing_passes.size(),
	    declared_resources,
	    declared_resources_infos,
	    declared_resources_count );

	{
		// apply root node affinity masks to backend render frame
		// so that the frame can decide how best to dispatch
		le::RootPassesField const* p_affinity_masks   = frame.rendergraph->root_passes_affinity_masks.data();
		uint32_t                   num_affinity_masks = frame.rendergraph->root_passes_affinity_masks.size();

		std::vector<char const*> names_ptrs;
		names_ptrs.reserve( frame.rendergraph->root_debug_names.size() );

		for ( auto& n : frame.rendergraph->root_debug_names ) {
			names_ptrs.push_back( n.c_str() );
		}

		vk_backend_i.set_frame_queue_submission_keys(
		    self->backend, frameIndex,
		    reinterpret_cast<void const*>( p_affinity_masks ), num_affinity_masks,
		    names_ptrs.data(), names_ptrs.size() );
	}

	frame.state = FrameData::State::eAcquired;

	return frame.state;
}

// ----------------------------------------------------------------------
// translate intermediate draw lists into vk command buffers, and sync primitives
static const FrameData::State& renderer_process_frame( le_renderer_o* self, size_t frameIndex ) {

	ZoneScoped;
	using namespace le_backend_vk; // for vk_bakend_i

	auto& frame = self->frames[ frameIndex ];

	if ( frame.state != FrameData::State::eAcquired ) {
		return frame.state;
	}
	// ---------| invariant: frame was previously recorded successfully

	// translate intermediate draw lists into vk command buffers, and sync primitives
	vk_backend_i.process_frame( self->backend, frameIndex );

	frame.state = FrameData::State::eProcessed;
	return frame.state;
}

// ----------------------------------------------------------------------

static void renderer_dispatch_frame( le_renderer_o* self, size_t frameIndex ) {

	ZoneScoped;
	using namespace le_backend_vk; // for vk_backend_i
	auto& frame = self->frames[ frameIndex ];

	if ( frame.state != FrameData::State::eProcessed ) {
		return;
	}

	// ---------| invariant: frame was successfully processed previously

	vk_backend_i.dispatch_frame( self->backend, frameIndex );

	frame.state = FrameData::State::eDispatched;
}

// ----------------------------------------------------------------------

static le_image_resource_handle renderer_get_swapchain_resource( le_renderer_o* self, le_swapchain_handle swapchain ) {
	ZoneScoped;
	using namespace le_backend_vk;
	return vk_backend_i.get_swapchain_resource( self->backend, swapchain );
}

static le_image_resource_handle renderer_get_swapchain_resource_default( le_renderer_o* self ) {
	ZoneScoped;
	using namespace le_backend_vk;
	return vk_backend_i.get_swapchain_resource_default( self->backend );
}

// ----------------------------------------------------------------------

static bool renderer_get_swapchain_extent( le_renderer_o* self, le_swapchain_handle swapchain, uint32_t* p_width, uint32_t* p_height ) {
	ZoneScoped;
	using namespace le_backend_vk;
	return vk_backend_i.get_swapchain_extent( self->backend, swapchain, p_width, p_height );
}

// ----------------------------------------------------------------------

static le_swapchain_handle renderer_add_swapchain( le_renderer_o* self, le_swapchain_settings_t const* settings ) {
	ZoneScoped;
	using namespace le_backend_vk;
	assert( self->backend && "Backend must exist" );
	return vk_backend_i.add_swapchain( self->backend, settings );
};
// ----------------------------------------------------------------------

static bool renderer_remove_swapchain( le_renderer_o* self, le_swapchain_handle swapchain ) {
	ZoneScoped;
	using namespace le_backend_vk;
	assert( self->backend && "Backend must exist" );
	return vk_backend_i.remove_swapchain( self->backend, swapchain );
};

static bool renderer_get_swapchains( le_renderer_o* self, size_t* num_swapchains, le_swapchain_handle* p_swapchain_handles ) {
	ZoneScoped;
	using namespace le_backend_vk;
	assert( self->backend && "Backend must exist" );
	return vk_backend_i.get_swapchains( self->backend, num_swapchains, p_swapchain_handles );
}

// ----------------------------------------------------------------------
static bool renderer_resize_swapchain( le_renderer_o* self, le_swapchain_handle swapchain, uint32_t width, uint32_t height ) {
	ZoneScoped;
	using namespace le_backend_vk;
	return vk_backend_i.recreate_swapchain( self->backend, swapchain, width, height );
};

// ----------------------------------------------------------------------

static void renderer_update( le_renderer_o* self, le_rendergraph_o* graph_ ) {
	ZoneScoped;

	static auto logger = LeLog( "le_renderer" );
	using namespace le_backend_vk;

	const auto& index     = self->currentFrameNumber;
	const auto& numFrames = self->frames.size();

	// If necessary, recompile and reload shader modules
	// - this must be complete before the record_frame step

	if ( LE_MT > 0 ) {
		// use task system (experimental)

		le_jobs::counter_t* shader_counter;

		le_jobs::job_t j{
		    []( void* backend ) {
			    vk_backend_i.update_shader_modules( static_cast<le_backend_o*>( backend ) );
		    },
		    self->backend };

		le_jobs::run_jobs( &j, 1, &shader_counter );

		struct frame_params_t {
			le_renderer_o* renderer;
			size_t         frame_index;
		};

		struct record_params_t {
			le_renderer_o*      renderer;
			size_t              frame_index;
			le_rendergraph_o*   rendergraph;
			size_t              current_frame_number;
			le_jobs::counter_t* shader_counter;
		};

		auto record_frame_fun = []( void* param_ ) {
			auto p = static_cast<record_params_t*>( param_ );
			// generate an intermediary, api-agnostic, representation of the frame

			le_jobs::wait_for_counter_and_free( p->shader_counter, 0 );
			renderer_record_frame( p->renderer, p->frame_index, p->rendergraph, p->current_frame_number );
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
		record_frame_params.rendergraph          = graph_;
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

	} else {

		// render on the main thread

		size_t recorded_frame_index = 0;
		{
			// RECORD FRAME
			recorded_frame_index = ( index + 0 ) % numFrames;
			// logger.info( "+++ [%5d] RECO", frameIndex );
			renderer_record_frame( self, recorded_frame_index, graph_, self->currentFrameNumber ); // generate an intermediary, api-agnostic, representation of the frame

			renderer_push_bindless_textures_data( self, recorded_frame_index );
		}

		vk_backend_i.update_shader_modules( self->backend );

		{
			// DISPATCH FRAME
			// acquire external backend resources such as swapchain
			// and create any temporary resources
			auto frameIndex = ( index + 2 ) % numFrames;
			// logger.info( "+++ [%5d] DISP", frameIndex );
			renderer_acquire_backend_resources( self, frameIndex ); //
			renderer_process_frame( self, frameIndex );             // generate api commands for the frame
			renderer_dispatch_frame( self, frameIndex );            //
		}

		{
			// CLEAR FRAME
			// wait for frame to come back (important to do this last, as it may block...)
			auto frameIndex = ( index + 1 ) % numFrames;
			// logger.info( "+++ [%5d] CLEA", frameIndex );
			renderer_clear_frame( self, frameIndex );
		}
	}

	// logger.info( "+++ NEXT FRAME\n" );
	++self->currentFrameNumber;
	FrameMark; // We have completed the current frame - this signals it to tracy
}

// ----------------------------------------------------------------------

static le_resource_info_t get_default_resource_info_for_image() {
	le_resource_info_t res = {};

	res.type = LeResourceType::eImage;
	{
		auto& img                   = res.image;
		img                         = {};
		img.flags                   = le::ImageCreateFlagBits( 0 );
		img.format                  = le::Format::eUndefined;
		img.arrayLayers             = 1;
		img.extent.width            = 0;
		img.extent.height           = 0;
		img.extent.depth            = 1;
		img.extent_from_pass.width  = 0;
		img.extent_from_pass.height = 0;
		img.extent_from_pass.depth  = 1;
		img.usage                   = le::ImageUsageFlagBits::eTransferDst | le::ImageUsageFlagBits::eSampled;
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
	le_resource_info_t res = {};
	res.type               = LeResourceType::eBuffer;
	res.buffer.size        = 0;
	res.buffer.usage       = le::BufferUsageFlags( le::BufferUsageFlagBits::eTransferDst );
	return res;
}

extern void register_le_rendergraph_api( void* api );            // in le_rendergraph.cpp
extern void register_le_command_buffer_encoder_api( void* api ); // in le_command_buffer_encoder.cpp

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_renderer, api ) {
	auto  le_renderer_api_i = static_cast<le_renderer_api*>( api );
	auto& le_renderer_i     = le_renderer_api_i->le_renderer_i;

	le_renderer_i.create                         = renderer_create;
	le_renderer_i.destroy                        = renderer_destroy;
	le_renderer_i.setup                          = renderer_setup;
	le_renderer_i.setup_with_window              = renderer_setup_with_window;
	le_renderer_i.update                         = renderer_update;
	le_renderer_i.get_settings                   = renderer_get_settings;
	le_renderer_i.get_current_frame_number       = renderer_get_current_frame_number;
	le_renderer_i.get_swapchain_extent           = renderer_get_swapchain_extent;
	le_renderer_i.get_pipeline_manager           = renderer_get_pipeline_manager;
	le_renderer_i.get_backend                    = renderer_get_backend;
	le_renderer_i.get_swapchain_resource         = renderer_get_swapchain_resource;
	le_renderer_i.get_swapchain_resource_default = renderer_get_swapchain_resource_default;
	le_renderer_i.add_swapchain                  = renderer_add_swapchain;
	le_renderer_i.remove_swapchain               = renderer_remove_swapchain;
	le_renderer_i.resize_swapchain               = renderer_resize_swapchain;
	le_renderer_i.get_swapchains                 = renderer_get_swapchains;
	le_renderer_i.produce_texture_handle         = renderer_produce_texture_handle;
	le_renderer_i.texture_handle_get_name        = texture_handle_get_name;
	le_renderer_i.create_rtx_blas_info           = renderer_create_rtx_blas_info_handle;
	le_renderer_i.create_rtx_tlas_info           = renderer_create_rtx_tlas_info_handle;
	le_renderer_i.allocate_bindless_texture      = renderer_allocate_bindless_texture;
	le_renderer_i.allocate_bindless_sampler       = renderer_allocate_bindless_sampler;
	le_renderer_i.allocate_bindless_storage_image = renderer_allocate_bindless_storage_image;

	le_renderer_i.get_resources_for_bindless_resources = renderer_get_resources_for_bindless_resources;

	auto& helpers_i = le_renderer_api_i->helpers_i;

	helpers_i.get_default_resource_info_for_buffer = get_default_resource_info_for_buffer;
	helpers_i.get_default_resource_info_for_image  = get_default_resource_info_for_image;

	le_renderer_i.produce_img_resource_handle      = renderer_produce_img_resource_handle;
	le_renderer_i.produce_buf_resource_handle      = renderer_produce_buf_resource_handle;
	le_renderer_i.produce_tlas_resource_handle     = renderer_produce_tlas_resource_handle;
	le_renderer_i.produce_blas_resource_handle     = renderer_produce_blas_resource_handle;

	le_renderer_i.clone_resource_data_into = renderer_clone_resource_data_into;
	// register sub-components of this api
	register_le_rendergraph_api( api );

	register_le_command_buffer_encoder_api( api );
	LE_LOAD_TRACING_LIBRARY;
}

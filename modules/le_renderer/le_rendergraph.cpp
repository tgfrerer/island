#include "le_renderer.h"

#include "le_backend_vk/le_backend_vk.h"

#include <vector>
#include <string>
#include <assert.h>
#include <algorithm>
#include <unordered_map>
#include <iostream>
#include <iomanip>

#include "le_renderer/private/le_renderer_types.h"

#ifndef PRINT_DEBUG_MESSAGES
#	define PRINT_DEBUG_MESSAGES false
#endif

#include <bitset>

constexpr size_t MAX_NUM_LAYER_RESOURCES      = 4096; // set this to larger value if you want to deal with a larger number of distinct resources.
using BitField                                = std::bitset<MAX_NUM_LAYER_RESOURCES>;
constexpr auto LE_RENDER_GRAPH_ROOT_LAYER_TAG = LE_RESOURCE( "LE_RENDER_GRAPH_ROOT_LAYER_TAG", LeResourceType::eUndefined );

struct Task {
	BitField reads;
	BitField writes;
};

// these are some sanity checks for le_renderer_types
static_assert( sizeof( le::CommandHeader ) == sizeof( uint64_t ), "Size of le::CommandHeader must be 64bit" );

struct ExecuteCallbackInfo {
    le_renderer_api::pfn_renderpass_execute_t fn        = nullptr;
    void *                                    user_data = nullptr;
};

struct le_renderpass_o {

	LeRenderPassType        type         = LE_RENDER_PASS_TYPE_UNDEFINED;
	uint32_t                ref_count    = 0;                           // reference count (we're following an intrusive shared pointer pattern)
	uint64_t                id           = 0;                           // hash of name
	uint64_t                sort_key     = 0;                           //
	uint32_t                width        = 0;                           ///< width  in pixels, must be identical for all attachments, default:0 means current frame.swapchainWidth
	uint32_t                height       = 0;                           ///< height in pixels, must be identical for all attachments, default:0 means current frame.swapchainHeight
	le::SampleCountFlagBits sample_count = le::SampleCountFlagBits::e1; // < SampleCount for all attachments.
	uint32_t                isRoot       = false;                       // whether pass *must* be processed

	std::vector<le_resource_handle_t> resources;              // all resources used in this pass
	std::vector<LeAccessFlags>        resources_access_flags; // access flags for all resources, in sync with resources
	std::vector<LeResourceUsageFlags> resources_usage;        // declared usage for each resource, in sync with resources

	std::vector<le_image_attachment_info_t> imageAttachments;    // settings for image attachments (may be color/or depth)
	std::vector<le_resource_handle_t>       attachmentResources; // kept in sync with imageAttachments, one resource per attachment

	std::vector<le_resource_handle_t> textureIds;   // imageSampler resource infos
	std::vector<LeImageSamplerInfo>   textureInfos; // kept in sync with texture id: info for corresponding texture id

	le_renderer_api::pfn_renderpass_setup_t callbackSetup            = nullptr;
	void *                                  setup_callback_user_data = nullptr;
	std::vector<ExecuteCallbackInfo>        executeCallbacks;

	le_command_buffer_encoder_o *encoder   = nullptr;
	std::string                  debugName = "";
};

// ----------------------------------------------------------------------

struct le_render_module_o : NoCopy, NoMove {
	std::vector<le_renderpass_o *>    passes;
	std::vector<le_resource_handle_t> declared_resources_id;   // | pre-declared resources (declared via module)
	std::vector<le_resource_info_t>   declared_resources_info; // | pre-declared resources (declared via module)
};

// ----------------------------------------------------------------------

struct le_rendergraph_o : NoCopy, NoMove {
	std::vector<le_renderpass_o *>    passes;
	std::vector<uint32_t>             sortIndices;
	std::vector<le_resource_handle_t> declared_resources_id;   // | pre-declared resources (declared via module)
	std::vector<le_resource_info_t>   declared_resources_info; // | pre-declared resources (declared via module)
};

// ----------------------------------------------------------------------

static le_renderpass_o *renderpass_create( const char *renderpass_name, const LeRenderPassType &type_ ) {
	auto self       = new le_renderpass_o();
	self->id        = hash_64_fnv1a( renderpass_name );
	self->type      = type_;
	self->debugName = renderpass_name;
	self->ref_count = 1;
	return self;
}

// ----------------------------------------------------------------------

static le_renderpass_o *renderpass_clone( le_renderpass_o const *rhs ) {
	auto self = new le_renderpass_o();
	*self     = *rhs;
	self->ref_count = 1;
	return self;
}

// ----------------------------------------------------------------------

static void renderpass_destroy( le_renderpass_o *self ) {

	if ( self->encoder ) {
		using namespace le_renderer;
		encoder_i.destroy( self->encoder );
	}

	delete self;
}

static void renderpass_ref_inc( le_renderpass_o *self ) {
	++self->ref_count;
}

static void renderpass_ref_dec( le_renderpass_o *self ) {
	if ( --self->ref_count == 0 ) {
		renderpass_destroy( self );
	}
}

// ----------------------------------------------------------------------

static void renderpass_set_setup_callback( le_renderpass_o *self, void *user_data, le_renderer_api::pfn_renderpass_setup_t callback ) {
	self->setup_callback_user_data = user_data;
	self->callbackSetup            = callback;
}

// ----------------------------------------------------------------------

static void renderpass_set_execute_callback( le_renderpass_o *self, void *user_data, le_renderer_api::pfn_renderpass_execute_t callback ) {
    self->executeCallbacks.push_back( {callback, user_data} );
}

// ----------------------------------------------------------------------
static void renderpass_run_execute_callback( le_renderpass_o *self ) {
    for ( auto const &c : self->executeCallbacks ) {
        c.fn( self->encoder, c.user_data );
    }
}

// ----------------------------------------------------------------------
static bool renderpass_run_setup_callback( le_renderpass_o *self ) {
	return self->callbackSetup( self, self->setup_callback_user_data );
}

// ----------------------------------------------------------------------
template <typename T>
static inline bool vector_contains( const std::vector<T> &haystack, const T &needle ) noexcept {
	return haystack.end() != std::find( haystack.begin(), haystack.end(), needle );
}

// ----------------------------------------------------------------------
// Associate a resource with a renderpass.
// Data containted in `resource_info` decides whether the resource
// is used for read, write, or read/write.
static void renderpass_use_resource( le_renderpass_o *self, const le_resource_handle_t &resource_id, LeResourceUsageFlags const &usage_flags ) {

	assert( usage_flags.type == LeResourceType::eBuffer || usage_flags.type == LeResourceType::eImage );

	// ---------| Invariant: resource is either an image or buffer

	size_t resource_idx    = 0; // index of matching resource
	size_t resources_count = self->resources.size();
	for ( le_resource_handle_t *res = self->resources.data(); resource_idx != resources_count; res++, resource_idx++ ) {
		if ( *res == resource_id ) {
			// found a match
			break;
		}
	}

	if ( resource_idx == resources_count ) {
		// not found, add resource and resource info
		self->resources.push_back( resource_id );
		// Note that we don't immediately set the access flag,
		// as the correct access flag is calculated based on resource_info
		// after this block.
		self->resources_access_flags.push_back( LeAccessFlagBits::eLeAccessFlagBitUndefined );
		self->resources_usage.push_back( usage_flags );
	} else {

		// Resource already exists.

		std::cerr << "FATAL: Resource '" << resource_id.debug_name << "' declared more than once for renderpass : '"
		          << self->debugName << "'. There can only be one declaration per resource per renderpass." << std::endl
		          << std::flush;

		assert( false );
	}

	// Now we check whether there is a read and/or a write operation on
	// the resource
	static constexpr uint32_t ALL_IMAGE_WRITE_FLAGS =
	    LE_IMAGE_USAGE_TRANSFER_DST_BIT |             //
	    LE_IMAGE_USAGE_STORAGE_BIT |                  //
	    LE_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |         //
	    LE_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | //
	    LE_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT       //
	    ;

	static constexpr uint32_t ALL_IMAGE_READ_FLAGS =
	    LE_IMAGE_USAGE_TRANSFER_SRC_BIT |             //
	    LE_IMAGE_USAGE_SAMPLED_BIT |                  //
	    LE_IMAGE_USAGE_STORAGE_BIT |                  // load, store, atomic
	    LE_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |         //
	    LE_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | //
	    LE_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |     //
	    LE_IMAGE_USAGE_INPUT_ATTACHMENT_BIT           //
	    ;

	static constexpr auto ALL_BUFFER_WRITE_FLAGS =
	    LE_BUFFER_USAGE_TRANSFER_DST_BIT |         //
	    LE_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | // assume read_write
	    LE_BUFFER_USAGE_STORAGE_BUFFER_BIT         // assume read_write
	    ;

	static constexpr auto ALL_BUFFER_READ_FLAGS =
	    LE_BUFFER_USAGE_TRANSFER_SRC_BIT |            //
	    LE_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |    //
	    LE_BUFFER_USAGE_UNIFORM_BUFFER_BIT |          //
	    LE_BUFFER_USAGE_INDEX_BUFFER_BIT |            //
	    LE_BUFFER_USAGE_VERTEX_BUFFER_BIT |           //
	    LE_BUFFER_USAGE_STORAGE_BUFFER_BIT |          // assume read_write
	    LE_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT |    // assume read_write
	    LE_BUFFER_USAGE_INDIRECT_BUFFER_BIT |         //
	    LE_BUFFER_USAGE_CONDITIONAL_RENDERING_BIT_EXT //
	    ;

	bool resourceWillBeWrittenTo = false;
	bool resourceWillBeReadFrom  = false;

	switch ( usage_flags.type ) {
	case LeResourceType::eBuffer: {
		resourceWillBeReadFrom  = usage_flags.typed_as.buffer_usage_flags & ALL_BUFFER_READ_FLAGS;
		resourceWillBeWrittenTo = usage_flags.typed_as.buffer_usage_flags & ALL_BUFFER_WRITE_FLAGS;
	} break;
	case LeResourceType::eImage: {
		resourceWillBeReadFrom  = usage_flags.typed_as.image_usage_flags & ALL_IMAGE_READ_FLAGS;
		resourceWillBeWrittenTo = usage_flags.typed_as.image_usage_flags & ALL_IMAGE_WRITE_FLAGS;
	} break;
	default:
        break;
	}

	// update access flags
	LeAccessFlags &access_flags = self->resources_access_flags[ resource_idx ];

	if ( resourceWillBeReadFrom ) {
		access_flags |= LeAccessFlagBits::eLeAccessFlagBitRead;
	}

	if ( resourceWillBeWrittenTo ) {
		access_flags |= LeAccessFlagBits::eLeAccessFlagBitWrite;
	}
}

// ----------------------------------------------------------------------
static void renderpass_sample_texture( le_renderpass_o *self, le_resource_handle_t texture, LeImageSamplerInfo const *textureInfo ) {

	// -- store texture info so that backend can create resources

	if ( vector_contains( self->textureIds, texture ) ) {
		return; // texture already present
	}

	// --------| invariant: texture id was not previously known

	// -- Add texture info to list of texture infos for this frame
	self->textureIds.push_back( texture );
	//	self->textureImageIds.push_back( textureInfo->imageView.imageId );
	self->textureInfos.push_back( *textureInfo ); // store a copy of info

	LeResourceUsageFlags required_flags{LeResourceType::eImage, {{LeImageUsageFlagBits::LE_IMAGE_USAGE_SAMPLED_BIT}}};

	// -- Mark image resource referenced by texture as used for reading
	renderpass_use_resource( self, textureInfo->imageView.imageId, required_flags );
}

// ----------------------------------------------------------------------

static void renderpass_add_color_attachment( le_renderpass_o *self, le_resource_handle_t image_id, le_image_attachment_info_t const *attachmentInfo ) {

	self->imageAttachments.push_back( *attachmentInfo );
	self->attachmentResources.push_back( image_id );

	// Make sure that this imgage can be used as a color attachment,
	// even if user forgot to specify the flag.
	LeResourceUsageFlags required_flags{LeResourceType::eImage, {{LeImageUsageFlagBits::LE_IMAGE_USAGE_COLOR_ATTACHMENT_BIT}}};

	renderpass_use_resource( self, image_id, required_flags );
}

// ----------------------------------------------------------------------

static void renderpass_add_depth_stencil_attachment( le_renderpass_o *self, le_resource_handle_t image_id, le_image_attachment_info_t const *attachmentInfo ) {

	self->imageAttachments.push_back( *attachmentInfo );
	self->attachmentResources.push_back( image_id );

	// Make sure that this image can be used as a depth stencil attachment,
	// even if user forgot to specify the flag.
	LeResourceUsageFlags required_flags{LeResourceType::eImage, {{LeImageUsageFlagBits::LE_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT}}};

	renderpass_use_resource( self, image_id, required_flags );
}

// ----------------------------------------------------------------------

static uint32_t renderpass_get_width( le_renderpass_o *self ) {
	return self->width;
}
// ----------------------------------------------------------------------
static uint32_t renderpass_get_height( le_renderpass_o *self ) {
	return self->height;
}

static void renderpass_set_width( le_renderpass_o *self, uint32_t width ) {
	self->width = width;
}

static void renderpass_set_height( le_renderpass_o *self, uint32_t height ) {
	self->height = height;
}

static void renderpass_set_sample_count( le_renderpass_o *self, le::SampleCountFlagBits const &sampleCount ) {
	self->sample_count = sampleCount;
}

static le::SampleCountFlagBits const &renderpass_get_sample_count( le_renderpass_o const *self ) {
	return self->sample_count;
}

// ----------------------------------------------------------------------

static void renderpass_set_is_root( le_renderpass_o *self, bool isRoot ) {
	self->isRoot = isRoot;
}

static bool renderpass_get_is_root( le_renderpass_o const *self ) {
	return self->isRoot;
}

static void renderpass_set_sort_key( le_renderpass_o *self, uint64_t sort_key ) {
	self->sort_key = sort_key;
}

static uint64_t renderpass_get_sort_key( le_renderpass_o const *self ) {
	return self->sort_key;
}

static LeRenderPassType renderpass_get_type( le_renderpass_o const *self ) {
	return self->type;
}

static void renderpass_get_used_resources( le_renderpass_o const *self, le_resource_handle_t const **pResources, LeResourceUsageFlags const **pResourcesUsage, size_t *count ) {
	assert( self->resources_usage.size() == self->resources.size() );

	*count           = self->resources.size();
	*pResources      = self->resources.data();
	*pResourcesUsage = self->resources_usage.data();
}

static const char *renderpass_get_debug_name( le_renderpass_o const *self ) {
	return self->debugName.c_str();
}

static uint64_t renderpass_get_id( le_renderpass_o const *self ) {
	return self->id;
}

static void renderpass_get_image_attachments( const le_renderpass_o *self, le_image_attachment_info_t const **pAttachments, le_resource_handle_t const **pResources, size_t *numAttachments ) {
	*pAttachments   = self->imageAttachments.data();
	*pResources     = self->attachmentResources.data();
	*numAttachments = self->imageAttachments.size();
}

static void renderpass_get_texture_ids( le_renderpass_o *self, const le_resource_handle_t **ids, uint64_t *count ) {
	*ids   = self->textureIds.data();
	*count = self->textureIds.size();
};

static void renderpass_get_texture_infos( le_renderpass_o *self, const LeImageSamplerInfo **infos, uint64_t *count ) {
	*infos = self->textureInfos.data();
	*count = self->textureInfos.size();
};

static bool renderpass_has_execute_callback( const le_renderpass_o *self ) {
    return !self->executeCallbacks.empty();
}

static bool renderpass_has_setup_callback( const le_renderpass_o *self ) {
	return self->callbackSetup != nullptr;
}

/// @warning Encoder becomes the thief's worry to destroy!
/// @returns null if encoder was already stolen, otherwise a pointer to an encoder object
le_command_buffer_encoder_o *renderpass_steal_encoder( le_renderpass_o *self ) {
	auto result   = self->encoder;
	self->encoder = nullptr;
	return result;
}

// ----------------------------------------------------------------------

static le_rendergraph_o *rendergraph_create() {
	auto obj = new le_rendergraph_o();
	return obj;
}

// ----------------------------------------------------------------------

static void rendergraph_reset( le_rendergraph_o *self ) {

	// we must destroy passes as we have ownership over them.
	for ( auto rp : self->passes ) {
		renderpass_destroy( rp );
	}
	self->passes.clear();

	self->declared_resources_id.clear();
	self->declared_resources_info.clear();
}

// ----------------------------------------------------------------------

static void rendergraph_destroy( le_rendergraph_o *self ) {
	rendergraph_reset( self );
	delete self;
}

// ----------------------------------------------------------------------

static void rendergraph_add_renderpass( le_rendergraph_o *self, le_renderpass_o *renderpass ) {

	self->passes.push_back( renderpass ); // Note: We receive ownership of the pass here. We must destroy it.
}

/// \brief Tag any tasks which contribute to any root task
/// \details We do this so that we can weed out any tasks which are provably
///          not contributing - these don't need to be executed at all.
static void tasks_tag_contributing( Task *const tasks, const size_t numTasks ) {

	// we must iterate backwards from last layer to first layer
	Task *            task      = tasks + numTasks;
	Task const *const task_rend = tasks;

	BitField read_accum;

	// find first root layer
	//    monitored reads will be from the first root layer

	while ( task != task_rend ) {
		--task;

		bool isRoot = task->reads[ 0 ]; // any task which has the root signal set in the first read channel is considered a root task

		// If it's a root task, get all reads from (= providers to) this task
		// If it's not a root task, first see if there are any writes to currently monitored reads
		//    if yes, add all reads to monitored reads

		if ( isRoot || ( task->writes & read_accum ).any() ) {
			// If this task is a root task - OR					      ) this means the layer is contributing
			// If this task writes to any subsequent monitored reads, )
			// Then we must monitor all reads by this task.
			read_accum |= task->reads;

			task->reads[ 0 ] = true; // Make sure the task is tagged as contributing
		} else {
			// Otherwise - this task does not contribute
		}
	} // end for all tasks, backwards iteration
}

/// Note: `sortIndices` must point to an array of `numtasks` elements of type uint32_t
static void tasks_calculate_sort_indices( Task const *const tasks, const size_t numTasks, uint32_t *sortIndices ) {

	BitField read_accum{};
	BitField write_accum{};

	/// Each bit in the task bitfield stands for one resource.
	/// Bitfield index corresponds to a resource id. Note that
	/// bitfields are indexed right-to left (index zero is right-most).

	bool needs_barrier = false;

	uint32_t sortIndex = 0;

	{
		Task const *const tasks_end = tasks + numTasks;
		uint32_t *        taskOrder = sortIndices;
		for ( Task const *task = tasks; task != tasks_end; task++, taskOrder++ ) {

			// Weed out any tasks which are marked as non-contributing

			if ( task->reads[ 0 ] == false ) {
				*taskOrder = ~( 0u ); // tag task as not contributing by marking it with the maximum sort index
				continue;
			}

			BitField read_write = ( task->reads & task->writes ); // read_after write in same task - this means a task boundary if it does touch any previously read or written elements

			// A barrier is needed, if:
			needs_barrier = ( read_accum & read_write ).any() ||    // - any previously read elements are touched by read-write, OR
			                ( write_accum & read_write ).any() ||   // - any previously written elements are touched by read-write, OR
			                ( write_accum & task->reads ).any() ||  // - the current task wants to read from a previously written task, OR
			                ( write_accum & task->writes ).any() || // - the current task writes to a previously written resource, OR
			                ( read_accum & task->writes ).any();    // - the current task wants to write to a task which was previously read.

			//			std::cout << "Needs barrier: " << ( needs_barrier ? "true" : "false" ) << std::endl
			//			          << std::flush;

			if ( needs_barrier ) {
				++sortIndex;         // Barriers are expressed by increasing the sortIndex. tasks with the same sortIndex *may* execute concurrently.
				read_accum.reset();  // Barriers apply everything before the current task
				write_accum.reset(); //
				needs_barrier = false;
			}

			write_accum |= task->writes;
			read_accum |= task->reads;

			*taskOrder = sortIndex; // store current sortIndex value with task

			// std::cout << task->reads << " reads" << std::endl
			//           << std::flush;
			// std::cout << read_accum << " read accum" << std::endl
			//           << std::flush;
			// std::cout << write_accum << " write accum" << std::endl
			//           << std::flush;
		}
	}
}

// ----------------------------------------------------------------------
// Calculate a topological order for passes within rendergraph.
//
// We assume that passes arrive in partial-order (i.e. the order
// of adding passes to a module is meaningful)
//
// As a side-effect, this method:
// + Removes (and deletes) any passes which do not contribute from a rendergraph.
// + Updates sortIndices so that it has same number of elements as rendergraph.
// After completion this method guarantees that sortIndices constains a valid
// sort index for each corresponding renderpass.
//
static void rendergraph_build( le_rendergraph_o *self ) {

	// We must express our list of passes as a list of tasks.
	// A task holds two bitfields, the bitfield names are: `read` and `write`.
	// Each bit in the bitfield represents a possible resource.
	// This means we must create a list of unique resources, so that we can use the resource index as the
	// offset value for a bit representing this particular resource in the bitfields.

	std::vector<Task>                                         tasks;
	std::array<le_resource_handle_t, MAX_NUM_LAYER_RESOURCES> uniqueHandles; // lookup for resource handles.
	uniqueHandles[ 0 ]        = LE_RENDER_GRAPH_ROOT_LAYER_TAG;              // handle with index zero is marker for root tasks
	size_t numUniqueResources = 1;

	// Translate all passes into a task
	//   Get list of resources per pass and build task from this

	for ( auto const &p : self->passes ) {

		Task task;

		const size_t numResources = p->resources.size();

		for ( size_t i = 0; i != numResources; i++ ) {
			auto const &         resource_handle = p->resources[ i ];
			LeAccessFlags const &access_flags    = p->resources_access_flags[ i ];

			size_t res_idx = 0; // unique resource id (monotonic, non-sparse, index into bitfield)
			for ( auto r = uniqueHandles.data(); res_idx != numUniqueResources; res_idx++, r++ ) {
				if ( *r == resource_handle ) {
					// found matching resource, res_idx is index into uniqueHandles for resource
					break;
				}
			}

			if ( res_idx == numUniqueResources ) {
				// resource was not found, we must add a new resource
				uniqueHandles[ res_idx ] = resource_handle;
				numUniqueResources++;
			}

			// --------| invariant: uniqueHandles[res_idx] is valid

			task.reads |= ( ( access_flags & LeAccessFlagBits::eLeAccessFlagBitRead ) << res_idx );
			task.writes |= ( ( ( access_flags & LeAccessFlagBits::eLeAccessFlagBitWrite ) >> 1 ) << res_idx );
		}

		if ( p->isRoot ) {
			// Any task which has reads[0] set to true is marked as a root task.
			task.reads[ 0 ] = true;
		}

		tasks.emplace_back( std::move( task ) );
	}

	// Tag all tasks which contribute to any root task.
	//
	// Tasks which don't contribute to any root task
	// can be disposed, as their products will never be used.
	tasks_tag_contributing( tasks.data(), tasks.size() );

	self->sortIndices.resize( tasks.size(), 0 );

	// Associate sort indices to tasks
	tasks_calculate_sort_indices( tasks.data(), tasks.size(), self->sortIndices.data() );

	auto printPassList = [&]() -> void {
        for ( size_t i = 0; i != self->sortIndices.size(); ++i ) {
            std::cout << "Pass: " << std::dec << std::setw( 3 ) << i << " sort order : " << std::setw( 12 ) << self->sortIndices[ i ] << " : "
			          << self->passes[ i ]->debugName
			          << std::endl
			          << std::flush;
        }
    };

    if ( PRINT_DEBUG_MESSAGES ) {
        printPassList();
    }

    {
        // Remove any passes from rendergraph which do not contribute.
        // Passes which don't contribute have a sort index of (unsigned) -1.
        //
        // We consiolidate the list of passes by rebuilding it while only
        // including passes which contribute (whose sort index != -1)

        size_t numSortIndices = self->sortIndices.size();

		std::vector<le_renderpass_o *> consolidated_passes;
		std::vector<uint32_t>          consolidated_sort_indices;

		consolidated_passes.reserve( numSortIndices );
		consolidated_sort_indices.reserve( numSortIndices );

		for ( size_t i = 0; i != self->sortIndices.size(); i++ ) {
			if ( self->sortIndices[ i ] != ( ~0u ) ) {
				// valid sort index, add to consolidated passes
				consolidated_passes.push_back( self->passes[ i ] );
				consolidated_sort_indices.push_back( self->sortIndices[ i ] );
			} else {
				// Sort index hints that this pass is not used,
				// since the rendergraph owns the pass at this point,
				// we must delete it.
				delete self->passes[ i ];
				self->passes[ i ] = nullptr;
			}
		}

		std::swap( self->passes, consolidated_passes );
		std::swap( self->sortIndices, consolidated_sort_indices );

		if ( PRINT_DEBUG_MESSAGES ) {
			std::cout << "* Consolidated Pass List *" << std::endl
			          << std::flush;
			printPassList();
		}
	}
}

// ----------------------------------------------------------------------
/// Record commands by calling execution callbacks for each renderpass.
///
/// Commands are stored as a command stream. This command stream uses a binary,
/// API-agnostic representation, and contains an ordered list of commands, and optionally,
/// inlined parameters for each command.
///
/// The command stream is stored inside of the Encoder that is used to record it (that's not elegant).
///
/// We could possibly go wide when recording renderpasses, with one context per renderpass.
static void rendergraph_execute( le_rendergraph_o *self, size_t frameIndex, le_backend_o *backend ) {

	if ( PRINT_DEBUG_MESSAGES ) {
		std::ostringstream msg;
		msg << std::endl
		    << std::endl;
		msg << "Render graph: " << std::endl;

		for ( const auto &pass : self->passes ) {
			msg << "renderpass: '" << pass->debugName << "' , sort_key: " << pass->sort_key << std::endl;

			le_image_attachment_info_t const *pImageAttachments   = nullptr;
			le_resource_handle_t const *      pResources          = nullptr;
			size_t                            numImageAttachments = 0;
			renderpass_get_image_attachments( pass, &pImageAttachments, &pResources, &numImageAttachments );

			for ( size_t i = 0; i != numImageAttachments; ++i ) {
				msg << "\t Attachment: '" << pResources[ i ].debug_name << std::endl; //"', last written to in pass: '" << pass_id_to_handle[ attachment->source_id ] << "'" << std::endl;
				msg << "\t load : " << std::setw( 10 ) << to_str( pImageAttachments[ i ].loadOp ) << std::endl;
				msg << "\t store: " << std::setw( 10 ) << to_str( pImageAttachments[ i ].storeOp ) << std::endl
				    << std::endl;
			}
		}
		std::cout << msg.str();
	}

	using namespace le_renderer;
	using namespace le_backend_vk;

	// Receive one allocator per pass -
	// allocators come from the frame's own pool
	auto const       ppAllocators = vk_backend_i.get_transient_allocators( backend, frameIndex, self->passes.size() );
	le_allocator_o **allocIt      = ppAllocators; // iterator over allocators - note that number of allocators must be identical with number of passes

	auto stagingAllocator = vk_backend_i.get_staging_allocator( backend, frameIndex );

	le_pipeline_manager_o *pipelineCache = vk_backend_i.get_pipeline_cache( backend ); // TODO: make pipeline cache either pass- or frame- local

	// Grab swapchain dimensions so that we may use these as defaults for
	// encoder extents if these cannot be initialised via renderpass extents.
	//
	// Note that this does not change the renderpass extents.
	le::Extent2D swapchain_extent{};
	vk_backend_i.get_swapchain_extent( backend, &swapchain_extent.width, &swapchain_extent.height );

	// Create one encoder per pass, and then record commands by calling the execute callback.

	const size_t numPasses = self->passes.size();

	for ( size_t i = 0; i != numPasses; ++i ) {

		auto &pass       = self->passes[ i ];
		auto &sort_index = self->sortIndices[ i ]; // passes with same sort_index may execute in parallel.

		if ( sort_index == ( ~0u ) ) {
			// Pass has been marked as non-contributing during rendergraph.build step.
			continue;
		}

		// ---------| invariant: pass may contribute

        if ( !pass->executeCallbacks.empty() ) {

			le::Extent2D encoder_extent{
                pass->width != 0 ? pass->width : swapchain_extent.width,   // Use pass extent unless it is 0, otherwise revert to swapchain_extent
                pass->height != 0 ? pass->height : swapchain_extent.height // Use pass extent unless it is 0, otherwise revert to swapchain_extent
			};

			pass->encoder = encoder_i.create( *allocIt, pipelineCache, stagingAllocator, encoder_extent ); // NOTE: we must manually track the lifetime of encoder!

			if ( pass->type == LeRenderPassType::LE_RENDER_PASS_TYPE_DRAW ) {

				// Set default scissor and viewport to full extent.

				le::Rect2D default_scissor[ 1 ] = {
				    {0, 0, encoder_extent.width, encoder_extent.height},
				};

				le::Viewport default_viewport[ 1 ] = {
				    {0.f, 0.f, float( encoder_extent.width ), float( encoder_extent.height ), 0.f, 1.f},
				};

				// setup encoder default viewport and scissor to extent
				encoder_i.set_scissor( pass->encoder, 0, 1, default_scissor );
				encoder_i.set_viewport( pass->encoder, 0, 1, default_viewport );
			}

			renderpass_run_execute_callback( pass ); // record draw commands into encoder

			allocIt++; // Move to next unused allocator
		}
	}

	// TODO: consolidate pipeline caches
}

// ----------------------------------------------------------------------

static void rendergraph_get_passes( le_rendergraph_o *self, le_renderpass_o ***pPasses, size_t *pNumPasses ) {
	*pPasses    = self->passes.data();
	*pNumPasses = self->passes.size();
}

// ----------------------------------------------------------------------

static void rendergraph_get_declared_resources( le_rendergraph_o *self, le_resource_handle_t const **p_resource_handles, le_resource_info_t const **p_resource_infos, size_t *p_resource_count ) {
	*p_resource_count   = self->declared_resources_id.size();
	*p_resource_handles = self->declared_resources_id.data();
	*p_resource_infos   = self->declared_resources_info.data();
}

// ----------------------------------------------------------------------

static le_render_module_o *render_module_create() {
	auto obj = new le_render_module_o();
	return obj;
}

// ----------------------------------------------------------------------

static void render_module_destroy( le_render_module_o *self ) {
	delete self;
}

// ----------------------------------------------------------------------

// TODO: make sure name for each pass is unique per rendermodule.
static void render_module_add_renderpass( le_render_module_o *self, le_renderpass_o *pass ) {
	// Note: we clone the pass here, as we can't be sure that the original pass will not fall out of scope
	// and be destroyed.
	self->passes.push_back( renderpass_clone( pass ) );
}

// ----------------------------------------------------------------------
// Builds rendergraph from render_module, calls `setup` callbacks on each renderpass which provides a
// `setup` callback.
// If renderpass provides a setup method, pass is only added to rendergraph if its setup
// method returns true. Discards contents of render_module at end.
static void render_module_setup_passes( le_render_module_o *self, le_rendergraph_o *rendergraph_ ) {

	for ( auto &pass : self->passes ) {
		// Call setup function on all passes, in order of addition to module
		//
		// Setup Function must:
		// + populate input attachments
		// + populate output attachments
		// + (optionally) add renderpass to graph builder.

		if ( renderpass_has_setup_callback( pass ) ) {
			if ( renderpass_run_setup_callback( pass ) ) {
				// if pass.setup() returns true, this means we shall add this pass to the graph
				// This means a transfer of ownership for pass: pass moves from module into graph_builder
				rendergraph_add_renderpass( rendergraph_, pass );
				pass = nullptr;
			} else {
				renderpass_destroy( pass );
				pass = nullptr;
			}
		} else {
			rendergraph_add_renderpass( rendergraph_, pass );
			pass = nullptr;
		}
	}

	// Move any resource ids and resource infos to rendergraph
	rendergraph_->declared_resources_id   = std::move( self->declared_resources_id );
	rendergraph_->declared_resources_info = std::move( self->declared_resources_info );

	self->passes.clear();
};

// ----------------------------------------------------------------------

static void render_module_declare_resource( le_render_module_o *self, le_resource_handle_t const &resource_id, le_resource_info_t const &info ) {
	self->declared_resources_id.emplace_back( resource_id );
	self->declared_resources_info.emplace_back( info );
}

// ----------------------------------------------------------------------

void register_le_rendergraph_api( void *api_ ) {

	auto le_renderer_api_i = static_cast<le_renderer_api *>( api_ );

	auto &le_render_module_i            = le_renderer_api_i->le_render_module_i;
	le_render_module_i.create           = render_module_create;
	le_render_module_i.destroy          = render_module_destroy;
	le_render_module_i.add_renderpass   = render_module_add_renderpass;
	le_render_module_i.setup_passes     = render_module_setup_passes;
	le_render_module_i.declare_resource = render_module_declare_resource;

	auto &le_rendergraph_i                  = le_renderer_api_i->le_rendergraph_i;
	le_rendergraph_i.create                 = rendergraph_create;
	le_rendergraph_i.destroy                = rendergraph_destroy;
	le_rendergraph_i.reset                  = rendergraph_reset;
	le_rendergraph_i.build                  = rendergraph_build;
	le_rendergraph_i.execute                = rendergraph_execute;
	le_rendergraph_i.get_passes             = rendergraph_get_passes;
	le_rendergraph_i.get_declared_resources = rendergraph_get_declared_resources;

	auto &le_renderpass_i                        = le_renderer_api_i->le_renderpass_i;
	le_renderpass_i.create                       = renderpass_create;
	le_renderpass_i.clone                        = renderpass_clone;
	le_renderpass_i.destroy                      = renderpass_destroy;
	le_renderpass_i.get_id                       = renderpass_get_id;
	le_renderpass_i.get_debug_name               = renderpass_get_debug_name;
	le_renderpass_i.get_type                     = renderpass_get_type;
	le_renderpass_i.get_width                    = renderpass_get_width;
	le_renderpass_i.set_width                    = renderpass_set_width;
	le_renderpass_i.set_sample_count             = renderpass_set_sample_count;
	le_renderpass_i.get_sample_count             = renderpass_get_sample_count;
	le_renderpass_i.get_height                   = renderpass_get_height;
	le_renderpass_i.set_height                   = renderpass_set_height;
	le_renderpass_i.set_setup_callback           = renderpass_set_setup_callback;
	le_renderpass_i.has_setup_callback           = renderpass_has_setup_callback;
	le_renderpass_i.set_execute_callback         = renderpass_set_execute_callback;
	le_renderpass_i.has_execute_callback         = renderpass_has_execute_callback;
	le_renderpass_i.set_is_root                  = renderpass_set_is_root;
	le_renderpass_i.get_is_root                  = renderpass_get_is_root;
	le_renderpass_i.get_sort_key                 = renderpass_get_sort_key;
	le_renderpass_i.set_sort_key                 = renderpass_set_sort_key;
	le_renderpass_i.add_color_attachment         = renderpass_add_color_attachment;
	le_renderpass_i.add_depth_stencil_attachment = renderpass_add_depth_stencil_attachment;
	le_renderpass_i.get_image_attachments        = renderpass_get_image_attachments;
	le_renderpass_i.use_resource                 = renderpass_use_resource;
	le_renderpass_i.get_used_resources           = renderpass_get_used_resources;
	le_renderpass_i.steal_encoder                = renderpass_steal_encoder;
	le_renderpass_i.sample_texture               = renderpass_sample_texture;
	le_renderpass_i.get_texture_ids              = renderpass_get_texture_ids;
	le_renderpass_i.get_texture_infos            = renderpass_get_texture_infos;
	le_renderpass_i.ref_inc                      = renderpass_ref_inc;
	le_renderpass_i.ref_dec                      = renderpass_ref_dec;
}

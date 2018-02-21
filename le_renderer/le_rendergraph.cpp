#include "le_rendergraph.h"
#include "le_backend_vk/le_backend_vk.h"

#include <vector>
#include <string>
#include <list>
#include <assert.h>
#include <algorithm>
#include <unordered_map>
#include <iostream>
#include <iomanip>
#include "vulkan/vulkan.hpp"

#define LE_RENDERPASS_MARKER_EXTERNAL "rp-external"
#define LE_GRAPH_BUILDER_RECURSION_DEPTH 20

using image_attachment_t = le_rendergraph_api::image_attachment_info_o;

// TODO: const_char_hash64 and fnv_hash64 must return the same hash value
// for the same string - currently, this is not the case, most probably,
// because the recursive function does hashing in the opposite direction

// ----------------------------------------------------------------------
// FNV hash using constexpr recursion over char string length (may execute at compile time)
inline uint64_t constexpr const_char_hash64( const char *input ) noexcept {
	return *input ? ( 0x100000001b3 * const_char_hash64( input + 1 ) ) ^ static_cast<uint64_t>( *input ) : 0xcbf29ce484222325;
}

template <typename T>
inline uint64_t fnv_hash64( const T &input_, size_t num_bytes_) noexcept {
	const char* input = reinterpret_cast<const char*>(&input_);
	size_t hash = 0xcbf29ce484222325;
	// note that we iterate backwards - this is so that the hash matches
	// the constexpr version which uses recursion.
	for (const char *p = input + num_bytes_; p != input; ){
		hash = ( 0x100000001b3 * hash ) ^ static_cast<uint64_t>( *(--p) );
	}
	return hash;
}


inline uint32_t constexpr const_char_hash32( const char *input ) noexcept {
	return *input ? ( 0x1000193 * const_char_hash32( input + 1 ) ) ^ static_cast<uint32_t>( *input ) : 0x811c9dc5;
}

struct IdentityHash {
	size_t operator()( const uint64_t &key_ ) const noexcept {
		return key_;
	}
};

// ----------------------------------------------------------------------

struct le_renderpass_o {

	uint64_t                        id;
	uint64_t                        execution_order = 0;
	std::vector<image_attachment_t> imageAttachments;
	// std::vector<buffer_attachment_t> bufferAttachments;

	le_rendergraph_api::pfn_renderpass_setup_t callbackSetup = nullptr;

	char         debugName[ 32 ];
};

// ----------------------------------------------------------------------

struct le_render_module_o : NoCopy, NoMove {
	le_render_module_o(le_backend_vk_device_o* device_)
	    : device(device_){

	}

	le_backend_vk_device_o* device;
	std::vector<le_renderpass_o> passes;
};

// ----------------------------------------------------------------------

struct le_graph_builder_o : NoCopy, NoMove {
	std::vector<le_renderpass_o> passes;
};

// ----------------------------------------------------------------------

static le_renderpass_o *renderpass_create(const char* renderpass_name) {
	auto self = new le_renderpass_o();
	self->id = const_char_hash64(renderpass_name);
	strncpy(self->debugName,renderpass_name,sizeof(self->debugName));
	return self;
}

// ----------------------------------------------------------------------

static void renderpass_destroy( le_renderpass_o *self ) {
	delete self;
}

// ----------------------------------------------------------------------

static void renderpass_set_setup_fun(le_renderpass_o*self, le_rendergraph_api::pfn_renderpass_setup_t fun){
	self->callbackSetup = fun;
}

// ----------------------------------------------------------------------

static void renderpass_add_image_attachment(le_renderpass_o*self, const char* name_, le_rendergraph_api::image_attachment_info_o* info_){
	// TODO: annotate the current renderpass to name of output attachment
	auto info = *info_;

	info.id = const_char_hash64(name_);

	// By default, flag attachment source as being external, if attachment was previously written in this pass,
	// source will be substituted by id of pass which writes to attachment, otherwise the flag will persist and
	// tell us that this attachment must be externally resolved.
	info.source_id = const_char_hash64(LE_RENDERPASS_MARKER_EXTERNAL);

	if ( info.access_flags == le::AccessFlagBits::eReadWrite ) {
		info.loadOp  = vk::AttachmentLoadOp::eLoad;
		info.storeOp = vk::AttachmentStoreOp::eStore;
	} else if ( info.access_flags & le::AccessFlagBits::eWrite ) {
		// Write-only means we may be seen as the creator of this resource
		info.source_id = self->id;
	} else if ( info.access_flags & le::AccessFlagBits::eRead ) {
		// TODO: we need to make sure to distinguish between image attachments and texture attachments
		info.loadOp  = vk::AttachmentLoadOp::eLoad;
		info.storeOp = vk::AttachmentStoreOp::eDontCare;
	} else {
		info.loadOp  = vk::AttachmentLoadOp::eDontCare;
		info.storeOp = vk::AttachmentStoreOp::eDontCare;
	}

	strncpy( info.debugName, name_, sizeof(info.debugName));

	self->imageAttachments.emplace_back(std::move(info));
}


// ----------------------------------------------------------------------

static le_render_module_o *render_module_create( le_backend_vk_device_o *device_ ) {
	auto obj = new le_render_module_o( device_ );
	return obj;
}

// ----------------------------------------------------------------------

static void render_module_destroy(le_render_module_o* self){
	delete self;
}

// ----------------------------------------------------------------------

static void render_module_add_renderpass(le_render_module_o*self, le_renderpass_o* pass){
	// TODO: make sure name for each pass is unique per rendermodule.
	self->passes.emplace_back(*pass);
}

// ----------------------------------------------------------------------

static void render_module_build_graph(le_render_module_o* self, le_graph_builder_o* graph_builder_){

	le::GraphBuilder graphBuilder{graph_builder_};

	for (auto &pass: self->passes){
		// Call setup function on all passes, in order of addition to module
		//
		// Setup Function must:
		// + populate input attachments
		// + populate output attachments
		// + (optionally) add renderpass to graph builder.
		assert(pass.callbackSetup != nullptr);
		if (pass.callbackSetup(&pass, self->device)){
			// if pass.setup() returns true, this means we shall add this pass to the graph
			graphBuilder.addRenderpass(&pass);
		}
	}
	// Now, renderpasses should have their attachments properly set.
	// Further, user will have added all renderpasses they wanted included in the module
	// to the graph builder.

	// The graph builder now has a list of all passes which contribute to the current module.

	// Step 1: Validate
	// - find any name clashes: inputs and outputs for each renderpass must be unique.
	// Step 2: sort passes in dependency order (by adding an execution order index to each pass)
	// Step 3: add  markers to each attachment for each pass, depending on their read/write status

	graphBuilder.buildGraph();

};

// ----------------------------------------------------------------------

static void render_module_execute_graph(le_render_module_o* self, le_graph_builder_o* graph_builder){

	std::ostringstream msg;

	msg << "render graph: " << std::endl;
	for ( const auto &pass : graph_builder->passes ) {
		msg << "renderpass: " << std::setw( 15 ) << std::hex << pass.id << ", " << "'" << pass.debugName << "' , order: " << pass.execution_order << std::endl;

		for ( const auto &attachment : pass.imageAttachments ) {
			if (attachment.access_flags & le::AccessFlagBits::eRead){
				msg << "r";
			}
			if (attachment.access_flags & le::AccessFlagBits::eWrite){
				msg << "w";
			}
			msg << " : " << std::setw( 32 ) << std::hex << attachment.id << ":" << attachment.source_id << ", '" << attachment.debugName << "'" << std::endl;
		}
	}
	std::cout << msg.str() << std::endl;


	/*

	  This method creates api-independent command lists for draw commands

	  We need to track the state of our resources - especially for texture
	  resources - can they change layout?.

	*/


}

// ----------------------------------------------------------------------

static le_graph_builder_o* graph_builder_create(){
	auto obj = new le_graph_builder_o();
	return obj;
}

// ----------------------------------------------------------------------

static void graph_builder_reset(le_graph_builder_o* self){
	self->passes.clear();
}

// ----------------------------------------------------------------------

static void graph_builder_destroy(le_graph_builder_o* self){
	delete self;
}

// ----------------------------------------------------------------------

static void graph_builder_add_renderpass(le_graph_builder_o* self, le_renderpass_o* renderpass){
	self->passes.emplace_back(*renderpass);
}

// ----------------------------------------------------------------------
/// \brief find corresponding output for each input attachment,
/// and tag input with output id
static void graph_builder_resolve_attachment_ids( std::vector<le_renderpass_o> &passes ) {

	// Rendermodule gives us a pre-sorted list of renderpasses,
	// we use this to resolve attachment aliases. Since Rendermodule is a linear sequence,
	// this means that dependencies for resources are well-defined. It's impossible for
	// two renderpasses using the same resource not to have a clearly defined priority, as
	// the earliest submitted renderpasses of the two will get priority.

	// map from output id -> source id
	std::unordered_map<uint64_t, uint64_t, IdentityHash> writeAttachmentTable;

	// We go through passes in module submission order, so that outputs will match later inputs.
	for ( auto &pass : passes ) {

		// We must first look if any of our READ attachments are already present in the attachment table.
		// If so, we update source ids (from table) for each attachment we found.
		for ( auto &attachment : pass.imageAttachments ) {
			if ( attachment.access_flags & le::AccessFlagBits::eRead ) {
				auto foundOutputIt = writeAttachmentTable.find( attachment.id );
				if ( foundOutputIt != writeAttachmentTable.end() ) {
					attachment.source_id = foundOutputIt->second;
				}
			}
		}

		// Outputs from current pass overwrite any cached outputs with same name:
		// later inputs with same name will then resolve to the latest version
		// of an output with a particular name.
		for ( auto &attachment : pass.imageAttachments ) {
			if ( attachment.access_flags & le::AccessFlagBits::eWrite ) {
				writeAttachmentTable[ attachment.id ] = pass.id;
			}
		}
	}

	// TODO: check if all resources have an associated source id, because
	// resources without source ID are unresolved.
}

// ----------------------------------------------------------------------
// depth-first traversal of graph, following each input to its corresponding output
static void graph_builder_traverse_passes( const std::unordered_map<uint64_t, le_renderpass_o *, IdentityHash> &passes,
                                           const uint64_t &                                                     sourceRenderpassId,
                                           const uint64_t                                                       recursion_depth ) {



	if ( recursion_depth > LE_GRAPH_BUILDER_RECURSION_DEPTH ) {
		std::cerr << __FUNCTION__ << " : "
		          << "max recursion level reached. check for cycles in render graph" << std::endl;
		return;
	}

	if (sourceRenderpassId == const_char_hash64(LE_RENDERPASS_MARKER_EXTERNAL)){
		// TODO: how do we deal with external resources?
		// std::cerr << __FUNCTION__ << " : READ to attachment referenced which was not previously written in this graph." << std::endl;
		return;
	}

	// as each input tells us its source renderpass,
	// we can look up the provider pass for each source by id
	auto &sourcePass = passes.at( sourceRenderpassId );

	// We want the maximum edge distance (one recursion equals one edge) from the root node
	// for each pass, since the max distance makes sure that all resources are available,
	// even resources which have a shorter path.
	sourcePass->execution_order = std::max( recursion_depth, sourcePass->execution_order );

	for ( auto &attachment: sourcePass->imageAttachments ) {
		if (attachment.access_flags & le::AccessFlagBits::eRead){
			graph_builder_traverse_passes( passes, attachment.source_id, recursion_depth + 1 );
		}
	}

}

// ----------------------------------------------------------------------

// Re-order renderpasses in topological order, based
// on graph dependencies
static void graph_builder_order_passes( std::vector<le_renderpass_o> &passes ) {

	// ----------| invariant: resources know their source id

	std::unordered_map<uint64_t, le_renderpass_o *, IdentityHash> passTable;
	passTable.reserve( passes.size() );

	for ( auto &p : passes ) {
		passTable.emplace( p.id, &p );
	}

	// Note that we set the lowest execution order to 1, so that any passes with order 0
	// are flagged as non-contributing (these may be pruned)
	graph_builder_traverse_passes( passTable, const_char_hash64( "root" ), 1 );

	// remove any passes which are not contributing
	auto endIt = std::remove_if( passes.begin(), passes.end(), []( const le_renderpass_o &pass ) -> bool {
		return pass.execution_order == 0;
	} );
	passes.erase( endIt, passes.end() );

	// sort passes by execution order
	// Note: we use stable sort so that write-after-write is performed in module submission order.
	std::stable_sort( passes.begin(), passes.end(), []( const le_renderpass_o &lhs, const le_renderpass_o &rhs ) -> bool {
		return lhs.execution_order > rhs.execution_order;
	} );
}

static inline bool is_depth_stencil_format(vk::Format format_){
	return (format_ >= vk::Format::eD16Unorm && format_ <= vk::Format::eD32SfloatS8Uint);
}

// ----------------------------------------------------------------------

static void graph_builder_create_resource_table(le_graph_builder_o* self){

	// we want a list of unique resources referenced in the renderpass,
	// and for each resource we must know its first reference.

	// we also need to know if there are any external resources

	// then, we go through all passes, and we track the resource state for each resource

	struct ResourceInfo {
		uint64_t   id = 0;
		vk::Format format;
	};

	std::unordered_map<uint64_t, ResourceInfo, IdentityHash> resourceTable;

	for (auto & pass : self->passes){
		for (auto &resource : pass.imageAttachments){
			ResourceInfo info;
			info.id = resource.id;
			info.format = resource.format;
			auto result = resourceTable.insert({info.id,info});
			if (!result.second){
				if (result.first->second.format != info.format){
					std::cerr << "WARNING: Resource '" << resource.debugName << "' re-defined with incompatible format." << std::endl;
				}
			}
		}
	}

	// track resource state

	// we should mark persistent resources which are not frame-local with special flags, so that they
	// come with an initial element in their sync chain, this element signals their last (frame-crossing) state
	// this naturally applies to "backbuffer", for example.

	struct ResourceState {
		vk::AccessFlags        access;                               // last memory write access
		vk::PipelineStageFlags stage;                                // last known stage using write access
		vk::ImageLayout        layout = vk::ImageLayout::eUndefined; // last known image layout
		vk::Format             format = vk::Format::eUndefined;      // last known image format
	};

	// With `syncChainTable` and image_attachment_info_o.syncState, we should
	// be able to create renderpasses. Each resource has a sync chain, and each attachment_info
	// has a struct which holds indices into the sync chain telling us where to look
	// up the sync state for a resource at different stages of renderpass construction.

	std::unordered_map<uint64_t, std::vector<ResourceState>, IdentityHash> syncChainTable;

	for ( auto &resource : resourceTable ) {
		syncChainTable[ resource.first ].push_back( ResourceState{} );
	}

	// TODO: frame-external ("persistent") resources such as backbuffer
	// need to be correctly initialised:
	//
	auto &backbufferState     = syncChainTable[ const_char_hash64( "backbuffer" ) ].front();
	backbufferState.stage  = vk::PipelineStageFlagBits::eColorAttachmentOutput; // we need this, since semaphore waits on this stage
	backbufferState.access = vk::AccessFlagBits( 0 );                           // semaphore took care of availability - we can assume memory is already available

	// * sync state: ready to enter renderpass: colorattachmentOutput=visible *

	// we use this to mask out any reads dstAccess, as it doesn't make sense to
	const auto ANY_WRITE_ACCESS_FLAGS = ( vk::AccessFlagBits::eColorAttachmentWrite |
	                                      vk::AccessFlagBits::eCommandProcessWriteNVX |
	                                      vk::AccessFlagBits::eCommandProcessWriteNVX |
	                                      vk::AccessFlagBits::eDepthStencilAttachmentWrite |
	                                      vk::AccessFlagBits::eHostWrite |
	                                      vk::AccessFlagBits::eMemoryWrite |
	                                      vk::AccessFlagBits::eShaderWrite |
	                                      vk::AccessFlagBits::eTransferWrite );

	for ( auto &pass : self->passes ) {
		for ( auto &resource : pass.imageAttachments ) {

			auto &info      = resourceTable[ resource.id ];
			auto &syncChain = syncChainTable[ resource.id ];

			bool isDepthStencil = is_depth_stencil_format( resource.format );

			// Renderpass implicit sync (per resource):
			// + enter renderpass : initial layout (layout must match)
			// + enter subpass
			// + load/clear op (executed once before first use per-resource)
			// + layout transform if initial layout and attachment reference layout differ for subpass
			// + command execution
			// + store op
			// + exit subpass : final layout
			// + layout transform (if final layout differs)

			{
				auto & previousSyncState = syncChain.back();
				ResourceState resourceFirstUse{previousSyncState};
				resourceFirstUse.format    = info.format;

				switch ( resource.access_flags ) {
				case le::AccessFlagBits::eReadWrite:
					// resource.loadOp must be LOAD

					// we must now specify which stages need to be visible for which coming memory access
					if ( isDepthStencil ) {
						resourceFirstUse.stage  = vk::PipelineStageFlagBits::eEarlyFragmentTests;
						resourceFirstUse.access = vk::AccessFlagBits::eDepthStencilAttachmentRead;
					} else {
						// we need to make visible the information from color attachment output stage
						// to anyone using read or write on the color attachment.
						resourceFirstUse.stage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
						resourceFirstUse.access =  vk::AccessFlagBits::eColorAttachmentRead;
					}
				    break;

				case le::AccessFlagBits::eWrite:
					// resource.loadOp must be either CLEAR / or DONT_CARE
					resourceFirstUse.layout = vk::ImageLayout::eUndefined; // override to undefined to invalidate attachment which will be cleared.
					resourceFirstUse.stage = isDepthStencil ? vk::PipelineStageFlagBits::eEarlyFragmentTests : vk::PipelineStageFlagBits::eColorAttachmentOutput;
					resourceFirstUse.access =  vk::AccessFlagBits(0); // no need to flush "stale" cache, if we're overwriting this anyway.
				    break;

				case le::AccessFlagBits::eRead:
				    break;
				}

				resource.syncState.idxInitial = syncChain.size();
				syncChain.emplace_back( std::move( resourceFirstUse ) ); // attachment initial state for a renderpass - may be loaded/cleared on first use
				    // * sync state: ready for load/store *
			}

			{
				// Nothing changes in terms of sync, only the layout may change - layout transitions happen outside the regular pipeline,
				// they happen-after availability operations, and happen-before the next visibility operations
				ResourceState subpassLayoutTransition{syncChain.back()};

				if ( resource.access_flags == le::AccessFlagBits::eRead ) {

					// TODO: implement READ attachment

				} else if ( resource.access_flags & le::AccessFlagBits::eWrite ) {
					// same for both write and read/write
					// resource.loadOp must be either clear / or don't care
					subpassLayoutTransition.layout = is_depth_stencil_format( resource.format ) ? vk::ImageLayout::eDepthStencilAttachmentOptimal : vk::ImageLayout::eColorAttachmentOptimal;
				}

				resource.syncState.idxSubpassLayoutTransition = syncChain.size();
				syncChain.emplace_back( std::move( subpassLayoutTransition ) ); // subpass layout transition
			}

			{
				auto & previousSyncState = syncChain.back();
				ResourceState externalToSubpassDependency{previousSyncState};

				if ( resource.access_flags == le::AccessFlagBits::eReadWrite ) {
					// resource.loadOp most be LOAD

					// we must now specify which stages need to be visible for which coming memory access
					if ( isDepthStencil ) {
						externalToSubpassDependency.stage = vk::PipelineStageFlagBits::eEarlyFragmentTests;
						externalToSubpassDependency.access = vk::AccessFlagBits::eDepthStencilAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentRead;
					} else {
						// we need to make visible the information from color attachment output stage
						// to anyone using read or write on the color attachment.
						externalToSubpassDependency.stage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
						externalToSubpassDependency.access = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eColorAttachmentRead;
					}

				} else if ( resource.access_flags & le::AccessFlagBits::eRead ) {
				} else if ( resource.access_flags & le::AccessFlagBits::eWrite ) {

					externalToSubpassDependency.stage = isDepthStencil ? vk::PipelineStageFlagBits::eEarlyFragmentTests : vk::PipelineStageFlagBits::eColorAttachmentOutput;
					externalToSubpassDependency.access = isDepthStencil ? vk::AccessFlagBits::eDepthStencilAttachmentWrite : vk::AccessFlagBits::eColorAttachmentWrite;
				}

				resource.syncState.idxExternalToSubpass = syncChain.size();
				syncChain.emplace_back( std::move( externalToSubpassDependency ) );
			}

			// ... NOTE: if resource is modified by commands inside the renderpass, this needs to be added to the sync chain here.

			{
				// Whichever next resource state will be in the sync chain will be the resource state we should transition to
				// when defining the last_subpass_to_external dependency
				// which is why, optimistically, we designate the index of the next, not yet written state here -
				resource.syncState.idxFinal = syncChain.size();
			}

			// print out info for this resource at this pass.
		}
	}

	// TODO: add final states for resources which are permanent - or are used on another queue
	// this includes backbuffer, and makes sure the backbuffer transitions to the correct state in its last
	// subpass dependency.

	for (auto & syncChainPair : syncChainTable){
		const auto & id = syncChainPair.first;
		auto & syncChain = syncChainPair.second;

		ResourceState finalState{syncChain.back()};

		if (id == const_char_hash64( "backbuffer" )){
			finalState.stage  = vk::PipelineStageFlagBits::eBottomOfPipe;
			finalState.access = vk::AccessFlagBits::eMemoryRead;
			finalState.layout    = vk::ImageLayout::ePresentSrcKHR;
		} else {
			// we mimick implicit dependency here, which exists for a final subpass
			// see p.210 vk spec (chapter 7, render pass)
			finalState.stage = vk::PipelineStageFlagBits::eBottomOfPipe;
			finalState.access = vk::AccessFlagBits(0);
		}

		syncChain.emplace_back(std::move(finalState));
	}


	// create renderpasses

	for ( auto &pass : self->passes ) {

		std::vector<vk::AttachmentDescription> attachments;
		attachments.reserve(pass.imageAttachments.size());

		std::unordered_map<uint64_t, image_attachment_t::SyncState, IdentityHash> syncStates;
		std::vector<vk::AttachmentReference> colorAttachmentReferences;

		vk::PipelineStageFlags srcStageFromExternalFlags;
		vk::PipelineStageFlags dstStageFromExternalFlags;
		vk::AccessFlags srcAccessFromExternalFlags;
		vk::AccessFlags dstAccessFromExternalFlags;

		vk::PipelineStageFlags srcStageToExternalFlags;
		vk::PipelineStageFlags dstStageToExternalFlags;
		vk::AccessFlags srcAccessToExternalFlags;
		vk::AccessFlags dstAccessToExternalFlags;


		for ( auto &attachment : pass.imageAttachments ) {
			auto &syncChain                   = syncChainTable.at( attachment.id );
			auto &syncIndices                 = attachment.syncState;
			auto &syncInitial                 = syncChain.at( syncIndices.idxInitial );
			auto &syncFromExternal            = syncChain.at( syncIndices.idxExternalToSubpass );
			auto &syncSubpassLayoutTransition = syncChain.at( syncIndices.idxSubpassLayoutTransition );
			auto &syncFinal                   = syncChain.at( syncIndices.idxFinal );

			bool isDepthStencil = is_depth_stencil_format( syncInitial.format );

			vk::AttachmentDescription attachmentDescription;
			attachmentDescription
			    .setFlags          ( vk::AttachmentDescriptionFlags() )
			    .setFormat         ( syncInitial.format )
			    .setSamples        ( vk::SampleCountFlagBits::e1 )
			    .setLoadOp         ( isDepthStencil ? vk::AttachmentLoadOp::eDontCare  : attachment.loadOp )
			    .setStoreOp        ( isDepthStencil ? vk::AttachmentStoreOp::eDontCare : attachment.storeOp )
			    .setStencilLoadOp  ( isDepthStencil ? attachment.loadOp                : vk::AttachmentLoadOp::eDontCare)
			    .setStencilStoreOp ( isDepthStencil ? attachment.storeOp               : vk::AttachmentStoreOp::eDontCare)
			    .setInitialLayout  ( syncInitial.layout )
			    .setFinalLayout    ( syncFinal.layout )
			    ;

			std::cout << "attachment: " << attachment.debugName << std::endl;
			std::cout << "layout initial: " << vk::to_string(syncInitial.layout) << std::endl;
			std::cout << "layout subpass: " << vk::to_string(syncSubpassLayoutTransition.layout) << std::endl;
			std::cout << "layout   final: " << vk::to_string(syncFinal.layout) << std::endl;
			
			attachments.emplace_back(attachmentDescription);
			colorAttachmentReferences.emplace_back( attachments.size() - 1, syncSubpassLayoutTransition.layout );

			srcStageFromExternalFlags |= syncInitial.stage;
			dstStageFromExternalFlags |= syncFromExternal.stage;
			srcAccessFromExternalFlags |= (syncInitial.access & ANY_WRITE_ACCESS_FLAGS);
			dstAccessFromExternalFlags |= syncFromExternal.access;

			srcStageToExternalFlags |= syncChain.at(syncIndices.idxFinal-1).stage;
			dstStageToExternalFlags |= syncFinal.stage;
			srcAccessToExternalFlags |= (syncChain.at(syncIndices.idxFinal-1).access & ANY_WRITE_ACCESS_FLAGS);
			dstAccessToExternalFlags |= syncFinal.access;

		}

		std::vector<vk::SubpassDescription>    subpasses;
		subpasses.reserve(1);

		vk::SubpassDescription subpassDescription;
		subpassDescription
		        .setFlags                   ( vk::SubpassDescriptionFlags() )
		        .setPipelineBindPoint       ( vk::PipelineBindPoint::eGraphics )
		        .setInputAttachmentCount    ( 0 )
		        .setPInputAttachments       ( nullptr )
		        .setColorAttachmentCount    ( uint32_t(colorAttachmentReferences.size()) )
		        .setPColorAttachments       ( colorAttachmentReferences.data() )
		        .setPResolveAttachments     ( nullptr )
		        .setPDepthStencilAttachment ( nullptr )
		        .setPreserveAttachmentCount ( 0 )
		        .setPPreserveAttachments    ( nullptr )
		        ;

		subpasses.emplace_back(subpassDescription);

		std::vector<vk::SubpassDependency>     dependencies;
		dependencies.reserve(2);
		{
			std::cout << "PASS :'" << pass.debugName << "'" << std::endl;
			std::cout << "- external to subpass -" << std::endl;
			std::cout << "\t srcStage: " << vk::to_string(srcStageFromExternalFlags ) << std::endl;
			std::cout << "\t dstStage: " << vk::to_string(dstStageFromExternalFlags ) << std::endl;
			std::cout << "\tsrcAccess: " << vk::to_string(srcAccessFromExternalFlags) << std::endl;
			std::cout << "\tdstAccess: " << vk::to_string(dstAccessFromExternalFlags) << std::endl;
			
			std::cout << "- subpass to external -" << std::endl;
			std::cout << "\t srcStage: " << vk::to_string( srcStageToExternalFlags ) << std::endl;
			std::cout << "\t dstStage: " << vk::to_string( dstStageToExternalFlags ) << std::endl;
			std::cout << "\tsrcAccess: " << vk::to_string( srcAccessToExternalFlags) << std::endl;
			std::cout << "\tdstAccess: " << vk::to_string( dstAccessToExternalFlags) << std::endl;
			
			
			vk::SubpassDependency externalToSubpassDependency;
			externalToSubpassDependency
			    .setSrcSubpass      ( VK_SUBPASS_EXTERNAL )                // outside of renderpass
			    .setDstSubpass      ( 0 )                                  // first subpass
			    .setSrcStageMask    ( srcStageFromExternalFlags )
			    .setDstStageMask    ( dstStageFromExternalFlags )
			    .setSrcAccessMask   ( srcAccessFromExternalFlags )
			    .setDstAccessMask   ( dstAccessFromExternalFlags )
			    .setDependencyFlags ( vk::DependencyFlagBits::eByRegion )
			    ;
			vk::SubpassDependency subpassToExternalDependency;
			subpassToExternalDependency
			    .setSrcSubpass      ( 0 )                                  // last subpass
			    .setDstSubpass      ( VK_SUBPASS_EXTERNAL )                // outside of renderpass
			    .setSrcStageMask    ( srcStageToExternalFlags )
			    .setDstStageMask    ( dstStageToExternalFlags )
			    .setSrcAccessMask   ( srcAccessToExternalFlags )
			    .setDstAccessMask   ( dstAccessToExternalFlags )
			    .setDependencyFlags ( vk::DependencyFlagBits::eByRegion )
			    ;

			dependencies.emplace_back(std::move(externalToSubpassDependency));
			dependencies.emplace_back(std::move(subpassToExternalDependency));
		}

		vk::RenderPassCreateInfo renderpassCreateInfo;
		renderpassCreateInfo
		    .setAttachmentCount ( uint32_t(attachments.size())  )
		    .setPAttachments    ( attachments.data()  )
		    .setSubpassCount    ( uint32_t(subpasses.size())    )
		    .setPSubpasses      ( subpasses.data()    )
		    .setDependencyCount ( uint32_t(dependencies.size()) )
		    .setPDependencies   ( dependencies.data() )
		    ;
	}
}

// ----------------------------------------------------------------------

static void graph_builder_build_graph(le_graph_builder_o* self){

	// Find corresponding output for each input attachment,
	// and tag input with output id, as dependencies are
	// declared using names rather than linked in code.
	graph_builder_resolve_attachment_ids(self->passes);

	// First, we must establish a toplogical sort order
	// so that passes which produce resources for other
	// passes are executed *before* their dependencies
	//
	// We use the passes' sort order as a field in the
	// sorting key for any command buffers associated with that
	// renderpass.
	graph_builder_order_passes(self->passes);


	graph_builder_create_resource_table(self);
}

// ----------------------------------------------------------------------

void register_le_rendergraph_api( void *api_ ) {

	auto  le_rendergraph_api_i = static_cast<le_rendergraph_api *>( api_ );
	auto &le_renderpass_i      = le_rendergraph_api_i->le_renderpass_i;
	auto &le_render_module_i   = le_rendergraph_api_i->le_render_module_i;
	auto &le_graph_builder_i   = le_rendergraph_api_i->le_graph_builder_i;

	le_renderpass_i.create                = renderpass_create;
	le_renderpass_i.destroy               = renderpass_destroy;
	le_renderpass_i.add_image_attachment  = renderpass_add_image_attachment;
	le_renderpass_i.set_setup_fun         = renderpass_set_setup_fun;

	le_render_module_i.create         = render_module_create;
	le_render_module_i.destroy        = render_module_destroy;
	le_render_module_i.add_renderpass = render_module_add_renderpass;
	le_render_module_i.build_graph    = render_module_build_graph;
	le_render_module_i.execute_graph  = render_module_execute_graph;

	le_graph_builder_i.create         = graph_builder_create;
	le_graph_builder_i.destroy        = graph_builder_destroy;
	le_graph_builder_i.reset          = graph_builder_reset;
	le_graph_builder_i.add_renderpass = graph_builder_add_renderpass;
	le_graph_builder_i.build_graph    = graph_builder_build_graph;
}

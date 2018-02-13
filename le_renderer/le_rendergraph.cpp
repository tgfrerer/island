#include "le_rendergraph.h"
#include "le_backend_vk/le_backend_vk.h"

#include <vector>
#include <string>
#include <assert.h>
#include <algorithm>
#include <unordered_map>
#include <iostream>
#include <iomanip>
#include "vulkan/vulkan.hpp"

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

	// For each output attachment, we must know
	// the LOAD OP- it can be any of these:
	// LOAD, CLEAR, DONT_CARE
	// if it were CLEAR, we must provide a clear
	// value.

	uint64_t                        id;

	std::vector<image_attachment_t> imageAttachments;

//	std::vector<buffer_attachment_t> inputBufferAttachments;
//	std::vector<buffer_attachment_t> outputBufferAttachments;

	le_rendergraph_api::pfn_renderpass_setup_t callbackSetup = nullptr;

	struct graph_info_t {
		uint64_t execution_order = 0;
	};
	graph_info_t graphInfo;
};

struct le_render_module_o {
	std::vector<le_renderpass_o*> passes;
};

struct le_graph_builder_o {
	le_backend_vk_device_o* device;
	std::vector<le_renderpass_o> passes;

	le_graph_builder_o(le_backend_vk_device_o* device_)
	    :device(device_){
	}

	~le_graph_builder_o() = default;

	// copy assignment operator
	le_graph_builder_o& operator=(const le_graph_builder_o& lhs) = delete ;

	// move assignment operator
	le_graph_builder_o& operator=(const le_graph_builder_o&& lhs) = delete;

};

// ----------------------------------------------------------------------

static le_renderpass_o *renderpass_create(const char* renderpass_name) {
	auto self = new le_renderpass_o();
	self->id = const_char_hash64(renderpass_name);
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

	if ( info.access_flags == le::AccessFlagBits::eReadWrite ) {
		info.loadOp  = vk::AttachmentLoadOp::eLoad;
		info.storeOp = vk::AttachmentStoreOp::eStore;
	} else if ( info.access_flags & le::AccessFlagBits::eWrite ) {
		// write-only means clear before -- TODO: we need to check that there is a clear callback, though.
		info.loadOp  = vk::AttachmentLoadOp::eClear;
		info.storeOp = vk::AttachmentStoreOp::eStore;
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

static le_render_module_o* render_module_create(){
	auto obj = new le_render_module_o();
	return obj;
}

// ----------------------------------------------------------------------

static void render_module_destroy(le_render_module_o* self){
	delete self;
}

// ----------------------------------------------------------------------

static void render_module_add_renderpass(le_render_module_o*self, le_renderpass_o* pass){
	// note that we store a copy
	self->passes.emplace_back(pass);
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
		assert(pass->callbackSetup != nullptr);
		if (pass->callbackSetup(pass, graph_builder_->device)){
			// if pass.setup() returns true, this means we shall add this pass to the graph
			graphBuilder.addRenderpass(pass);
		}
	}
	// Now, renderpasses should have their attachment properly set.
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

static le_graph_builder_o* graph_builder_create(le_backend_vk_device_o* device){
	auto obj = new le_graph_builder_o(device);
	return obj;
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
	// the first submitted renderpasses of the two will get priority.

	// map from output id -> source id
	std::unordered_map<uint64_t, uint64_t, IdentityHash> attachmentTable;

	// We go through passes in module submission order, so that outputs will match later inputs.
	for ( auto &pass : passes ) {

		// We must first look if any of our READ attachments are already present in the attachment table.
		// If so, we update source ids (from table) for each attachment we found.
		for ( auto &attachment : pass.imageAttachments ) {
			if ( attachment.access_flags & le::AccessFlagBits::eRead ) {
				auto foundOutputIt = attachmentTable.find( attachment.id );
				if ( foundOutputIt != attachmentTable.end() ) {
					attachment.source_id = foundOutputIt->second;
				}
			}
		}

		// Outputs from current pass overwrite any cached outputs with same name:
		// later inputs with same name will then resolve to the latest version
		// of an output with a particular name.
		for ( auto &attachment : pass.imageAttachments ) {
			if ( attachment.access_flags & le::AccessFlagBits::eWrite ) {
				attachmentTable[ attachment.id ] = pass.id;
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

	static const size_t MAX_PASS_RECURSION_DEPTH = 20;

	if ( recursion_depth > MAX_PASS_RECURSION_DEPTH ) {
		std::cerr << __FUNCTION__ << " : "
		          << "max recursion level reached. check for cycles in render graph";
		return;
	}

	// as each input tells us its source renderpass,
	// we can look up the provider pass for each source by id
	auto &sourcePass = passes.at( sourceRenderpassId );

	// We want the maximum edge distance (one recursion equals one edge) from the root node
	// for each pass, since the max distance makes sure that all resources are available,
	// even resources which have a shorter path.
	sourcePass->graphInfo.execution_order = std::max( recursion_depth, sourcePass->graphInfo.execution_order );

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

	std::unordered_map<uint64_t, le_renderpass_o*, IdentityHash> passTable;
	passTable.reserve(passes.size());

	for ( auto &p : passes ) {
		passTable.emplace( p.id, &p );
	}

	graph_builder_traverse_passes(passTable, const_char_hash64("final"), 1);

	std::sort( passes.begin(), passes.end(), []( const le_renderpass_o &lhs, const le_renderpass_o &rhs ) -> bool {
		return lhs.graphInfo.execution_order > rhs.graphInfo.execution_order;
	} );

	std::ostringstream msg;

	msg << "render graph: " << std::endl;
	for ( const auto &pass : passes ) {
		msg << "renderpass: " << std::setw( 15 ) << std::hex << pass.id << ", order: " << pass.graphInfo.execution_order << std::endl;

		for ( const auto &attachment : pass.imageAttachments ) {
			if (attachment.access_flags & le::AccessFlagBits::eRead){
				msg << "r";
			}
			if (attachment.access_flags & le::AccessFlagBits::eWrite){
				msg << "w";
			}
			msg << " : " << std::setw( 32 ) << std::hex << attachment.id << ":" << attachment.source_id << ", " << attachment.debugName << std::endl;
		}
	}
	std::cout << msg.str() << std::endl;
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
	// NOTE: We can use the passes' sort order as a field in the
	// sorting key for any command buffers associated with that
	// renderpass.
	graph_builder_order_passes(self->passes);

	// NOTE: We should be able to prune any passes which have execution order 0
	// as they don't contribute to our final pass(es).

	// For each resource, add READ  marker before first read
	// For each resource, add WRITE marker after write

	// resources are stateful - when we build command buffers,
	// command buffers, and renderpasses are responsible to
	// minimise state transfers for resources.

	// when we add resources to a queue, the queue guarantees that
	// resource state transfers happen in a linear manner.

//	for ( auto &p : self->passes ) {
//		for ( auto &i : p.inputTextureAttachments ) {
//		}
//		for ( auto &o : p.outputAttachments ) {
//			// by default, output attachments should be placed in shader_read layout
//			// unless we're talking about the swapchain texture, which should be in
//			// memory_read state.
//		}
//	}

	//	std::unordered_map<uint64_t, std::vector<uint64_t>>	resourcesWrittenIn;

	//	for (auto &p : self->passes){
	//		for (auto o: p.outputAttachments){
	//			resourcesWrittenIn[o.id].emplace_back(o.source_id);
	//		}
	//	}

	//	std::unordered_map<uint64_t, std::vector<uint64_t>>	resourcesReadIn;

	//	for (auto &p : self->passes){
	//		for (auto i: p.inputAttachments){
	//			resourcesReadIn[i.id].emplace_back(p.id);
	//		}
	//	}

	//	std::ostringstream msg;

	//	msg << "Write table: " << std::endl;
	//	for ( const auto &resource : resourcesWrittenIn ) {
	//		msg << "resource: " << std::setw( 15 ) << std::hex << resource.first << std::endl;

	//		for ( const auto &resource_version: resource.second) {
	//			msg << "write: " << std::setw( 32 ) << std::hex << resource_version << std::endl;
	//		}

	//	}

	//	msg << "Read table: " << std::endl;
	//	for ( const auto &resource : resourcesReadIn ) {
	//		msg << "resource: " << std::setw( 15 ) << std::hex << resource.first << std::endl;

	//		for ( const auto &resource_version: resource.second) {
	//			msg << "read: " << std::setw( 32 ) << std::hex << resource_version << std::endl;
	//		}

	//	}

	//	std::cout << msg.str() << std::endl;

	//	// todo: create a list of unique resources so that resource manager knows what it must provide for this
	//	// frame - these must be allocated if not yet present.

	//	/*

	//	To be able to fully compare inputs we need a hashing function which takes into account id, source_id and extent_in_swapchain_units
	//	- but: if we attach callbacks to image attachments this will impact on their hash signature.
	//	- this means, when we resolve attachments, we must also copy over callbacks...

	//	*/

	//	std::unordered_map<size_t, image_attachment_t, IdentityHash> imageResources;

	//	for (auto & p: self->passes){
	//		for (auto & i:p.inputAttachments){
	//			imageResources.emplace(fnv_hash64(i,sizeof(i)),i);
	//		}
	//		for (auto & i:p.inputTextureAttachments){
	//			imageResources.emplace(fnv_hash64(i,sizeof(i)),i);
	//		}
	//		for (auto & o:p.outputAttachments){
	//			imageResources.emplace(fnv_hash64(o,sizeof(o)),o);
	//		}
	//	}

	//		msg << "Resource table: " << std::endl;
	//		for ( const auto &resource : imageResources ) {
	//			msg << "resource: " << std::setw( 15 ) << std::hex << resource.first << " = "
	//			    << resource.second.id << ":"
	//			    << resource.second.source_id
	//			    << ", extent_factor: " << std::dec << resource.second.extent_in_backbuffer_units
	//			    << ", format: " << vk::to_string( resource.second.format ) << std::endl;
	//		}

	//		std::cout << msg.str() << std::endl;

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

	le_graph_builder_i.create         = graph_builder_create;
	le_graph_builder_i.destroy        = graph_builder_destroy;
	le_graph_builder_i.add_renderpass = graph_builder_add_renderpass;
	le_graph_builder_i.build_graph    = graph_builder_build_graph;

}

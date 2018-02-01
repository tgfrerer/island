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


// ----------------------------------------------------------------------
// FNV hash using constexpr recursion over char string length (may execute at compile time)
inline uint64_t constexpr const_char_hash64( const char *input ) noexcept {
	return *input ? ( 0x100000001b3 * const_char_hash64( input + 1 ) ) ^ static_cast<uint64_t>( *input ) : 0xcbf29ce484222325;
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

using image_attachment_t = le_rendergraph_api::image_attachment_info_o;

struct le_renderpass_o {

	// For each output attachment, we must know
	// the LOAD OP- it can be any of these:
	// LOAD, CLEAR, DONT_CARE
	// if it were CLEAR, we must provide a clear
	// value.

	uint64_t                        id;
	std::vector<image_attachment_t> inputAttachments;
	std::vector<image_attachment_t> outputAttachments;
	std::vector<image_attachment_t> inputTextureAttachments;
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

static void renderpass_add_input_attachment(le_renderpass_o*self, const char* name_, le_rendergraph_api::image_attachment_info_o* info_){
	auto info = *info_;
	info.id = const_char_hash64(name_);
	self->inputAttachments.emplace_back(std::move(info));
}

// ----------------------------------------------------------------------

static void renderpass_add_output_attachment(le_renderpass_o*self, const char* name_, le_rendergraph_api::image_attachment_info_o* info_){
	// TODO: annotate the current renderpass to name of output attachment
	auto info = *info_;
	info.id = const_char_hash64(name_);
	info.source_id = self->id;
	self->outputAttachments.emplace_back(std::move(info));
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
static void graph_builder_resolve_attachment_ids(std::vector<le_renderpass_o>& passes){

	// Since rendermodule gives us a pre-sorted list of renderpasses,
	// we use this to resolve attachment aliases.
	// We can't render to the same attachment at the same time,
	// which is why every output attachment initially gets tagged
	// with its writer's id. Eventually, we'll find a way to re-use
	// attachments which may be aliased.

	std::unordered_map<uint64_t, image_attachment_t, IdentityHash> imageInputAttachments;
	std::unordered_map<uint64_t, image_attachment_t, IdentityHash> imageOutputAttachments;

	for ( auto &p : passes ) {

		for ( auto &i : p.inputAttachments ) {
			auto foundOutputIt = imageOutputAttachments.find( i.id );
			if ( foundOutputIt != imageOutputAttachments.end() ) {
				i.source_id = foundOutputIt->second.source_id;
			}
		}

		for ( auto &i : p.inputTextureAttachments ) {
			auto foundOutputIt = imageOutputAttachments.find( i.id );
			if ( foundOutputIt != imageOutputAttachments.end() ) {
				i.source_id = foundOutputIt->second.source_id;
			}
		}

		// for all subsequent passes, replace any aliased attachments.
		for ( auto &o : p.outputAttachments ) {
			imageOutputAttachments[ o.id ] = o;
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
	// we can look it up by id
	auto &sourcePass = passes.at( sourceRenderpassId );

	// We want the maximum edge distance (one recursion equals one edge) from the root node
	// for each pass, since the max distance makes sure that all resources are available,
	// even resources which have a shorter path.
	sourcePass->graphInfo.execution_order = std::max( recursion_depth, sourcePass->graphInfo.execution_order );

	for ( auto &inAttachment : sourcePass->inputAttachments ) {
		graph_builder_traverse_passes( passes, inAttachment.source_id, recursion_depth + 1 );
	}
}

// ----------------------------------------------------------------------

// re-order renderpasses in topological order, based
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

		for ( const auto &attachment : pass.inputAttachments ) {
			msg << "in : " << std::setw( 32 ) << std::hex << attachment.id << ":" << attachment.source_id << std::endl;
		}
		for ( const auto &attachment : pass.outputAttachments ) {
			msg << "out: " << std::setw( 32 ) << std::hex << attachment.id << ":" << attachment.source_id << std::endl;
		}
	}
	std::cout << msg.str() << std::endl;
}

// ----------------------------------------------------------------------

static void graph_builder_build_graph(le_graph_builder_o* self){

	// find corresponding output for each input attachment,
	// and tag input with output id
	graph_builder_resolve_attachment_ids(self->passes);

	// first, we must establish the sort order
	graph_builder_order_passes(self->passes);

	// we should be able to prune any passes which have execution order 0
	// as they don't contribute to our final pass(es).

}

// ----------------------------------------------------------------------

void register_le_rendergraph_api( void *api_ ) {

	auto  le_rendergraph_api_i = static_cast<le_rendergraph_api *>( api_ );
	auto &le_renderpass_i      = le_rendergraph_api_i->le_renderpass_i;
	auto &le_render_module_i   = le_rendergraph_api_i->le_render_module_i;
	auto &le_graph_builder_i   = le_rendergraph_api_i->le_graph_builder_i;

	le_renderpass_i.create                = renderpass_create;
	le_renderpass_i.destroy               = renderpass_destroy;
	le_renderpass_i.add_input_attachment  = renderpass_add_input_attachment;
	le_renderpass_i.add_output_attachment = renderpass_add_output_attachment;
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

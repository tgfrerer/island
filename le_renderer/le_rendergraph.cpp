
#include "le_renderer.h"
#include "le_renderer/private/le_rendergraph.h"

#include "le_renderer/private/le_renderpass.h"
#include "le_renderer/private/hash_util.h"

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

#include "le_renderer/private/le_renderer_types.h"
// these are some sanity checks for le_renderer_types

static_assert(sizeof(le::CommandHeader)==sizeof(uint64_t),"size must be 64bit");


#define LE_GRAPH_BUILDER_RECURSION_DEPTH 20

using image_attachment_t = le_renderer_api::image_attachment_info_o;

// ----------------------------------------------------------------------

struct le_render_module_o : NoCopy, NoMove {
	std::vector<le_renderpass_o> passes;
};

// ----------------------------------------------------------------------

struct le_graph_builder_o : NoCopy, NoMove {
	std::vector<le_renderpass_o> passes;


};

// ----------------------------------------------------------------------

static le_render_module_o *render_module_create( ) {
	auto obj = new le_render_module_o( );
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
		if (pass.callbackSetup(&pass)){
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

static void graph_builder_get_passes( le_graph_builder_o *self, le_renderpass_o **pPasses, size_t *pNumPasses ) {

	*pPasses = self->passes.data();
	*pNumPasses = self->passes.size();

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
}

// ----------------------------------------------------------------------

static void graph_builder_execute_graph(le_graph_builder_o* self){

	std::ostringstream msg;

	msg << "render graph: " << std::endl;
	for ( const auto &pass : self->passes ) {
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
	std::cout << msg.str();

	le::CommandBufferEncoder cb;

	for (auto & pass: self->passes){
		if (pass.callbackRender != nullptr){
			pass.callbackRender(cb, pass.render_callback_user_data);
		}
	}

}

// ----------------------------------------------------------------------

void register_le_rendergraph_api( void *api_ ) {

	auto  le_renderer_api_i = static_cast<le_renderer_api *>( api_ );
	auto &le_render_module_i   = le_renderer_api_i->le_render_module_i;
	auto &le_graph_builder_i   = le_renderer_api_i->le_graph_builder_i;

	le_render_module_i.create         = render_module_create;
	le_render_module_i.destroy        = render_module_destroy;
	le_render_module_i.add_renderpass = render_module_add_renderpass;
	le_render_module_i.build_graph    = render_module_build_graph;

	le_graph_builder_i.create         = graph_builder_create;
	le_graph_builder_i.destroy        = graph_builder_destroy;
	le_graph_builder_i.reset          = graph_builder_reset;
	le_graph_builder_i.add_renderpass = graph_builder_add_renderpass;
	le_graph_builder_i.build_graph    = graph_builder_build_graph;
	le_graph_builder_i.execute_graph  = graph_builder_execute_graph;
	le_graph_builder_i.get_passes     = graph_builder_get_passes;
}

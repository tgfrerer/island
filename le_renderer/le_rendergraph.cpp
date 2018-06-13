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

#include "le_renderer/private/le_renderer_types.h"

#ifndef PRINT_DEBUG_MESSAGES
#define PRINT_DEBUG_MESSAGES false
#endif

#define LE_GRAPH_BUILDER_RECURSION_DEPTH 20

// these are some sanity checks for le_renderer_types
static_assert( sizeof( le::CommandHeader ) == sizeof( uint64_t ), "Size of le::CommandHeader must be 64bit" );

using image_attachment_t = le_renderer_api::image_attachment_info_o;

// ----------------------------------------------------------------------

struct le_render_module_o : NoCopy, NoMove {
	std::vector<le_renderpass_o> passes;
};

// ----------------------------------------------------------------------

struct le_graph_builder_o : NoCopy, NoMove {
	std::vector<le_renderpass_o>               passes;
	std::vector<le_command_buffer_encoder_o *> encoders;
};

// ----------------------------------------------------------------------

static le_graph_builder_o *graph_builder_create() {
	auto obj = new le_graph_builder_o();
	return obj;
}

// ----------------------------------------------------------------------

static void graph_builder_reset( le_graph_builder_o *self ) {
	self->passes.clear();
}

// ----------------------------------------------------------------------

static void graph_builder_destroy( le_graph_builder_o *self ) {
	graph_builder_reset( self );
	delete self;
}

// ----------------------------------------------------------------------

static void graph_builder_add_renderpass( le_graph_builder_o *self, le_renderpass_o *renderpass ) {
	self->passes.emplace_back( *renderpass );
}

// ----------------------------------------------------------------------
/// \brief find corresponding output for each input resource
static std::vector<std::vector<uint64_t>> graph_builder_resolve_resource_ids( const std::vector<le_renderpass_o> &passes ) {

	std::vector<std::vector<uint64_t>> dependenciesPerPass;

	// Rendermodule gives us a pre-sorted list of renderpasses,
	// we use this to resolve attachment aliases. Since Rendermodule is a linear sequence,
	// this means that dependencies for resources are well-defined. It's impossible for
	// two renderpasses using the same resource not to have a clearly defined priority, as
	// the earliest submitted renderpasses of the two will get priority.

	// returns: for each pass, a list of passes which write to resources that this pass uses.

	dependenciesPerPass.reserve( passes.size() );

	// map from resource id -> source pass id
	std::unordered_map<uint64_t, uint64_t, IdentityHash> writeAttachmentTable;

	// We go through passes in module submission order, so that outputs will match later inputs.
	uint64_t passIndex = 0;
	for ( auto &pass : passes ) {

		std::vector<uint64_t> passesThisPassDependsOn;
		passesThisPassDependsOn.reserve( pass.readResourceCount );

		// We must first look if any of our READ attachments are already present in the attachment table.
		// If so, we update source ids (from table) for each attachment we found.
		for ( size_t i = 0; i != pass.readResourceCount; ++i ) {
			const auto &resource = pass.readResources[ i ];

			auto foundOutputIt = writeAttachmentTable.find( resource );
			if ( foundOutputIt != writeAttachmentTable.end() ) {
				passesThisPassDependsOn.emplace_back( foundOutputIt->second );
			}
		}

		dependenciesPerPass.emplace_back( passesThisPassDependsOn );

		// Outputs from current pass overwrite any cached outputs with same name:
		// later inputs with same name will then resolve to the latest version
		// of an output with a particular name.
		for ( size_t i = 0; i != pass.writeResourceCount; ++i ) {
			const auto &writeResourceId             = pass.writeResources[ i ];
			writeAttachmentTable[ writeResourceId ] = passIndex;
		}

		++passIndex;
	}

	return dependenciesPerPass;
}

// ----------------------------------------------------------------------
/// \brief depth-first traversal of graph, following each input to its corresponding output
static void graph_builder_traverse_passes( const std::vector<std::vector<uint64_t>> &passes,
                                           const uint64_t &                          currentRenderpassId,
                                           const uint32_t                            recursion_depth,
                                           std::vector<uint32_t> &                   pass_sort_orders ) {

	if ( recursion_depth > LE_GRAPH_BUILDER_RECURSION_DEPTH ) {
		std::cerr << __FUNCTION__ << " : "
		          << "max recursion level reached. check for cycles in render graph" << std::endl;
		return;
	}

	// TODO: how do we deal with external resources?

	// as each input tells us its source renderpass,
	// we can look up the provider pass for each source by id
	auto &sourcePasses = passes.at( currentRenderpassId );

	// We want the maximum edge distance (one recursion equals one edge) from the root node
	// for each pass, since the max distance makes sure that all resources are available,
	// even resources which have a shorter path.
	pass_sort_orders[ currentRenderpassId ] = std::max( recursion_depth, pass_sort_orders[ currentRenderpassId ] );

	for ( auto &sourcePass : sourcePasses ) {
		graph_builder_traverse_passes( passes, sourcePass, recursion_depth + 1, pass_sort_orders );
	}
}

// ----------------------------------------------------------------------

static std::vector<uint64_t> graph_builder_find_root_passes( const std::vector<le_renderpass_o> &passes ) {
	std::vector<uint64_t> roots;
	roots.reserve( passes.size() );

	uint64_t i = 0;
	for ( auto &pass : passes ) {
		if ( pass.isRoot ) {
			roots.push_back( i );
		}
		++i;
	}

	return roots;
}

// ----------------------------------------------------------------------

static void graph_builder_build_graph( le_graph_builder_o *self ) {

	// Find corresponding output for each input attachment,
	// and tag input with output id, as dependencies are
	// declared using names rather than linked in code.
	auto pass_dependencies = graph_builder_resolve_resource_ids( self->passes );

	{
		// Establish a toplogical sorting order
		// so that passes which produce resources for other
		// passes are executed *before* their dependencies
		//
		auto root_passes = graph_builder_find_root_passes( self->passes );

		std::vector<uint32_t> pass_sort_orders; // sort order for each pass in self->passes
		pass_sort_orders.resize( self->passes.size(), 0 );

		for ( auto root : root_passes ) {
			// note that we begin with sort order 1, so that any passes which have
			// sort order 0 still after this loop is complete can be seen as
			// marked for deletion / or can be ignored.
			graph_builder_traverse_passes( pass_dependencies, root, 1, pass_sort_orders );
		}

		// We use the passes' sort order as a field in the
		// sorting key for any command buffers associated with that
		// renderpass.

		// store sort key with every pass
		for ( size_t i = 0; i != self->passes.size(); ++i ) {
			self->passes[ i ].sort_key = pass_sort_orders[ i ];
		}
	}

	// Use sort key to order passes in decending order, based on sort key.
	// pass with lower sort key depends on pass with higher sort key.
	std::stable_sort( self->passes.begin(), self->passes.end(), []( const le_renderpass_o &lhs, const le_renderpass_o &rhs ) {
		return lhs.sort_key > rhs.sort_key;
	} );
}

// ----------------------------------------------------------------------

static void graph_builder_execute_graph( le_graph_builder_o *self, size_t frameIndex, le_backend_o *backend ) {

	/// Record render commands by calling rendercallbacks for each renderpass.
	///
	/// Render Commands are stored as a command stream. This command stream uses a binary,
	/// API-agnostic representation, and contains an ordered list of commands, and optionally,
	/// inlined parameters for each command.
	///
	/// The command stream is stored inside of the Encoder that is used to record it (that's not elegant).
	///
	/// We could possibly go wide when recording renderpasses, with one context per renderpass.

	if ( PRINT_DEBUG_MESSAGES ) {
		std::ostringstream msg;
		msg << "render graph: " << std::endl;
		for ( const auto &pass : self->passes ) {
			msg << "renderpass: " << std::setw( 15 ) << std::hex << pass.id << ", "
			    << "'" << pass.debugName << "' , sort_key: " << pass.sort_key << std::endl;

			for ( const auto &attachment : pass.imageAttachments ) {
				if ( attachment.access_flags & le::AccessFlagBits::eRead ) {
					msg << "r";
				}
				if ( attachment.access_flags & le::AccessFlagBits::eWrite ) {
					msg << "w";
				}
				msg << " : " << std::setw( 32 ) << std::hex << attachment.resource_id << ":" << attachment.source_id << ", '" << attachment.debugName << "'" << std::endl;
			}
		}
		std::cout << msg.str();
	}

	self->encoders.reserve( self->passes.size() );

	static const auto &encoderInterface = Registry::getApi<le_renderer_api>()->le_command_buffer_encoder_i;
	static const auto &backendInterface = Registry::getApi<le_backend_vk_api>()->vk_backend_i;

	// TODO: have one allocator per pass, so that allocator and encoder may be interleaved,
	// and run on parallel threads.
	auto allocator = backendInterface.get_transient_allocator( backend, frameIndex );

	for ( auto &pass : self->passes ) {

		if ( pass.sort_key != 0 ) {
		}

		if ( pass.callbackExecute != nullptr && pass.sort_key != 0 ) {

			auto encoder = encoderInterface.create( allocator ); // NOTE: we must manually track the lifetime of encoder!

			pass.callbackExecute( encoder, pass.execute_callback_user_data );
			pass.encoder = ( encoder );
		}
	}
}

// ----------------------------------------------------------------------

static void graph_builder_get_passes( le_graph_builder_o *self, le_renderpass_o **pPasses, size_t *pNumPasses ) {

	*pPasses    = self->passes.data();
	*pNumPasses = self->passes.size();
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

static void render_module_add_renderpass( le_render_module_o *self, le_renderpass_o *pass ) {
	// TODO: make sure name for each pass is unique per rendermodule.
	self->passes.emplace_back( *pass );
}

// ----------------------------------------------------------------------

static void render_module_setup_passes( le_render_module_o *self, le_graph_builder_o *graph_builder_ ) {

	for ( auto &pass : self->passes ) {
		// Call setup function on all passes, in order of addition to module
		//
		// Setup Function must:
		// + populate input attachments
		// + populate output attachments
		// + (optionally) add renderpass to graph builder.
		assert( pass.callbackSetup != nullptr );
		if ( pass.callbackSetup( &pass ) ) {
			// if pass.setup() returns true, this means we shall add this pass to the graph
			graph_builder_add_renderpass( graph_builder_, &pass );
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
};

// ----------------------------------------------------------------------

void register_le_rendergraph_api( void *api_ ) {

	auto le_renderer_api_i = static_cast<le_renderer_api *>( api_ );

	auto &le_render_module_i          = le_renderer_api_i->le_render_module_i;
	le_render_module_i.create         = render_module_create;
	le_render_module_i.destroy        = render_module_destroy;
	le_render_module_i.add_renderpass = render_module_add_renderpass;
	le_render_module_i.setup_passes   = render_module_setup_passes;

	auto &le_graph_builder_i          = le_renderer_api_i->le_graph_builder_i;
	le_graph_builder_i.create         = graph_builder_create;
	le_graph_builder_i.destroy        = graph_builder_destroy;
	le_graph_builder_i.reset          = graph_builder_reset;
	le_graph_builder_i.add_renderpass = graph_builder_add_renderpass;
	le_graph_builder_i.build_graph    = graph_builder_build_graph;
	le_graph_builder_i.execute_graph  = graph_builder_execute_graph;
	le_graph_builder_i.get_passes     = graph_builder_get_passes;
}

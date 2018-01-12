#include "le_rendergraph.h"

#include <vector>
#include <string>
#include <assert.h>
#include <algorithm>
#include <unordered_set>
#include <iostream>
#include <iomanip>

// todo: fill in missing methods,
// wire up methods,
// think whether you want to use copies or pointers inside the constructed container structs.

struct le_renderpass_o {
	std::string                                name;
	std::unordered_set<std::string>                      inputAttachments;
	std::unordered_set<std::string>                      outputAttachments;
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
	std::vector<le_renderpass_o> passes;
};

// ----------------------------------------------------------------------

static le_renderpass_o *renderpass_create(const char* renderpass_name) {
	auto self = new le_renderpass_o();
	self->name = std::string(renderpass_name);
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

static void renderpass_add_input_attachment(le_renderpass_o*self, const char* name_){
	self->inputAttachments.emplace(name_);
}

// ----------------------------------------------------------------------

static void renderpass_add_output_attachment(le_renderpass_o*self, const char* name_){
	self->outputAttachments.emplace(name_);
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

static void render_module_build_graph(le_render_module_o* self, le_graph_builder_o* graph_builder){
	for (auto &pass: self->passes){
		// Call setup function on all passes, in order of addition to module
		//
		// Setup Function must:
		// + populate input attachments
		// + populate output attachments
		// + (optionally) add renderpass to graph builder.
		assert(pass->callbackSetup != nullptr);
		pass->callbackSetup(pass, graph_builder);
	}
	// Now, renderpasses should have their attachment properly set.
	// Further, user will have added all renderpasses they wanted included in the module
	// to the graph builder.

	// The graph builder now has a list of all passes which contribute to the current module.

	// Step 1: Validate
	// - find any name clashes: inputs and outputs for each renderpass must be unique.

	le::GraphBuilder gb{graph_builder};

	gb.buildGraph();
	// Step 2: sort passes in dependency order (by adding an execution order index to each pass)
	// Step 3: add  markers to each attachment for each pass, depending on their read/write status

};

// ----------------------------------------------------------------------

static le_graph_builder_o* graph_builder_create(){
	auto obj = new le_graph_builder_o();
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

static bool traverse_tree( uint64_t                                       execution_order,
                           const std::vector<le_renderpass_o>::iterator& begin_range,
                           const std::vector<le_renderpass_o>::iterator& end_range,
                           std::string                                    outputSignature ) {

	auto found_element = std::find_if(begin_range, end_range, [&outputSignature](const le_renderpass_o &pass)->bool{
		return (pass.outputAttachments.count(outputSignature) == 1);
	});


	if (found_element != end_range){
		found_element->graphInfo.execution_order = std::max(execution_order, found_element->graphInfo.execution_order);
		auto & inputAttachments = found_element->inputAttachments;
		bool result = true;
		for (auto & i : inputAttachments){
			result &= traverse_tree(execution_order + 1, found_element+1, end_range, i);
		}
		return result;
	} else {
		std::cerr << "could not find prior matching output attachment: '" << outputSignature << "' for pass: " << (begin_range-1)->name << std::endl;
		return false;
	}
}

// ----------------------------------------------------------------------


static void graph_builder_sort_passes(std::vector<le_renderpass_o>& passes){

	std::string outputSignature = "backbuffer";

	// we can only look for earlier passes for dependencies

	// find last pass writing to backbuffer

	std::reverse(passes.begin(),passes.end());

	bool tree_valid = traverse_tree(0, passes.begin(), passes.end(), "backbuffer");

	if (!tree_valid){
		std::cerr << "tree not valid";
	}

	// with this pass, list all inputs
	// for each input, find last pass writing to this input


}

// ----------------------------------------------------------------------

static void graph_builder_build_graph(le_graph_builder_o* self){

	// Validate:
	// + make sure that no two passes write_only to an attachment

	// first, we must establish the sort order
	graph_builder_sort_passes(self->passes);

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

#ifndef GUARD_LE_RENDERGRAPH_H
#define GUARD_LE_RENDERGRAPH_H


#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

// depends on le_backend_vk. le_backend_vk must be loaded before this class is used.

#ifdef __cplusplus
extern "C" {
#endif

void register_le_rendergraph_api( void *api );

struct le_renderpass_o;
struct le_graph_builder_o;
struct le_render_module_o;
struct le_backend_vk_device_o;

namespace vk {
    enum class Format; // forward declaration
	enum class Layout;
	enum class AttachmentStoreOp;
	enum class AttachmentLoadOp;
}

struct le_rendergraph_api {
	static constexpr auto id       = "le_rendergraph";
	static constexpr auto pRegFun  = register_le_rendergraph_api;

	enum AccessFlagBits : uint8_t {
		eRead      = 0x01,
		eWrite     = 0x02,
		eReadWrite = eRead | eWrite,
	};

	struct image_attachment_info_o {

		uint64_t              id           = 0; // hash name given to this attachment, based on name string
		uint64_t              source_id    = 0; // hash name of writer/creator
		uint8_t               access_flags = 0;
		vk::Format            format;
		vk::Layout            finalLayout;
		vk::AttachmentLoadOp  loadOp;
		vk::AttachmentStoreOp storeOp;
		void ( *onClear )( void *clear_data ) = nullptr;
		char debugName[ 32 ];
	};

	typedef bool(*pfn_renderpass_setup_t)(le_renderpass_o* obj, le_backend_vk_device_o *);

	struct renderpass_interface_t {
		le_renderpass_o* ( *create                ) (const char* renderpass_name);
		void             ( *destroy               ) (le_renderpass_o* obj);
		void             ( *set_setup_fun         ) (le_renderpass_o* obj, pfn_renderpass_setup_t setup_fun );

		void             ( *add_image_attachment  ) (le_renderpass_o* obj, const char*, image_attachment_info_o* info);
	};

	struct rendermodule_interface_t {
		le_render_module_o* ( *create)         ( );
		void                ( *destroy)        ( le_render_module_o* obj );
		void                ( *add_renderpass) ( le_render_module_o* obj, le_renderpass_o* rp );
		void                ( *build_graph)    ( le_render_module_o* obj, le_graph_builder_o* gb );
		void                ( *execute_graph ) ( le_render_module_o* obj, le_graph_builder_o* gb );
	};

	// graph builder builds a graph for a module
	struct graph_builder_interface_t {
		le_graph_builder_o* ( *create        ) ( le_backend_vk_device_o* device);
		void                ( *destroy       ) ( le_graph_builder_o* obj );
		void                ( *add_renderpass) ( le_graph_builder_o* obj, le_renderpass_o* rp );
		void                ( *build_graph)    ( le_graph_builder_o* obj );
	};

	renderpass_interface_t    le_renderpass_i;
	rendermodule_interface_t  le_render_module_i;
	graph_builder_interface_t le_graph_builder_i;
};

#ifdef __cplusplus
} // extern "C"
#endif

namespace le {

using ImageAttachmentInfo = le_rendergraph_api::image_attachment_info_o;
using AccessFlagBits      = le_rendergraph_api::AccessFlagBits;

class RenderPass {
	const le_rendergraph_api &                        rendergraphApiI = *Registry::getApi<le_rendergraph_api>();
	const le_rendergraph_api::renderpass_interface_t &renderpassI     = rendergraphApiI.le_renderpass_i;

	le_renderpass_o *self;

  public:
	RenderPass( const char *name_ )
	    : self( renderpassI.create( name_ ) ) {
	}

	~RenderPass(){
		renderpassI.destroy(self);
	}

	operator auto() {
		return self;
	}

	void setSetupCallback( le_rendergraph_api::pfn_renderpass_setup_t fun ) {
		renderpassI.set_setup_fun( self, fun );
	}
};

// ----------------------------------------------------------------------

class RenderPassRef {
	// non-owning version of RenderPass, but with more public methods
	const le_rendergraph_api &                        rendergraphApiI = *Registry::getApi<le_rendergraph_api>();
	const le_rendergraph_api::renderpass_interface_t &renderpassI     = rendergraphApiI.le_renderpass_i;

	le_renderpass_o *self = nullptr;

  public:
	RenderPassRef()  = delete;
	~RenderPassRef() = default;

	RenderPassRef( le_renderpass_o *self_ )
	    : self( self_ ) {
	}

	operator auto() {
		return self;
	}

	RenderPassRef &addImageAttachment( const char *name_,  le_rendergraph_api::image_attachment_info_o* info ) {
		renderpassI.add_image_attachment( self, name_, info );
		return *this;
	}

};

// ----------------------------------------------------------------------

class GraphBuilder {
	const le_rendergraph_api &                           rendergraphApiI = *Registry::getApi<le_rendergraph_api>();
	const le_rendergraph_api::graph_builder_interface_t &graphbuilderI   = rendergraphApiI.le_graph_builder_i;

	le_graph_builder_o *self;
	bool                is_reference = false;

  public:
	GraphBuilder(le_backend_vk_device_o* device)
	    : self( graphbuilderI.create(device) ) {
	}

	GraphBuilder( le_graph_builder_o *self_ )
	    : self( self_ )
	    , is_reference( true ) {
	}

	~GraphBuilder() {
		if ( !is_reference ) {
			graphbuilderI.destroy( self );
		}
	}

	operator auto() {
		return self;
	}

	void addRenderpass( le_renderpass_o *rp ) {
		graphbuilderI.add_renderpass( self, rp );
	}

	void buildGraph(){
		graphbuilderI.build_graph(self);
	}
};

// ----------------------------------------------------------------------

class RenderModule {
	const le_rendergraph_api &                          rendergraphApiI = *Registry::getApi<le_rendergraph_api>();
	const le_rendergraph_api::rendermodule_interface_t &rendermoduleI   = rendergraphApiI.le_render_module_i;

	le_render_module_o *self;
	bool                is_reference = false;

  public:
	RenderModule()
	    : self( rendermoduleI.create() ) {
	}

	RenderModule( le_render_module_o *self_ )
	    : self( self_ )
	    , is_reference( true ) {
	}

	~RenderModule() {
		if ( !is_reference ) {
			rendermoduleI.destroy( self );
		}
	}

	operator auto() {
		return self;
	}

	void addRenderPass(le_renderpass_o* renderpass){
		rendermoduleI.add_renderpass(self,renderpass);
	}

	void buildGraph( le_graph_builder_o *gb_ ) {
		rendermoduleI.build_graph( self, gb_ );
	}

	void executeGraph( le_graph_builder_o *gb_ ) {
		rendermoduleI.execute_graph( self, gb_ );
	}
};

} // namespace le

#endif // GUARD_LE_RENDERGRAPH_H

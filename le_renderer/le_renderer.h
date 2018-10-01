#ifndef GUARD_LE_RENDERER_H
#define GUARD_LE_RENDERER_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#include "private/le_renderer_types.h"

constexpr uint64_t LE_RENDERPASS_MARKER_EXTERNAL = hash_64_fnv1a_const( "rp-external" );

#ifdef __cplusplus
extern "C" {
#endif

void register_le_renderer_api( void *api );
void register_le_rendergraph_api( void *api );            // in le_rendergraph.cpp
void register_le_command_buffer_encoder_api( void *api ); // in le_command_buffer_encoder.cpp

struct le_renderer_o;
struct le_render_module_o;
struct le_renderpass_o;
struct le_graph_builder_o;
struct le_command_buffer_encoder_o;
struct le_backend_o;
struct le_allocator_o;
struct le_shader_module_o; ///< shader module, 1:1 relationship with a shader source file

// clang-format off
struct le_renderer_api {

	static constexpr auto id      = "le_renderer";
	static constexpr auto pRegFun = register_le_renderer_api;


	struct renderer_interface_t {
		le_renderer_o *                ( *create                                )( le_backend_o  *backend );
		void                           ( *destroy                               )( le_renderer_o *obj );
		void                           ( *setup                                 )( le_renderer_o *obj );
		void                           ( *update                                )( le_renderer_o *obj, le_render_module_o *module );
		le_shader_module_o*            ( *create_shader_module                  )( le_renderer_o *self, char const *path, LeShaderType mtype );

		/// introduces a new resource name to the renderer, returns a handle under which the renderer will recognise this resource
		LeResourceHandle               ( *declare_resource                      )( le_renderer_o* self, LeResourceType type );

		/// returns the resource handle for the current swapchain image
		LeResourceHandle               ( *get_backbuffer_resource               )( le_renderer_o* self );
		le_backend_o*                  ( *get_backend)(le_renderer_o* self);
	};


	typedef bool ( *pfn_renderpass_setup_t )( le_renderpass_o *obj, void* user_data );
	typedef void ( *pfn_renderpass_execute_t )( le_command_buffer_encoder_o *encoder, void *user_data );

	struct renderpass_interface_t {
		le_renderpass_o *            ( *create               )( const char *renderpass_name, const LeRenderPassType &type_ );
		void                         ( *destroy              )( le_renderpass_o *obj );
		le_renderpass_o *            ( *clone                )( const le_renderpass_o *obj );
		void                         ( *set_setup_callback   )( le_renderpass_o *obj, pfn_renderpass_setup_t setup_fun, void *user_data );
		bool                         ( *has_setup_callback   )( const le_renderpass_o* obj);
		bool                         ( *run_setup_callback   )( le_renderpass_o* obj);
		void                         ( *add_image_attachment )( le_renderpass_o *obj, LeResourceHandle resource_id, LeImageAttachmentInfo const *info );
		uint32_t                     ( *get_width            )( le_renderpass_o* obj);
		uint32_t                     ( *get_height           )( le_renderpass_o* obj);
		void                         ( *set_width            )( le_renderpass_o* obj, uint32_t width);
		void                         ( *set_height           )( le_renderpass_o* obj, uint32_t height);
		void                         ( *set_execute_callback )( le_renderpass_o *obj, pfn_renderpass_execute_t render_fun, void *user_data );
		void                         ( *run_execute_callback )( le_renderpass_o* obj, le_command_buffer_encoder_o* encoder);
		bool                         ( *has_execute_callback )( const le_renderpass_o* obj);
		void                         ( *use_resource         )( le_renderpass_o *obj, LeResourceHandle resource_id, uint32_t access_flags );
		void                         ( *create_resource      )( le_renderpass_o *obj, LeResourceHandle resource_id, const le_resource_info_t &info );
		void                         ( *set_is_root          )( le_renderpass_o *obj, bool is_root );
		bool                         ( *get_is_root          )( const le_renderpass_o *obj);
		void                         ( *set_sort_key         )( le_renderpass_o *obj, uint64_t sort_key);
		uint64_t                     ( *get_sort_key         )( const le_renderpass_o *obj);
		void                         ( *get_read_resources   )( const le_renderpass_o * obj, LeResourceHandle const ** pReadResources, size_t* count );
		void                         ( *get_write_resources  )( const le_renderpass_o * obj, LeResourceHandle const ** pWriteResources, size_t* count );
		void                         ( *get_create_resources )( const le_renderpass_o *obj, LeResourceHandle const **pCreateResources, le_resource_info_t const **pResourceInfos, size_t *count );
		const char*                  ( *get_debug_name       )( const le_renderpass_o* obj );
		uint64_t                     ( *get_id               )( const le_renderpass_o* obj );
		LeRenderPassType             ( *get_type             )( const le_renderpass_o* obj );
		le_command_buffer_encoder_o* ( *steal_encoder        )( le_renderpass_o* obj );
		void                         ( *get_image_attachments)(const le_renderpass_o* obj, const LeImageAttachmentInfo** pAttachments, size_t* numAttachments);

		// TODO: not too sure about the nomenclature of this
		// Note that this method implicitly marks the image resource referenced in LeTextureInfo for read access.
		void                         ( *sample_texture        )(le_renderpass_o* obj, LeResourceHandle texture_name, const LeTextureInfo* info);

		void                         ( *get_texture_ids       )(le_renderpass_o* obj, const LeResourceHandle ** pIds, uint64_t* count);
		void                         ( *get_texture_infos     )(le_renderpass_o* obj, const LeTextureInfo** pInfos, uint64_t* count);
	};

	struct rendermodule_interface_t {
		le_render_module_o * ( *create         )();
		void                 ( *destroy        )( le_render_module_o *obj );
		void                 ( *add_renderpass )( le_render_module_o *obj, le_renderpass_o *rp );
		void                 ( *setup_passes   )( le_render_module_o *obj, le_graph_builder_o *gb );
	};

	// graph builder builds a graph for a module
	struct graph_builder_interface_t {
		le_graph_builder_o * ( *create         )();
		void                 ( *destroy        )( le_graph_builder_o *obj );
		void                 ( *reset          )( le_graph_builder_o *obj );

		void                 ( *build_graph    )( le_graph_builder_o *obj );
		void                 ( *execute_graph  )( le_graph_builder_o *obj, size_t frameIndex, le_backend_o *backend );
		void                 ( *get_passes     )( le_graph_builder_o *obj, le_renderpass_o ***pPasses, size_t *pNumPasses );
	};

	struct command_buffer_encoder_interface_t {
		le_command_buffer_encoder_o *( *create                 )( le_allocator_o *allocator );
		void                         ( *destroy                )( le_command_buffer_encoder_o *obj );

		void                         ( *draw                   )( le_command_buffer_encoder_o *self, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance );
		void                         ( *draw_indexed           )( le_command_buffer_encoder_o *self, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);
		void                         ( *set_line_width         )( le_command_buffer_encoder_o *self, float line_width_ );
		void                         ( *set_viewport           )( le_command_buffer_encoder_o *self, uint32_t firstViewport, const uint32_t viewportCount, const le::Viewport *pViewports );
		void                         ( *set_scissor            )( le_command_buffer_encoder_o *self, uint32_t firstScissor, const uint32_t scissorCount, const le::Rect2D *pViewports );
		void                         ( *bind_graphics_pipeline )( le_command_buffer_encoder_o *self, uint64_t gpsoHash);

		void                         ( *bind_index_buffer      )( le_command_buffer_encoder_o *self, LeResourceHandle const bufferId, uint64_t offset, uint64_t const indexType);
		void                         ( *bind_vertex_buffers    )( le_command_buffer_encoder_o *self, uint32_t firstBinding, uint32_t bindingCount, LeResourceHandle const * pBufferId, uint64_t const * pOffsets );

		void                         ( *set_index_data         )( le_command_buffer_encoder_o *self, void const *data, uint64_t numBytes, uint64_t indexType );
		void                         ( *set_vertex_data        )( le_command_buffer_encoder_o *self, void const *data, uint64_t numBytes, uint32_t bindingIndex );

		void                         ( *write_to_buffer        )( le_command_buffer_encoder_o *self, LeResourceHandle const resourceId, size_t offset, void const* data, size_t numBytes);
		void                         ( *write_to_image         )( le_command_buffer_encoder_o *self, LeResourceHandle const resourceId, struct LeBufferWriteRegion const &region, void const *data, size_t numBytes );

		// stores ubo argument data to scratch buffer - note that parameter index must be dynamic offset index
		void                         ( *set_argument_ubo_data  ) (le_command_buffer_encoder_o *self, uint64_t argumentNameId, void const * data, size_t numBytes);
		void                         ( *set_argument_texture   ) (le_command_buffer_encoder_o* self, LeResourceHandle const textureId, uint64_t argumentName, uint64_t arrayIndex);

		void                         ( *get_encoded_data       )( le_command_buffer_encoder_o *self, void **data, size_t *numBytes, size_t *numCommands );
	};


	renderer_interface_t               le_renderer_i;
	renderpass_interface_t             le_renderpass_i;
	rendermodule_interface_t           le_render_module_i;
	graph_builder_interface_t          le_graph_builder_i;
	command_buffer_encoder_interface_t le_command_buffer_encoder_i;
};
// clang-format on

#ifdef __cplusplus
} // extern "C"

namespace le_renderer {
#	ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<le_renderer_api>( true );
#	else
const auto api = Registry::addApiStatic<le_renderer_api>();
#	endif

static const auto &renderer_i      = api -> le_renderer_i;
static const auto &renderpass_i    = api -> le_renderpass_i;
static const auto &render_module_i = api -> le_render_module_i;
static const auto &graph_builder_i = api -> le_graph_builder_i;
static const auto &encoder_i       = api -> le_command_buffer_encoder_i;

} // namespace le_renderer

namespace le {

class ResourceHandle {

  public:
	constexpr ResourceHandle()
	    : m_resource( nullptr ) {
	}

	constexpr ResourceHandle( decltype( nullptr ) )
	    : m_resource( nullptr ) {
	}

	ResourceHandle( LeResourceHandle buffer )
	    : m_resource( buffer ) {
	}

	ResourceHandle &operator=( decltype( nullptr ) ) {
		m_resource = nullptr;
		return *this;
	}

	bool operator==( ResourceHandle const &rhs ) const {
		return m_resource == rhs.m_resource;
	}

	bool operator!=( ResourceHandle const &rhs ) const {
		return m_resource != rhs.m_resource;
	}

	bool operator<( ResourceHandle const &rhs ) const {
		return m_resource < rhs.m_resource;
	}

	operator LeResourceHandle const &() const {
		return m_resource;
	}

	explicit operator bool() const {
		return m_resource != nullptr;
	}

	bool operator!() const {
		return m_resource == nullptr;
	}

  private:
	LeResourceHandle m_resource;
};
static_assert( sizeof( ResourceHandle ) == sizeof( LeResourceHandle ), "handle and wrapper have different size!" );

class Renderer {

	le_renderer_o *self;

  public:
	Renderer( le_backend_o *backend )
	    : self( le_renderer::renderer_i.create( backend ) ) {
	}

	~Renderer() {
		le_renderer::renderer_i.destroy( self );
	}

	void setup() {
		le_renderer::renderer_i.setup( self );
	}

	void update( le_render_module_o *module ) {
		le_renderer::renderer_i.update( self, module );
	}

	le_shader_module_o *createShaderModule( char const *path, LeShaderType moduleType ) {
		return le_renderer::renderer_i.create_shader_module( self, path, moduleType );
	}

	ResourceHandle declareResource( LeResourceType type ) {
		return le_renderer::renderer_i.declare_resource( self, type );
	}

	ResourceHandle getBackbufferResource() {
		return le_renderer::renderer_i.get_backbuffer_resource( self );
	}

	operator auto() {
		return self;
	}
};

class RenderPass {

	le_renderpass_o *self;

  public:
	RenderPass( const char *name_, const LeRenderPassType &type_ )
	    : self( le_renderer::renderpass_i.create( name_, type_ ) ) {
	}

	~RenderPass() {
		le_renderer::renderpass_i.destroy( self );
	}

	operator auto() {
		return self;
	}

	void setSetupCallback( void *user_data, le_renderer_api::pfn_renderpass_setup_t fun ) {
		le_renderer::renderpass_i.set_setup_callback( self, fun, user_data );
	}

	void setExecuteCallback( void *user_data, le_renderer_api::pfn_renderpass_execute_t fun ) {
		le_renderer::renderpass_i.set_execute_callback( self, fun, user_data );
	}
};

// ----------------------------------------------------------------------

class RenderPassRef {
	// non-owning version of RenderPass, but with more public methods

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

	/// \brief adds a resource as an image attachment to the renderpass, resource is used for ColorAttachment and Write access, unless otherwise specified
	/// \details use an LeImageAttachmentInfo struct to specialise parameters, such as LOAD_OP, CLEAR_OP, and Clear/Load Color.
	RenderPassRef &addImageAttachment( const LeResourceHandle &resource_id, const LeImageAttachmentInfo &info = LeImageAttachmentInfo() ) {
		le_renderer::renderpass_i.add_image_attachment( self, resource_id, &info );
		return *this;
	}

	RenderPassRef &addDepthImageAttachment( const LeResourceHandle &resource_id, const LeImageAttachmentInfo &info = {
	                                                                                 eLeAccessFlagBitWrite,
	                                                                                 LE_ATTACHMENT_LOAD_OP_CLEAR,
	                                                                                 LE_ATTACHMENT_STORE_OP_STORE,
	                                                                                 LeImageAttachmentInfo::DefaultClearValueDepthStencil,
	                                                                                 0,
	                                                                                 nullptr,
	                                                                                 0,
	                                                                                 {},
                                                                                 } ) {
		return addImageAttachment( resource_id, info );
	}

	/// \brief register resource with this renderpass, access Read unless otherwise specified
	RenderPassRef &useResource( LeResourceHandle resource_id, uint32_t access_flags = LeAccessFlagBits::eLeAccessFlagBitRead ) {
		le_renderer::renderpass_i.use_resource( self, resource_id, access_flags );
		return *this;
	}

	RenderPassRef &createResource( LeResourceHandle resource_id, const le_resource_info_t &info ) {
		le_renderer::renderpass_i.create_resource( self, resource_id, info );
		return *this;
	}

	RenderPassRef &setIsRoot( bool isRoot = true ) {
		le_renderer::renderpass_i.set_is_root( self, isRoot );
		return *this;
	}

	RenderPassRef &sampleTexture( LeResourceHandle textureName, const LeTextureInfo &texInfo ) {
		le_renderer::renderpass_i.sample_texture( self, textureName, &texInfo );
		return *this;
	}

	RenderPassRef &setWidth( uint32_t width ) {
		le_renderer::renderpass_i.set_width( self, width );
		return *this;
	}

	RenderPassRef &setHeight( uint32_t height ) {
		le_renderer::renderpass_i.set_height( self, height );
		return *this;
	}
};

// ----------------------------------------------------------------------

class RenderModule {

	le_render_module_o *self;
	bool                is_reference = false;

  public:
	RenderModule()
	    : self( le_renderer::render_module_i.create() ) {
	}

	RenderModule( le_render_module_o *self_ )
	    : self( self_ )
	    , is_reference( true ) {
	}

	~RenderModule() {
		if ( !is_reference ) {
			le_renderer::render_module_i.destroy( self );
		}
	}

	operator auto() {
		return self;
	}

	void addRenderPass( le_renderpass_o *renderpass ) {
		le_renderer::render_module_i.add_renderpass( self, renderpass );
	}

	void setupPasses( le_graph_builder_o *gb_ ) {
		le_renderer::render_module_i.setup_passes( self, gb_ );
	}
};

class Encoder {
	// non-owning version of RenderPass, but with more public methods

	le_command_buffer_encoder_o *self = nullptr;

  public:
	Encoder()  = delete;
	~Encoder() = default;

	Encoder( le_command_buffer_encoder_o *self_ )
	    : self( self_ ) {
	}

	operator auto() {
		return self;
	}

	Encoder &draw( const uint32_t &vertexCount, const uint32_t &instanceCount, const uint32_t &firstVertex, const uint32_t &firstInstance ) {
		le_renderer::encoder_i.draw( self, vertexCount, instanceCount, firstVertex, firstInstance );
		return *this;
	}

	Encoder &drawIndexed( uint32_t const &indexCount, uint32_t const &instanceCount, uint32_t const &firstIndex, int32_t const &vertexOffset, uint32_t const &firstInstance ) {
		le_renderer::encoder_i.draw_indexed( self, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance );
		return *this;
	}

	Encoder &setLineWidth( float const &lineWidth ) {
		le_renderer::encoder_i.set_line_width( self, lineWidth );
		return *this;
	}

	Encoder &setViewports( uint32_t firstViewport, const uint32_t &viewportCount, const le::Viewport *pViewports ) {
		le_renderer::encoder_i.set_viewport( self, firstViewport, viewportCount, pViewports );
		return *this;
	}

	Encoder &setScissors( uint32_t firstScissor, const uint32_t scissorCount, const le::Rect2D *pViewports ) {
		le_renderer::encoder_i.set_scissor( self, firstScissor, scissorCount, pViewports );
		return *this;
	}

	Encoder &bindGraphicsPipeline( uint64_t gpsoHash ) {
		le_renderer::encoder_i.bind_graphics_pipeline( self, gpsoHash );
		return *this;
	}

	Encoder &bindIndexBuffer( le::ResourceHandle const &bufferId, uint64_t const &offset, uint64_t const &indexType = 0 ) {
		le_renderer::encoder_i.bind_index_buffer( self, bufferId, offset, indexType );
		return *this;
	}

	Encoder &bindVertexBuffer( uint32_t const &firstBinding, uint32_t const &bindingCount, LeResourceHandle const *pBufferId, uint64_t const *pOffsets ) {
		le_renderer::encoder_i.bind_vertex_buffers( self, firstBinding, bindingCount, pBufferId, pOffsets );
		return *this;
	}

	Encoder &setIndexData( void const *data, uint64_t const &numBytes, uint64_t const &indexType ) {
		le_renderer::encoder_i.set_index_data( self, data, numBytes, indexType );
		return *this;
	}

	Encoder &setVertexData( void const *data, uint64_t const &numBytes, uint32_t const &bindingIndex ) {
		le_renderer::encoder_i.set_vertex_data( self, data, numBytes, bindingIndex );
		return *this;
	}

	Encoder &writeToBuffer( le::ResourceHandle const &resourceId, size_t const &offset, void const *data, size_t const &numBytes ) {
		le_renderer::encoder_i.write_to_buffer( self, resourceId, offset, data, numBytes );
		return *this;
	}

	Encoder &writeToImage( le::ResourceHandle const resourceId, struct LeBufferWriteRegion const &region, void const *data, size_t const &numBytes ) {
		le_renderer::encoder_i.write_to_image( self, resourceId, region, data, numBytes );
		return *this;
	}

	Encoder &setArgumentData( uint64_t const &argumentNameId, void const *data, size_t const &numBytes ) {
		le_renderer::encoder_i.set_argument_ubo_data( self, argumentNameId, data, numBytes );
		return *this;
	}

	Encoder &setArgumentTexture( LeResourceHandle const &textureId, uint64_t const &argumentName, uint64_t const &arrayIndex ) {
		le_renderer::encoder_i.set_argument_texture( self, textureId, argumentName, arrayIndex );
		return *this;
	}
};
// ----------------------------------------------------------------------

} // namespace le
#endif // __cplusplus
#endif // GUARD_LE_RENDERER_H

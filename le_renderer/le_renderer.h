#ifndef GUARD_LE_RENDERER_H
#define GUARD_LE_RENDERER_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

// FIXME: remove this
#define LE_RENDERPASS_MARKER_EXTERNAL "rp-external"

// depends on le_backend_vk. le_backend_vk must be loaded before this class is used.

#ifdef __cplusplus
extern "C" {
#endif

void register_le_renderer_api( void *api );
void register_le_rendergraph_api( void *api );             // in le_rendergraph.cpp
void register_le_command_buffer_encoder_api( void *api_ ); // in le_command_buffer_encoder.cpp

enum LeRenderPassType : uint32_t {
	LE_RENDER_PASS_TYPE_UNDEFINED = 0,
	LE_RENDER_PASS_TYPE_DRAW      = 1,
	LE_RENDER_PASS_TYPE_TRANSFER  = 2,
	LE_RENDER_PASS_TYPE_COMPUTE   = 3,
};

enum LeAttachmentStoreOp : uint32_t {
	LE_ATTACHMENT_STORE_OP_STORE    = 0,
	LE_ATTACHMENT_STORE_OP_DONTCARE = 1,
};

enum LeAttachmentLoadOp : uint32_t {
	LE_ATTACHMENT_LOAD_OP_LOAD     = 0,
	LE_ATTACHMENT_LOAD_OP_CLEAR    = 1,
	LE_ATTACHMENT_LOAD_OP_DONTCARE = 2,
};

enum class LeShaderType : uint64_t {
	eNone        = 0, // no default type for shader modules, you must specify a type
	eVert        = 0x00000001,
	eTessControl = 0x00000002,
	eTessEval    = 0x00000004,
	eGeom        = 0x00000008,
	eFrag        = 0x00000010,
	eCompute     = 0x00000020,
	eAllGraphics = 0x0000001F, // max needed space to cover this enum is 6 bit
};

enum class LeResourceType : uint32_t {
	eUndefined = 0,
	eBuffer,
	eImage,
};

namespace vk {
enum class Format; // forward declaration

} // namespace vk

namespace le {
struct Viewport {
	float x;
	float y;
	float width;
	float height;
	float minDepth;
	float maxDepth;
};

struct Rect2D {
	uint32_t x;
	uint32_t y;
	uint32_t width;
	uint32_t height;
};
struct Extent3D {
	uint32_t width;
	uint32_t height;
	uint32_t depth;
};

} // namespace le

struct le_renderer_o;
struct le_render_module_o;
struct le_renderpass_o;
struct le_graph_builder_o;
struct le_command_buffer_encoder_o;
struct le_backend_o;
struct le_allocator_o;

struct le_shader_module_o; ///< shader module, 1:1 relationship with a shader source file

struct le_vertex_input_attribute_description {

	// Note that we store the log2 of the number of Bytes needed to store values of a type in the LS nibble,
	// so that we can say: numBytes =  1 << (type & 0x0F);
	enum TYPE : uint8_t {
		eChar  = ( 0 << 4 ) | 0,
		eHalf  = ( 1 << 4 ) | 1,
		eInt   = ( 2 << 4 ) | 2,
		eUInt  = ( 3 << 4 ) | 2,
		eFloat = ( 4 << 4 ) | 2,
	};

	uint64_t xxx_padding : 19;
	bool     isNormalised : 1;    /// 0..1 (), 19 bits of unused space...
	uint64_t vecsize : 4;         /// 0..7 (number of elements)
	TYPE     type : 8;            /// use enum NUM_BYTES (1 << num bytes =  1|2|4|8)
	uint64_t binding_offset : 16; /// 0..65535 offset for this location within binding
	uint64_t binding : 8;         /// 0..255
	uint64_t location : 8;        /// 0..255
};

struct le_vertex_input_binding_description {
	enum INPUT_RATE : bool {
		ePerVertex   = 0,
		ePerInstance = 1,
	};
	INPUT_RATE input_rate : 1;
	uint32_t   stride : 16;
	uint32_t   binding : 4;
};

struct le_graphics_pipeline_create_info_t {
	le_shader_module_o *shader_module_frag = nullptr;
	le_shader_module_o *shader_module_vert = nullptr;

	le_vertex_input_attribute_description *vertex_input_attribute_descriptions       = nullptr;
	size_t                                 vertex_input_attribute_descriptions_count = 0;
	le_vertex_input_binding_description *  vertex_input_binding_descriptions         = nullptr;
	size_t                                 vertex_input_binding_descriptions_count   = 0;
};

struct le_graphics_pipeline_state_o; // object containing pipeline state

struct LeTextureInfo {
	struct SamplerInfo {
		uint32_t minFilter;
		uint32_t magFilter;
	};
	struct ImageViewInfo {
		uint64_t imageId; // le image resource id
		uint32_t format;  // vkFormat
	};
	SamplerInfo   sampler;
	ImageViewInfo imageView;
};

struct LeImageAttachmentInfo {
	uint64_t            resource_id  = 0; // hash name given to this attachment, based on name string
	uint64_t            source_id    = 0; // hash name of writer/creator renderpass
	uint8_t             access_flags = 0; // read, write or readwrite
	vk::Format          format;
	LeAttachmentLoadOp  loadOp;
	LeAttachmentStoreOp storeOp;

	void ( *onClear )( void *clear_data ) = nullptr;
	char debugName[ 32 ];
};

struct le_resource_info_t {

	struct Image {
		uint32_t     flags;       // creation flags
		uint32_t     imageType;   // enum vk::ImageType
		uint32_t     format;      // enum vk::Format
		le::Extent3D extent;      //
		uint32_t     mipLevels;   //
		uint32_t     arrayLayers; //
		uint32_t     samples;     // enum VkSampleCountFlagBits
		uint32_t     tiling;      // enum VkImageTiling
		uint32_t     usage;       // usage flags
		uint32_t     sharingMode; // enum vkSharingMode
	};

	struct Buffer {
		size_t   size;
		uint32_t usage_flags;
	};

	LeResourceType type;
	union {
		Buffer buffer;
		Image  image;
	};
};

// clang-format off
struct le_renderer_api {

	static constexpr auto id      = "le_renderer";
	static constexpr auto pRegFun = register_le_renderer_api;


	struct renderer_interface_t {
		le_renderer_o *                ( *create                                )( le_backend_o  *backend );
		void                           ( *destroy                               )( le_renderer_o *obj );
		void                           ( *setup                                 )( le_renderer_o *obj );
		void                           ( *update                                )( le_renderer_o *obj, le_render_module_o *module );
		le_graphics_pipeline_state_o * ( *create_graphics_pipeline_state_object )( le_renderer_o *self, le_graphics_pipeline_create_info_t const *pipeline_info );
		le_shader_module_o*            ( *create_shader_module                  )( le_renderer_o *self, char const *path, LeShaderType mtype );
	};

	enum AccessFlagBits : uint32_t {
		eRead      = 0x01,
		eWrite     = 0x02,
		eReadWrite = eRead | eWrite,
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
		void                         ( *add_image_attachment )( le_renderpass_o *obj, uint64_t resource_id, LeImageAttachmentInfo *info );
		void                         ( *set_execute_callback )( le_renderpass_o *obj, pfn_renderpass_execute_t render_fun, void *user_data );
		void                         ( *run_execute_callback )( le_renderpass_o* obj, le_command_buffer_encoder_o* encoder);
		bool                         ( *has_execute_callback )( const le_renderpass_o* obj);
		void                         ( *use_resource         )( le_renderpass_o *obj, uint64_t resource_id, uint32_t access_flags );
		void                         ( *create_resource      )( le_renderpass_o *obj, uint64_t resource_id, const le_resource_info_t &info );
		void                         ( *set_is_root          )( le_renderpass_o *obj, bool is_root );
		bool                         ( *get_is_root          )( const le_renderpass_o *obj);
		void                         ( *set_sort_key         )( le_renderpass_o *obj, uint64_t sort_key);
		uint64_t                     ( *get_sort_key         )( const le_renderpass_o *obj);
		void                         ( *get_read_resources   )( const le_renderpass_o * obj, const uint64_t ** pReadResources, size_t* count );
		void                         ( *get_write_resources  )( const le_renderpass_o * obj, const uint64_t ** pWriteResources, size_t* count );
		void                         ( *get_create_resources )( const le_renderpass_o *obj, uint64_t const **pCreateResources, le_resource_info_t const **pResourceInfos, size_t *count );
		const char*                  ( *get_debug_name       )( const le_renderpass_o* obj );
		uint64_t                     ( *get_id               )( const le_renderpass_o* obj );
		LeRenderPassType             ( *get_type             )( const le_renderpass_o* obj );
		le_command_buffer_encoder_o* ( *steal_encoder        )( le_renderpass_o* obj );
		void                         ( *get_image_attachments)(const le_renderpass_o* obj, const LeImageAttachmentInfo** pAttachments, size_t* numAttachments);

		// TODO: not too sure about the nomenclature of this
		// Note that this method does use the resource in question for reading.
		void                         ( *sample_texture        )(le_renderpass_o* obj, uint64_t texture_name, const LeTextureInfo* info);
		void                         ( *get_texture_ids       )(le_renderpass_o* obj, const uint64_t ** pIds, uint64_t* count);
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
		void                         ( *bind_graphics_pipeline )( le_command_buffer_encoder_o *self, le_graphics_pipeline_state_o* pipeline);

		void                         ( *bind_index_buffer      )( le_command_buffer_encoder_o *self, uint64_t bufferId, uint64_t offset, uint64_t indexType);
		void                         ( *bind_vertex_buffers    )( le_command_buffer_encoder_o *self, uint32_t firstBinding, uint32_t bindingCount, uint64_t *pBufferIds, uint64_t *pOffsets );

		void                         ( *set_index_data         )( le_command_buffer_encoder_o *self, void *data, uint64_t numBytes, uint64_t indexType );
		void                         ( *set_vertex_data        )( le_command_buffer_encoder_o *self, void *data, uint64_t numBytes, uint32_t bindingIndex );

		void                         ( *write_to_buffer        )( le_command_buffer_encoder_o *self, uint64_t resourceId, size_t offset, void const* data, size_t numBytes);
		void                         ( *write_to_image         )( le_command_buffer_encoder_o *self, uint64_t resourceId, struct LeBufferWriteRegion const &region, void const *data, size_t numBytes );

		// stores ubo argument data to scratch buffer - note that parameter index must be dynamic offset index
		void                         ( *set_argument_ubo_data  ) (le_command_buffer_encoder_o *self, uint64_t argumentNameId, void * data, size_t numBytes);
		void                         ( *set_argument_texture   ) (le_command_buffer_encoder_o* self, uint64_t textureId, uint64_t argumentName, uint64_t arrayIndex);

		void                         ( *get_encoded_data       )( le_command_buffer_encoder_o *self, void **data, size_t *numBytes, size_t *numCommands );
	};


	renderpass_interface_t             le_renderpass_i;
	rendermodule_interface_t           le_render_module_i;
	graph_builder_interface_t          le_graph_builder_i;
	renderer_interface_t               le_renderer_i;
	command_buffer_encoder_interface_t le_command_buffer_encoder_i;
};
// clang-format on

#ifdef __cplusplus
} // extern "C"

namespace le {

using AccessFlagBits = le_renderer_api::AccessFlagBits;

class Renderer {
	const le_renderer_api &                      rendererApiI = *Registry::getApi<le_renderer_api>();
	const le_renderer_api::renderer_interface_t &rendererI    = rendererApiI.le_renderer_i;

	le_renderer_o *self;

  public:
	Renderer( le_backend_o *backend )
	    : self( rendererI.create( backend ) ) {
	}

	~Renderer() {
		rendererI.destroy( self );
	}

	void setup() {
		rendererI.setup( self );
	}

	void update( le_render_module_o *module ) {
		rendererI.update( self, module );
	}

	le_graphics_pipeline_state_o *createGraphicsPipelineStateObject( le_graphics_pipeline_create_info_t const *info ) {
		return rendererI.create_graphics_pipeline_state_object( self, info );
	}

	le_shader_module_o *createShaderModule( char const *path, LeShaderType moduleType ) {
		return rendererI.create_shader_module( self, path, moduleType );
	}
};

class RenderPass {
	const le_renderer_api &                        rendererApiI = *Registry::getApi<le_renderer_api>();
	const le_renderer_api::renderpass_interface_t &renderpassI  = rendererApiI.le_renderpass_i;

	le_renderpass_o *self;

  public:
	RenderPass( const char *name_, const LeRenderPassType &type_ )
	    : self( renderpassI.create( name_, type_ ) ) {
	}

	~RenderPass() {
		renderpassI.destroy( self );
	}

	operator auto() {
		return self;
	}

	void setSetupCallback( void *user_data, le_renderer_api::pfn_renderpass_setup_t fun ) {
		renderpassI.set_setup_callback( self, fun, user_data );
	}

	void setExecuteCallback( void *user_data, le_renderer_api::pfn_renderpass_execute_t fun ) {
		renderpassI.set_execute_callback( self, fun, user_data );
	}
};

// ----------------------------------------------------------------------

class RenderPassRef {
	// non-owning version of RenderPass, but with more public methods
	const le_renderer_api &                        rendererApiI = *Registry::getApi<le_renderer_api>();
	const le_renderer_api::renderpass_interface_t &renderpassI  = rendererApiI.le_renderpass_i;

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

	RenderPassRef &addImageAttachment( uint64_t resource_id, LeImageAttachmentInfo *info ) {
		renderpassI.add_image_attachment( self, resource_id, info );
		return *this;
	}

	RenderPassRef &useResource( uint64_t resource_id, uint32_t access_flags ) {
		renderpassI.use_resource( self, resource_id, access_flags );
		return *this;
	}

	RenderPassRef &createResource( uint64_t resource_id, const le_resource_info_t &info ) {
		renderpassI.create_resource( self, resource_id, info );
		return *this;
	}

	RenderPassRef &setIsRoot( bool isRoot = true ) {
		renderpassI.set_is_root( self, isRoot );
		return *this;
	}

	RenderPassRef &sampleTexture( uint64_t textureName, const LeTextureInfo &texInfo ) {
		renderpassI.sample_texture( self, textureName, &texInfo );
		return *this;
	}
};

// ----------------------------------------------------------------------

class RenderModule {
	const le_renderer_api &                          rendererApiI  = *Registry::getApi<le_renderer_api>();
	const le_renderer_api::rendermodule_interface_t &rendermoduleI = rendererApiI.le_render_module_i;

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

	void addRenderPass( le_renderpass_o *renderpass ) {
		rendermoduleI.add_renderpass( self, renderpass );
	}

	void setupPasses( le_graph_builder_o *gb_ ) {
		rendermoduleI.setup_passes( self, gb_ );
	}
};

// ----------------------------------------------------------------------

class CommandBufferEncoder : NoCopy, NoMove {
	const le_renderer_api &                                    rendererApiI = *Registry::getApi<le_renderer_api>();
	const le_renderer_api::command_buffer_encoder_interface_t &cbEncoderI   = rendererApiI.le_command_buffer_encoder_i;

	le_command_buffer_encoder_o *self;
	bool                         is_reference = false;

  public:
	CommandBufferEncoder( le_allocator_o *allocator )
	    : self( cbEncoderI.create( allocator ) ) {
	}

	CommandBufferEncoder( le_command_buffer_encoder_o *self_ )
	    : self( self_ )
	    , is_reference( true ) {
	}

	~CommandBufferEncoder() {
		if ( !is_reference ) {
			cbEncoderI.destroy( self );
		}
	}

	operator auto() {
		return self;
	}

	void setLineWidth( float lineWidth ) {
		cbEncoderI.set_line_width( self, lineWidth );
	}

	void draw( uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance ) {
		cbEncoderI.draw( self, vertexCount, instanceCount, firstVertex, firstInstance );
	}

	void setViewport( uint32_t firstViewport, const uint32_t viewportCount, const le::Viewport *pViewports ) {
		cbEncoderI.set_viewport( self, firstViewport, viewportCount, pViewports );
	}

	void setScissor( uint32_t firstScissor, const uint32_t scissorCount, const le::Rect2D *pScissors ) {
		cbEncoderI.set_scissor( self, firstScissor, scissorCount, pScissors );
	}

	void bindVertexBuffers( uint32_t firstBinding, uint32_t bindingCount, uint64_t *pBuffers, uint64_t *pOffsets ) {
		cbEncoderI.bind_vertex_buffers( self, firstBinding, bindingCount, pBuffers, pOffsets );
	}

	void setVertexData( void *data, uint64_t numBytes, uint32_t bindingIndex ) {
		cbEncoderI.set_vertex_data( self, data, numBytes, bindingIndex );
	}
};

} // namespace le
#endif // __cplusplus
#endif // GUARD_LE_RENDERER_H

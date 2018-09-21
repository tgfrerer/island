#ifndef GUARD_LE_RENDERER_H
#define GUARD_LE_RENDERER_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#include "private/le_renderer_types.h"

// FIXME: remove this
#define LE_RENDERPASS_MARKER_EXTERNAL "rp-external"

#ifdef __cplusplus
extern "C" {
#endif

void register_le_renderer_api( void *api );
void register_le_rendergraph_api( void *api );             // in le_rendergraph.cpp
void register_le_command_buffer_encoder_api( void *api_ ); // in le_command_buffer_encoder.cpp

enum LeRenderPassType : uint32_t {
	LE_RENDER_PASS_TYPE_UNDEFINED = 0,
	LE_RENDER_PASS_TYPE_DRAW      = 1, // << most common case, should be 0
	LE_RENDER_PASS_TYPE_TRANSFER  = 2,
	LE_RENDER_PASS_TYPE_COMPUTE   = 3,
};

enum LeAttachmentStoreOp : uint32_t {
	LE_ATTACHMENT_STORE_OP_STORE    = 0, // << most common case
	LE_ATTACHMENT_STORE_OP_DONTCARE = 1,
};

enum LeAttachmentLoadOp : uint32_t {
	LE_ATTACHMENT_LOAD_OP_CLEAR    = 0, // << most common case
	LE_ATTACHMENT_LOAD_OP_LOAD     = 1,
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

enum class LeResourceType : uint8_t {
	eUndefined = 0,
	eBuffer,
	eImage,
	eTexture,
};

typedef int LeFormat_t; // we're declaring this as a placeholder for image format enum

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

// forward declaration of Vk Types -- not sure if we don't want to wrap these eventually
#define DECLARE_VK_TO_LE_HANDLE( object ) typedef struct Vk##object *Le##object##Handle;

DECLARE_VK_TO_LE_HANDLE( PipelineRasterizationStateCreateInfo )

/// \note This struct assumes a little endian machine for sorting
struct le_vertex_input_attribute_description {

	// Note that we store the log2 of the number of Bytes needed to store values of a type
	// in the least significant two bits, so that we can say: numBytes =  1 << (type & 0x03);
	enum Type : uint8_t {
		eChar   = ( 0 << 2 ) | 0,
		eUChar  = ( 1 << 2 ) | 0,
		eShort  = ( 2 << 2 ) | 1,
		eUShort = ( 3 << 2 ) | 1,
		eInt    = ( 4 << 2 ) | 2,
		eUInt   = ( 5 << 2 ) | 2,
		eHalf   = ( 6 << 2 ) | 1, // 16 bit float type
		eFloat  = ( 7 << 2 ) | 2, // 32 bit float type
	};

	union {
		struct {
			uint8_t  location;       /// 0..32 shader attribute location
			uint8_t  binding;        /// 0..32 binding slot
			uint16_t binding_offset; /// 0..65565 offset for this location within binding (careful: must not be larget than maxVertexInputAttributeOffset [0.0x7ff])
			Type     type;           /// base type for attribute
			uint8_t  vecsize;        /// 0..7 number of elements of base type
			uint8_t  isNormalised;   /// whether this input comes pre-normalized
		};
		uint64_t raw_data = 0;
	};
};

struct le_vertex_input_binding_description {
	enum INPUT_RATE : uint8_t {
		ePerVertex   = 0,
		ePerInstance = 1,
	};

	union {
		struct {
			uint8_t    binding;    /// binding slot 0..32(==MAX_ATTRIBUTE_BINDINGS)
			INPUT_RATE input_rate; /// per-vertex (0) or per-instance (1)
			uint16_t   stride;     /// per-vertex or per-instance stride in bytes (must be smaller than maxVertexInputBindingStride = [0x800])
		};
		uint32_t raw_data;
	};
};

struct le_graphics_pipeline_create_info_t {
	le_shader_module_o *shader_module_frag = nullptr;
	le_shader_module_o *shader_module_vert = nullptr;

	le_vertex_input_attribute_description *      vertex_input_attribute_descriptions       = nullptr;
	size_t                                       vertex_input_attribute_descriptions_count = 0;
	le_vertex_input_binding_description *        vertex_input_binding_descriptions         = nullptr;
	size_t                                       vertex_input_binding_descriptions_count   = 0;
	LePipelineRasterizationStateCreateInfoHandle rasterizationState                        = nullptr;
};

struct le_graphics_pipeline_state_o; // object containing pipeline state

enum LeAccessFlagBits : uint32_t {
	eLeAccessFlagBitUndefined  = 0x0,
	eLeAccessFlagBitRead       = 0x1 << 0,
	eLeAccessFlagBitWrite      = 0x1 << 1,
	eLeAccessFlagBitsReadWrite = eLeAccessFlagBitRead | eLeAccessFlagBitWrite,
};

typedef uint32_t LeAccessFlags;

struct LeTextureInfo {
	struct SamplerInfo {
		int minFilter; // enum VkFilter
		int magFilter; // enum VkFilter
		               // TODO: add clamp clamp modes etc.
	};
	struct ImageViewInfo {
		LeResourceHandle imageId; // le image resource id
		int              format;  // enum VkFormat, leave at 0 (undefined) to use format of image referenced by `imageId`
	};
	SamplerInfo   sampler;
	ImageViewInfo imageView;
};

struct LeClearColorValue {
	union {
		float    float32[ 4 ];
		int32_t  int32[ 4 ];
		uint32_t uint32[ 4 ];
	};
};

struct LeClearDepthStencilValue {
	float    depth;
	uint32_t stencil;
};

struct LeClearValue {
	union {
		LeClearColorValue        color;
		LeClearDepthStencilValue depthStencil;
	};
};

struct LeImageAttachmentInfo {

	static constexpr LeClearValue DefaultClearValueColor        = {{{{{0.f, 0.f, 0.f, 0.f}}}}};
	static constexpr LeClearValue DefaultClearValueDepthStencil = {{{{{1.f, 0}}}}};

	LeAccessFlags       access_flags = eLeAccessFlagBitWrite;        // read, write or readwrite (default is write)
	LeAttachmentLoadOp  loadOp       = LE_ATTACHMENT_LOAD_OP_CLEAR;  //
	LeAttachmentStoreOp storeOp      = LE_ATTACHMENT_STORE_OP_STORE; //
	LeClearValue        clearValue   = DefaultClearValueColor;       // only used if loadOp == clear

	LeFormat_t       format      = 0;       // if format is not given it will be automatically derived from attached image format
	LeResourceHandle resource_id = nullptr; // (private - do not set) handle given to this attachment
	uint64_t         source_id   = 0;       // (private - do not set) hash name of writer/creator renderpass

	char debugName[ 32 ];
};

struct le_resource_info_t {

	struct Image {
		uint32_t     flags;       // creation flags
		uint32_t     imageType;   // enum vk::ImageType
		int32_t      format;      // enum vk::Format
		le::Extent3D extent;      //
		uint32_t     mipLevels;   //
		uint32_t     arrayLayers; //
		uint32_t     samples;     // enum VkSampleCountFlagBits
		uint32_t     tiling;      // enum VkImageTiling
		uint32_t     usage;       // usage flags
		uint32_t     sharingMode; // enum vkSharingMode
	};

	struct Buffer {
		uint32_t size;
		uint32_t usage; // e.g. VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
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

		/// introduces a new resource name to the renderer, returns a handle under which the renderer will recognise this resource
		LeResourceHandle               ( *declare_resource                      )( le_renderer_o* self, LeResourceType type );

		/// returns the resource handle for the current swapchain image
		LeResourceHandle               ( *get_backbuffer_resource               )( le_renderer_o* self );
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
		void                         ( *bind_graphics_pipeline )( le_command_buffer_encoder_o *self, le_graphics_pipeline_state_o* pipeline);

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

	le_graphics_pipeline_state_o *createGraphicsPipelineStateObject( le_graphics_pipeline_create_info_t const *info ) {
		return le_renderer::renderer_i.create_graphics_pipeline_state_object( self, info );
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

// ----------------------------------------------------------------------

} // namespace le
#endif // __cplusplus
#endif // GUARD_LE_RENDERER_H

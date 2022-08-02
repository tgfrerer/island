#ifndef GUARD_LE_RENDERER_H
#define GUARD_LE_RENDERER_H

#include "le_core.h"
#include "private/le_renderer_types.h"

extern const uint64_t LE_RENDERPASS_MARKER_EXTERNAL; // set in le_renderer.cpp

struct le_renderer_o;
struct le_renderpass_o;
struct le_rendergraph_o;
struct le_command_buffer_encoder_o;
struct le_backend_o;
struct le_shader_module_o; ///< shader module, 1:1 relationship with a shader source file
struct le_pipeline_manager_o;

struct le_allocator_o;         // from backend
struct le_staging_allocator_o; // from backend

LE_OPAQUE_HANDLE( le_shader_module_handle );

#define LE_BUF_RESOURCE( x ) \
	le_renderer::renderer_i.produce_buf_resource_handle( ( x ), 0, 0 )

#define LE_IMG_RESOURCE( x ) \
	le_renderer::renderer_i.produce_img_resource_handle( ( x ), 0, 0, 0 )

struct le_shader_binding_table_o;

// clang-format off
struct le_renderer_api {

	struct renderer_interface_t {
		le_renderer_o *                ( *create                  )( );
		void                           ( *destroy                 )( le_renderer_o *obj );
		void                           ( *setup                   )( le_renderer_o *obj, le_renderer_settings_t const & settings );
		void                           ( *update                  )( le_renderer_o *obj, le_rendergraph_o *rendergraph);

        le_renderer_settings_t const * ( *get_settings            )( le_renderer_o* self );

		/// returns the image resource handle for a swapchain at given index
		uint32_t                       ( *get_swapchain_count     )( le_renderer_o* self);
		le_img_resource_handle         ( *get_swapchain_resource  )( le_renderer_o* self, uint32_t index );
		void                           ( *get_swapchain_extent    )( le_renderer_o* self, uint32_t index, uint32_t* p_width, uint32_t* p_height );
		le_backend_o*                  ( *get_backend             )( le_renderer_o* self );

		le_pipeline_manager_o*         ( *get_pipeline_manager    )( le_renderer_o* self );

        le_texture_handle              ( *produce_texture_handle  )(char const * maybe_name );
        char const *                   ( *texture_handle_get_name )(le_texture_handle handle);

        le_buf_resource_handle (*produce_buf_resource_handle)(char const * maybe_name, uint8_t flags, uint16_t index);
        le_img_resource_handle (*produce_img_resource_handle)(char const * maybe_name, uint8_t num_samples, le_img_resource_handle reference_handle, uint8_t flags);

        le_tlas_resource_handle (*produce_tlas_resource_handle)(char const * maybe_name);
        le_blas_resource_handle (*produce_blas_resource_handle)(char const * maybe_name);

		le_rtx_blas_info_handle        ( *create_rtx_blas_info ) (le_renderer_o* self, le_rtx_geometry_t* geometries, uint32_t geometries_count, le::BuildAccelerationStructureFlagsKHR const & flags);
		le_rtx_tlas_info_handle        ( *create_rtx_tlas_info ) (le_renderer_o* self, uint32_t instances_count, le::BuildAccelerationStructureFlagsKHR const& flags);
	};



	struct helpers_interface_t {
		le_resource_info_t (*get_default_resource_info_for_image)();
		le_resource_info_t (*get_default_resource_info_for_buffer)();
	};

	typedef bool ( *pfn_renderpass_setup_t )( le_renderpass_o *obj, void* user_data );
	typedef void ( *pfn_renderpass_execute_t )( le_command_buffer_encoder_o *encoder, void *user_data );

	struct renderpass_interface_t {
		le_renderpass_o *               ( *create               )( const char *renderpass_name, const le::QueueFlagBits &type_ );
		void                            ( *destroy              )( le_renderpass_o *obj );
		le_renderpass_o *               ( *clone                )( const le_renderpass_o *obj );
        void                            ( *set_setup_callback   )( le_renderpass_o *obj, void *user_data, pfn_renderpass_setup_t setup_fun );
		bool                            ( *has_setup_callback   )( const le_renderpass_o* obj);
		void                            ( *add_color_attachment )( le_renderpass_o *obj, le_img_resource_handle resource_id, le_image_attachment_info_t const *info );
		void                            ( *add_depth_stencil_attachment )( le_renderpass_o *obj, le_img_resource_handle resource_id, le_image_attachment_info_t const *info );
		void                            ( *set_width            )( le_renderpass_o* obj, uint32_t width);
		void                            ( *set_height           )( le_renderpass_o* obj, uint32_t height);
		void                            ( *set_sample_count     ) (le_renderpass_o* obj, le::SampleCountFlagBits const & sampleCount);
		bool                            ( *get_framebuffer_settings)(le_renderpass_o const * obj, uint32_t* width, uint32_t* height, le::SampleCountFlagBits* sample_count);
		void                            ( *set_execute_callback )( le_renderpass_o *obj, void *user_data, pfn_renderpass_execute_t render_fun );
		bool                            ( *has_execute_callback )( const le_renderpass_o* obj);
		void                            ( *use_img_resource     )( le_renderpass_o *obj, const le_img_resource_handle& resource_id, const LeResourceUsageFlags &usage_flags);
		void                            ( *use_buf_resource     )( le_renderpass_o *obj, const le_buf_resource_handle& resource_id, const LeResourceUsageFlags &usage_flags);
		void                            ( *use_resource         )( le_renderpass_o *obj, const le_resource_handle& resource_id, const LeResourceUsageFlags &usage_flags);
		void                            ( *set_is_root          )( le_renderpass_o *obj, bool is_root );
		bool                            ( *get_is_root          )( const le_renderpass_o *obj);
		void                            ( *get_used_resources   )( const le_renderpass_o *obj, le_resource_handle const **pResourceIds, LeResourceUsageFlags const **pResourcesUsage, size_t *count );
		const char*                     ( *get_debug_name       )( const le_renderpass_o* obj );
		uint64_t                        ( *get_id               )( const le_renderpass_o* obj );
		void                            ( *get_queue_sumbission_info)( const le_renderpass_o* obj, le::QueueFlagBits* pass_type, le::RootPassesField * queue_submission_id);
		le_command_buffer_encoder_o*    ( *steal_encoder        )( le_renderpass_o* obj );
		void                            ( *get_image_attachments)(const le_renderpass_o* obj, const le_image_attachment_info_t** pAttachments, const le_img_resource_handle ** pResourceIds, size_t* numAttachments);

		// Reference counting
		void (*ref_inc)(le_renderpass_o* self);
		void (*ref_dec)(le_renderpass_o* self);

		// TODO: not too sure about the nomenclature of this
		// Note that this method implicitly marks the image resource referenced in LeTextureInfo for read access.
		void                         ( *sample_texture        )(le_renderpass_o* obj, le_texture_handle texture, const le_image_sampler_info_t* info);

		void                         ( *get_texture_ids       )(le_renderpass_o* obj, le_texture_handle const ** pIds, uint64_t* count);
		void                         ( *get_texture_infos     )(le_renderpass_o* obj, le_image_sampler_info_t const ** pInfos, uint64_t* count);
	};

	// Graph builder builds a graph for a rendergraph
	struct rendergraph_interface_t {
		le_rendergraph_o *   ( *create           ) ( );
		void                 ( *destroy          ) ( le_rendergraph_o *self );
		void                 ( *reset            ) ( le_rendergraph_o *self );
		void                 ( *add_renderpass   ) ( le_rendergraph_o *self, le_renderpass_o *rp );
		void                 ( *declare_resource ) ( le_rendergraph_o *self, le_resource_handle const & resource_id, le_resource_info_t const & info);
	};

	struct rendergraph_private_interface_t {
		void                 ( *build                  ) ( le_rendergraph_o *self, size_t frameNumber );
		void                 ( *execute                ) ( le_rendergraph_o *self, size_t frameIndex, le_backend_o *backend );
		void                 ( *setup_passes           ) ( le_rendergraph_o *self, le_rendergraph_o *gb );
		void                 ( *get_passes             ) ( le_rendergraph_o *self, le_renderpass_o ***pPasses, size_t *pNumPasses );
		void                 ( *get_declared_resources ) ( le_rendergraph_o *self, le_resource_handle const **p_resource_handles, le_resource_info_t const **p_resource_infos, size_t *p_resource_count );
        void                 ( *get_p_affinity_masks   ) ( le_rendergraph_o* self, le::RootPassesField const **p_affinity_masks, uint32_t *num_affinity_masks );

    };

	struct command_buffer_encoder_interface_t {

        /// Used to optionally capture transient binding state from command buffers
        struct buffer_binding_info_o {
             le_resource_handle resource;
   	         uint64_t             offset;
        };

		le_command_buffer_encoder_o *( *create                 )( le_allocator_o **allocator, le_pipeline_manager_o* pipeline_cache, le_staging_allocator_o* stagingAllocator, le::Extent2D const& extent );
		void                         ( *destroy                )( le_command_buffer_encoder_o *obj );

		void                         ( *draw                   )( le_command_buffer_encoder_o *self, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance );
		void                         ( *draw_indexed           )( le_command_buffer_encoder_o *self, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);
		void                         ( *draw_mesh_tasks        )( le_command_buffer_encoder_o *self, uint32_t taskCount, uint32_t fistTask);

		void                         (* dispatch               )( le_command_buffer_encoder_o *self, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ );
		void                         (* buffer_memory_barrier  )( le_command_buffer_encoder_o *self, le::PipelineStageFlags2 const &srcStageMask, le::PipelineStageFlags2 const &dstStageMask, le::AccessFlags2 const & dstAccessMask, le_buf_resource_handle const &buffer, uint64_t const & offset, uint64_t const & range );

		void                         ( *set_line_width         )( le_command_buffer_encoder_o *self, float line_width_ );
		void                         ( *set_viewport           )( le_command_buffer_encoder_o *self, uint32_t firstViewport, const uint32_t viewportCount, const le::Viewport *pViewports );
		void                         ( *set_scissor            )( le_command_buffer_encoder_o *self, uint32_t firstScissor, const uint32_t scissorCount, const le::Rect2D *pViewports );

		void                         ( *bind_graphics_pipeline )( le_command_buffer_encoder_o *self, le_gpso_handle pipelineHandle);
		void                         ( *bind_compute_pipeline  )( le_command_buffer_encoder_o *self, le_cpso_handle pipelineHandle);

		void                         ( *bind_index_buffer      )( le_command_buffer_encoder_o *self, le_buf_resource_handle const bufferId, uint64_t offset, le::IndexType const & indexType);
		void                         ( *bind_vertex_buffers    )( le_command_buffer_encoder_o *self, uint32_t firstBinding, uint32_t bindingCount, le_buf_resource_handle const * pBufferId, uint64_t const * pOffsets );

		void                         ( *set_index_data         )( le_command_buffer_encoder_o *self, void const *data, uint64_t numBytes, le::IndexType const & indexType, buffer_binding_info_o* optional_binding_info_readback );
		void                         ( *set_vertex_data        )( le_command_buffer_encoder_o *self, void const *data, uint64_t numBytes, uint32_t bindingIndex, buffer_binding_info_o* optional_transient_binding_info_readback );

		void                         ( *write_to_buffer        )( le_command_buffer_encoder_o *self, le_buf_resource_handle const& dst_buffer, size_t dst_offset, void const* data, size_t numBytes);
		void                         ( *write_to_image         )( le_command_buffer_encoder_o *self, le_img_resource_handle const& dst_img, le_write_to_image_settings_t const & writeInfo, void const *data, size_t numBytes );

        void                         ( *set_push_constant_data )( le_command_buffer_encoder_o* self, void const *data, uint64_t numBytes);

		le::Extent2D const &         ( *get_extent             ) ( le_command_buffer_encoder_o* self );

		void                         ( *bind_argument_buffer   )( le_command_buffer_encoder_o *self, le_buf_resource_handle const bufferId, uint64_t argumentName, uint64_t offset, uint64_t range );

		void                         ( *set_argument_data      )( le_command_buffer_encoder_o *self, uint64_t argumentNameId, void const * data, size_t numBytes);
		void                         ( *set_argument_texture   )( le_command_buffer_encoder_o *self, le_texture_handle const textureId, uint64_t argumentName, uint64_t arrayIndex);
		void                         ( *set_argument_image     )( le_command_buffer_encoder_o *self, le_img_resource_handle const imageId, uint64_t argumentName, uint64_t arrayIndex);

		void                         ( *set_argument_tlas      )( le_command_buffer_encoder_o *self, le_tlas_resource_handle const tlasId, uint64_t argumentName, uint64_t arrayIndex);

		void 						 ( *build_rtx_blas         )( le_command_buffer_encoder_o *self, le_blas_resource_handle const* const blas_handles, const uint32_t handles_count);
        // one blas handle per rtx geometry instance
		void 						 ( *build_rtx_tlas         )( le_command_buffer_encoder_o *self, le_tlas_resource_handle const* tlas_handle, le_rtx_geometry_instance_t const * instances, le_blas_resource_handle const * blas_handles, uint32_t instances_count);
        
        le_shader_binding_table_o*   ( *build_sbt              )(le_command_buffer_encoder_o* self, le_rtxpso_handle pipeline);
        void                         ( *sbt_set_ray_gen        )(le_shader_binding_table_o* sbt, uint32_t ray_gen);
        void                         ( *sbt_add_hit            )(le_shader_binding_table_o* sbt, uint32_t ray_gen);
        void                         ( *sbt_add_callable       )(le_shader_binding_table_o* sbt, uint32_t ray_gen);
        void                         ( *sbt_add_miss           )(le_shader_binding_table_o* sbt, uint32_t ray_gen);
        void                         ( *sbt_add_u32_param      )(le_shader_binding_table_o* sbt, uint32_t param);
        void                         ( *sbt_add_f32_param      )(le_shader_binding_table_o* sbt, float param);

        // returns nullptr if shader binding table is in invalid state, otherwise sbt
        le_shader_binding_table_o*   ( *sbt_validate           )(le_shader_binding_table_o* sbt);

        // NOTE pipeline is implicitly bound, as it is stored with shader binding table (sbt)
		void                         ( *bind_rtx_pipeline      )( le_command_buffer_encoder_o *self, le_shader_binding_table_o* shader_binding_table);
        void                         ( *trace_rays             )( le_command_buffer_encoder_o* self, uint32_t width, uint32_t height, uint32_t depth);

		le_pipeline_manager_o*       ( *get_pipeline_manager   )( le_command_buffer_encoder_o *self );
		void                         ( *get_encoded_data       )( le_command_buffer_encoder_o *self, void **data, size_t *numBytes, size_t *numCommands );
	};

	renderer_interface_t               le_renderer_i;
	renderpass_interface_t             le_renderpass_i;
	rendergraph_interface_t            le_rendergraph_i;
	rendergraph_private_interface_t    le_rendergraph_private_i;
	command_buffer_encoder_interface_t le_command_buffer_encoder_i;
	helpers_interface_t                helpers_i;
};
// clang-format on

LE_MODULE( le_renderer );
LE_MODULE_LOAD_DEFAULT( le_renderer );

#ifdef __cplusplus

namespace le_renderer {
static const auto& api = le_renderer_api_i;

static const auto& renderer_i    = api->le_renderer_i;
static const auto& renderpass_i  = api->le_renderpass_i;
static const auto& rendergraph_i = api->le_rendergraph_i;
static const auto& encoder_i     = api->le_command_buffer_encoder_i;
static const auto& helpers_i     = api->helpers_i;

} // namespace le_renderer

namespace le {

class Renderer {

	le_renderer_o* self;

  public:
	Renderer()
	    : self( le_renderer::renderer_i.create() ) {
	}

	~Renderer() {
		le_renderer::renderer_i.destroy( self );
	}

	void setup( le_renderer_settings_t const& settings ) {
		le_renderer::renderer_i.setup( self, settings );
	}

	void setup( le_window_o* window ) {
		le_renderer::renderer_i.setup( self, le::RendererInfoBuilder( window ).build() );
	}

	/// Call this method exactly once per Frame - this is where rendergraph execution callbacks are triggered.
	void update( le_rendergraph_o* rendergraph ) {
		le_renderer::renderer_i.update( self, rendergraph );
	}

	le_renderer_settings_t const& getSettings() const noexcept {
		return *le_renderer::renderer_i.get_settings( self );
	}

	uint32_t getSwapchainCount() const {
		return le_renderer::renderer_i.get_swapchain_count( self );
	}

	le_img_resource_handle getSwapchainResource( uint32_t index = 0 ) const {
		return le_renderer::renderer_i.get_swapchain_resource( self, index );
	}

	void getSwapchainExtent( uint32_t* pWidth, uint32_t* pHeight, uint32_t index = 0 ) const {
		le_renderer::renderer_i.get_swapchain_extent( self, index, pWidth, pHeight );
	}

	const le::Extent2D getSwapchainExtent( uint32_t index = 0 ) const {
		le::Extent2D result;
		le_renderer::renderer_i.get_swapchain_extent( self, index, &result.width, &result.height );
		return result;
	}

	le_pipeline_manager_o* getPipelineManager() const {
		return le_renderer::renderer_i.get_pipeline_manager( self );
	}

	static le_texture_handle produceTextureHandle( char const* maybe_name ) {
		return le_renderer::renderer_i.produce_texture_handle( maybe_name );
	}

	static le_img_resource_handle produceImageHandle( char const* maybe_name ) {
		return le_renderer::renderer_i.produce_img_resource_handle( maybe_name, 0, nullptr, 0 );
	}

	static le_buf_resource_handle produceBufferHandle( char const* maybe_name ) {
		return le_renderer::renderer_i.produce_buf_resource_handle( maybe_name, 0, 0 );
	}

	operator auto() {
		return self;
	}
};

class RenderPass {

	le_renderpass_o* self;

  public:
	RenderPass( const char* name_, const le::QueueFlagBits& type_ = le::QueueFlagBits::eGraphics )
	    : self( le_renderer::renderpass_i.create( name_, type_ ) ) {
	}

	RenderPass( const char* name_, const le::QueueFlagBits& type_, le_renderer_api::pfn_renderpass_setup_t fun_setup, le_renderer_api::pfn_renderpass_execute_t fun_exec, void* user_data )
	    : self( le_renderer::renderpass_i.create( name_, type_ ) ) {
		le_renderer::renderpass_i.set_setup_callback( self, user_data, fun_setup );
		le_renderer::renderpass_i.set_execute_callback( self, user_data, fun_exec );
	}

	// Create facade from pointer
	RenderPass( le_renderpass_o* self_ )
	    : self( self_ ) {
		le_renderer::renderpass_i.ref_inc( self );
	}

	// Destructor
	~RenderPass() {
		// We check for validity of self, as move assignment/constructor
		// set the moved-from to null after completion.
		if ( self ) {
			le_renderer::renderpass_i.ref_dec( self );
		}
	}

	// Copy constructor
	RenderPass( RenderPass const& rhs )
	    : self( rhs.self ) {
		le_renderer::renderpass_i.ref_inc( self );
	}

	// Copy assignment
	RenderPass& operator=( RenderPass const& rhs ) {
		self = rhs.self;
		le_renderer::renderpass_i.ref_inc( self );
		return *this;
	}

	// Move constructor
	RenderPass( RenderPass&& rhs ) noexcept
	    : self( rhs.self ) {
		rhs.self = nullptr;
	}

	// Move assignment
	RenderPass& operator=( RenderPass&& rhs ) {
		self     = rhs.self;
		rhs.self = nullptr;
		return *this;
	}

	operator auto() {
		return self;
	}

	operator const auto() const {
		return self;
	}

	RenderPass& setSetupCallback( void* user_data, le_renderer_api::pfn_renderpass_setup_t fun ) {
		le_renderer::renderpass_i.set_setup_callback( self, user_data, fun );
		return *this;
	}

	RenderPass& setExecuteCallback( void* user_data, le_renderer_api::pfn_renderpass_execute_t fun ) {
		le_renderer::renderpass_i.set_execute_callback( self, user_data, fun );
		return *this;
	}

	// ----

	/// \brief Adds a resource as an image attachment to the renderpass.
	/// \details resource is used for ColorAttachment and Write access, unless otherwise specified.
	///          Use an le_image_attachment_info_t struct to specialise parameters, such as LOAD_OP, CLEAR_OP, and Clear/Load Color.
	RenderPass& addColorAttachment( const le_img_resource_handle&     resource_id,
	                                const le_image_attachment_info_t& attachmentInfo = le_image_attachment_info_t() ) {
		le_renderer::renderpass_i.add_color_attachment( self, resource_id, &attachmentInfo );
		return *this;
	}

	RenderPass& addDepthStencilAttachment( const le_img_resource_handle&     resource_id,
	                                       const le_image_attachment_info_t& attachmentInfo = LeDepthAttachmentInfo() ) {
		le_renderer::renderpass_i.add_depth_stencil_attachment( self, resource_id, &attachmentInfo );
		return *this;
	}

	RenderPass& useImageResource( le_img_resource_handle resource_id, le::ImageUsageFlags const& usage_flags ) {
		le_renderer::renderpass_i.use_img_resource( self, resource_id, { LeResourceType::eImage, { usage_flags } } );
		return *this;
	}

	RenderPass& useBufferResource( le_buf_resource_handle resource_id, le::BufferUsageFlags const& usage_flags ) {
		le_renderer::renderpass_i.use_buf_resource( self, resource_id, { LeResourceType::eBuffer, { usage_flags } } );
		return *this;
	}

	RenderPass& useRtxBlasResource( le_resource_handle resource_id, const LeRtxBlasUsageFlags& usage_flags = { LE_RTX_BLAS_USAGE_READ_BIT } ) {
		le_renderer::renderpass_i.use_resource( self, resource_id, { LeResourceType::eRtxBlas, { { usage_flags } } } );
		return *this;
	}

	RenderPass& useRtxTlasResource( le_resource_handle resource_id, const LeRtxTlasUsageFlags& usage_flags = { LE_RTX_TLAS_USAGE_READ_BIT } ) {
		le_renderer::renderpass_i.use_resource( self, resource_id, { LeResourceType::eRtxTlas, { { usage_flags } } } );
		return *this;
	}

	RenderPass& setIsRoot( bool isRoot = true ) {
		le_renderer::renderpass_i.set_is_root( self, isRoot );
		return *this;
	}

	RenderPass& sampleTexture( le_texture_handle textureName, const le_image_sampler_info_t& imageSamplerInfo ) {
		le_renderer::renderpass_i.sample_texture( self, textureName, &imageSamplerInfo );
		return *this;
	}

	RenderPass& sampleTexture( le_texture_handle textureName, le_img_resource_handle img_handle ) {
		return sampleTexture( textureName, le::ImageSamplerInfoBuilder( img_handle ).build() );
	}

	RenderPass& setWidth( uint32_t width ) {
		le_renderer::renderpass_i.set_width( self, width );
		return *this;
	}

	RenderPass& setHeight( uint32_t height ) {
		le_renderer::renderpass_i.set_height( self, height );
		return *this;
	}

	RenderPass& setSampleCount( le::SampleCountFlagBits const& sampleCount ) {
		le_renderer::renderpass_i.set_sample_count( self, sampleCount );
		return *this;
	}
};

// ----------------------------------------------------------------------

class RenderGraph {

	le_rendergraph_o* self;
	bool              is_reference = false;

  public:
	RenderGraph()
	    : self( le_renderer::rendergraph_i.create() ) {
	}

	RenderGraph( le_rendergraph_o* self_ )
	    : self( self_ )
	    , is_reference( true ) {
	}

	~RenderGraph() {
		if ( !is_reference ) {
			le_renderer::rendergraph_i.destroy( self );
		}
	}

	operator auto() {
		return self;
	}

	RenderGraph& addRenderPass( le_renderpass_o* renderpass ) {
		le_renderer::rendergraph_i.add_renderpass( self, renderpass );
		return *this;
	}

	RenderGraph& declareResource( le_resource_handle const& resource_id, le_resource_info_t const& info ) {
		le_renderer::rendergraph_i.declare_resource( self, resource_id, info );
		return *this;
	}
};

// ----------------------------------------------------------------------

class ImageInfoBuilder : NoCopy, NoMove {
	le_resource_info_t             res = le_renderer::helpers_i.get_default_resource_info_for_image();
	le_resource_info_t::ImageInfo& img = res.image;

  public:
	// FIXME: This method does not check that the resource_info type is actually image!
	ImageInfoBuilder( const le_resource_info_t& info )
	    : res( info ) {
	}
	ImageInfoBuilder()  = default;
	~ImageInfoBuilder() = default;

	ImageInfoBuilder& setFormat( Format format ) {
		img.format = format;
		return *this;
	}

	ImageInfoBuilder& setCreateFlags( le::ImageCreateFlags flags = 0 ) {
		img.flags = flags;
		return *this;
	}

	ImageInfoBuilder& setArrayLayers( uint32_t arrayLayers = 1 ) {
		img.arrayLayers = arrayLayers;
		return *this;
	}

	ImageInfoBuilder& setExtent( uint32_t width, uint32_t height, uint32_t depth = 1 ) {
		img.extent.width  = width;
		img.extent.height = height;
		img.extent.depth  = depth;
		return *this;
	}

	ImageInfoBuilder& setUsageFlags( le::ImageUsageFlags const& usageFlagBits ) {
		img.usage = usageFlagBits;
		return *this;
	}

	ImageInfoBuilder& addUsageFlags( le::ImageUsageFlags const& usageFlags ) {
		img.usage = img.usage | usageFlags;
		return *this;
	}

	ImageInfoBuilder& setMipLevels( uint32_t mipLevels = 1 ) {
		img.mipLevels = mipLevels;
		return *this;
	}

	ImageInfoBuilder& setImageType( le::ImageType const& imageType = le::ImageType::e2D ) {
		img.imageType = imageType;
		return *this;
	}

	ImageInfoBuilder& setImageTiling( le::ImageTiling const& imageTiling = le::ImageTiling::eOptimal ) {
		img.tiling = imageTiling;
		return *this;
	}

	const le_resource_info_t& build() {
		return res;
	}
};

// ----------------------------------------------------------------------

class BufferInfoBuilder : NoCopy, NoMove {
	le_resource_info_t              res = le_renderer::helpers_i.get_default_resource_info_for_buffer();
	le_resource_info_t::BufferInfo& buf = res.buffer;

  public:
	// FIXME: This method does not check that the resource_info type is actually buffer!
	BufferInfoBuilder( const le_resource_info_t& info )
	    : res( info ) {
	}

	BufferInfoBuilder()  = default;
	~BufferInfoBuilder() = default;

	BufferInfoBuilder& setSize( uint32_t size ) {
		buf.size = size;
		return *this;
	}

	BufferInfoBuilder& setUsageFlags( le::BufferUsageFlags const& usageFlagBits ) {
		buf.usage = usageFlagBits;
		return *this;
	}

	BufferInfoBuilder& addUsageFlags( le::BufferUsageFlags const& usageFlags ) {
		buf.usage |= usageFlags;
		return *this;
	}

	const le_resource_info_t& build() {
		return res;
	}
};

// ----------------------------------------------------------------------

class Encoder {

	le_command_buffer_encoder_o* self = nullptr;

  public:
	Encoder()  = delete;
	~Encoder() = default;

	Encoder( le_command_buffer_encoder_o* self_ )
	    : self( self_ ) {
	}

	operator auto() {
		return self;
	}

	Extent2D const& getRenderpassExtent() {
		return le_renderer::encoder_i.get_extent( self );
	}

	Encoder& bufferMemoryBarrier(
	    le::PipelineStageFlags2 const& srcStageMask,
	    le::PipelineStageFlags2 const& dstStageMask,
	    le::AccessFlags2 const&         dstAccessMask,
	    le_buf_resource_handle const&  buffer,
	    uint64_t const&                offset = 0,
	    uint64_t const&                range  = ~( 0ull ) ) {
		// todo:fill in
		le_renderer::encoder_i.buffer_memory_barrier( self, srcStageMask, dstStageMask, dstAccessMask, buffer, offset, range );
		return *this;
	}

	Encoder& dispatch( const uint32_t& groupCountX = 1, const uint32_t& groupCountY = 1, const uint32_t& groupCountZ = 1 ) {
		le_renderer::encoder_i.dispatch( self, groupCountX, groupCountY, groupCountZ );
		return *this;
	}

	Encoder& draw( const uint32_t& vertexCount, const uint32_t& instanceCount = 1, const uint32_t& firstVertex = 0, const uint32_t& firstInstance = 0 ) {
		le_renderer::encoder_i.draw( self, vertexCount, instanceCount, firstVertex, firstInstance );
		return *this;
	}

	Encoder& drawIndexed( uint32_t const& indexCount, uint32_t const& instanceCount = 1, uint32_t const& firstIndex = 0, int32_t const& vertexOffset = 0, uint32_t const& firstInstance = 0 ) {
		le_renderer::encoder_i.draw_indexed( self, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance );
		return *this;
	}

	Encoder& drawMeshTasks( const uint32_t& taskCount, const uint32_t& firstTask = 0 ) {
		le_renderer::encoder_i.draw_mesh_tasks( self, taskCount, firstTask );
		return *this;
	}

	Encoder& traceRays( uint32_t const& width, uint32_t const& height, uint32_t const& depth = 1 ) {
		le_renderer::encoder_i.trace_rays( self, width, height, depth );
		return *this;
	}

	Encoder& setLineWidth( float const& lineWidth ) {
		le_renderer::encoder_i.set_line_width( self, lineWidth );
		return *this;
	}

	Encoder& setViewports( uint32_t firstViewport, const uint32_t& viewportCount, const le::Viewport* pViewports ) {
		le_renderer::encoder_i.set_viewport( self, firstViewport, viewportCount, pViewports );
		return *this;
	}

	Encoder& setScissors( uint32_t firstScissor, const uint32_t scissorCount, const le::Rect2D* pScissors ) {
		le_renderer::encoder_i.set_scissor( self, firstScissor, scissorCount, pScissors );
		return *this;
	}

	Encoder& bindGraphicsPipeline( le_gpso_handle pipelineHandle ) {
		le_renderer::encoder_i.bind_graphics_pipeline( self, pipelineHandle );
		return *this;
	}

	Encoder& bindRtxPipeline( le_shader_binding_table_o* sbt ) {
		le_renderer::encoder_i.bind_rtx_pipeline( self, sbt );
		return *this;
	}

	Encoder& bindComputePipeline( le_cpso_handle pipelineHandle ) {
		le_renderer::encoder_i.bind_compute_pipeline( self, pipelineHandle );
		return *this;
	}

	Encoder& bindIndexBuffer( le_buf_resource_handle const& bufferId, uint64_t const& offset, IndexType const& indexType = IndexType::eUint16 ) {
		le_renderer::encoder_i.bind_index_buffer( self, bufferId, offset, indexType );
		return *this;
	}

	Encoder& bindVertexBuffers( uint32_t const& firstBinding, uint32_t const& bindingCount, le_buf_resource_handle const* pBufferId, uint64_t const* pOffsets ) {
		le_renderer::encoder_i.bind_vertex_buffers( self, firstBinding, bindingCount, pBufferId, pOffsets );
		return *this;
	}

	/// \brief Set index data directly by uploading data via GPU scratch buffer
	/// \note if either `data == nullptr`, or numBytes == 0, this method call has no effect.
	Encoder& setIndexData( void const* data, uint64_t const& numBytes, IndexType const& indexType = IndexType::eUint16, le_renderer_api::command_buffer_encoder_interface_t::buffer_binding_info_o* transient_buffer_info_readback = nullptr ) {
		le_renderer::encoder_i.set_index_data( self, data, numBytes, indexType, transient_buffer_info_readback );
		return *this;
	}

	/// \brief Set vertex data directly by uploading data via GPU scratch buffer
	/// \note if either `data == nullptr`, or numBytes == 0, this method call has no effect.
	Encoder& setVertexData( void const* data, uint64_t const& numBytes, uint32_t const& bindingIndex, le_renderer_api::command_buffer_encoder_interface_t::buffer_binding_info_o* transient_buffer_info_readback = nullptr ) {
		le_renderer::encoder_i.set_vertex_data( self, data, numBytes, bindingIndex, transient_buffer_info_readback );
		return *this;
	}

	Encoder& setPushConstantData( void const* data, uint64_t const& numBytes ) {
		le_renderer::encoder_i.set_push_constant_data( self, data, numBytes );
		return *this;
	}

	Encoder& writeToBuffer( le_buf_resource_handle const& dstBuffer, size_t const& byteOffsetDst, void const* data, size_t const& numBytes ) {
		le_renderer::encoder_i.write_to_buffer( self, dstBuffer, byteOffsetDst, data, numBytes );
		return *this;
	}

	Encoder& writeToImage( le_img_resource_handle const& dstImg, le_write_to_image_settings_t const& writeInfo, void const* data, size_t const& numBytes ) {
		le_renderer::encoder_i.write_to_image( self, dstImg, writeInfo, data, numBytes );
		return *this;
	}

	Encoder& setArgumentData( uint64_t const& argumentNameId, void const* data, size_t const& numBytes ) {
		le_renderer::encoder_i.set_argument_data( self, argumentNameId, data, numBytes );
		return *this;
	}

	Encoder& setArgumentTexture( uint64_t const& argumentName, le_texture_handle const& textureId, uint64_t const& arrayIndex = 0 ) {
		le_renderer::encoder_i.set_argument_texture( self, textureId, argumentName, arrayIndex );
		return *this;
	}

	Encoder& setArgumentImage( uint64_t const& argumentName, le_img_resource_handle const& imageId, uint64_t const& arrayIndex = 0 ) {
		le_renderer::encoder_i.set_argument_image( self, imageId, argumentName, arrayIndex );
		return *this;
	}

	Encoder& setArgumentTlas( uint64_t const& argumentName, le_tlas_resource_handle const& tlasId, uint64_t const& arrayIndex = 0 ) {
		le_renderer::encoder_i.set_argument_tlas( self, tlasId, argumentName, arrayIndex );
		return *this;
	}

	Encoder& bindArgumentBuffer( uint64_t const& argumentName, le_buf_resource_handle const& bufferId, uint64_t const& offset = 0, uint64_t const& range = ( ~0ULL ) ) {
		le_renderer::encoder_i.bind_argument_buffer( self, bufferId, argumentName, offset, range );
		return *this;
	}

	class ShaderBindingTableBuilder {
		Encoder const&             parent;
		le_shader_binding_table_o* sbt = nullptr;

	  public:
		ShaderBindingTableBuilder( Encoder const& parent_, le_rtxpso_handle pso )
		    : parent( parent_ )
		    , sbt( le_renderer::encoder_i.build_sbt( parent.self, pso ) ) {
		}

		ShaderBindingTableBuilder& setRayGenIdx( uint32_t idx ) {
			le_renderer::encoder_i.sbt_set_ray_gen( sbt, idx );
			return *this;
		}

		ShaderBindingTableBuilder& addCallableIdx( uint32_t idx ) {
			le_renderer::encoder_i.sbt_add_callable( sbt, idx );
			return *this;
		}

		ShaderBindingTableBuilder& addHitIdx( uint32_t idx ) {
			le_renderer::encoder_i.sbt_add_hit( sbt, idx );
			return *this;
		}

		ShaderBindingTableBuilder& addMissIdx( uint32_t idx ) {
			le_renderer::encoder_i.sbt_add_miss( sbt, idx );
			return *this;
		}

		ShaderBindingTableBuilder& addParameterValueU32( uint32_t val ) {
			le_renderer::encoder_i.sbt_add_u32_param( sbt, val );
			return *this;
		}

		ShaderBindingTableBuilder& addParameterValueF32( float val ) {
			le_renderer::encoder_i.sbt_add_f32_param( sbt, val );
			return *this;
		}

		le_shader_binding_table_o* build() {
			return le_renderer::encoder_i.sbt_validate( sbt );
		}
	};

	le_pipeline_manager_o* getPipelineManager() {
		return le_renderer::encoder_i.get_pipeline_manager( self );
	}
};
// ----------------------------------------------------------------------

} // namespace le
#endif // __cplusplus
#endif // GUARD_LE_RENDERER_H

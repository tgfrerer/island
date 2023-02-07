#ifndef GUARD_LE_RENDERER_H
#define GUARD_LE_RENDERER_H

#include "le_core.h"
#include "private/le_renderer/le_renderer_types.h"

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
LE_OPAQUE_HANDLE( le_swapchain_handle );

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
		void                           ( *setup                   )( le_renderer_o *obj, le_renderer_settings_t const * settings );

		void                           ( *update                  )( le_renderer_o *obj, le_rendergraph_o *rendergraph);

        le_renderer_settings_t const * ( *get_settings            )( le_renderer_o* self );

		le_backend_o*                  ( *get_backend             )( le_renderer_o* self );

	
		// note: this method must be called before setup()

		le_img_resource_handle         ( * get_swapchain_resource)( le_renderer_o* self, le_swapchain_handle swapchain);
		le_img_resource_handle         ( * get_swapchain_resource_default)( le_renderer_o* self);
		bool                           ( * get_swapchain_extent  )( le_renderer_o* self, le_swapchain_handle swapchain, uint32_t* p_width, uint32_t* p_height );
		bool                           ( * get_swapchains        )(le_renderer_o* self, size_t *num_swapchains , le_swapchain_handle* p_swapchain_handles);
		le_swapchain_handle 		   ( * add_swapchain 		 )(le_renderer_o* self, le_swapchain_settings_t const * settings);
		bool 						   ( * remove_swapchain 	 )(le_renderer_o* self, le_swapchain_handle swapchain);

		le_pipeline_manager_o*         ( *get_pipeline_manager    )( le_renderer_o* self );

        le_texture_handle              ( *produce_texture_handle  )(char const * maybe_name );
        char const *                   ( *texture_handle_get_name )(le_texture_handle handle);

        le_buf_resource_handle (*produce_buf_resource_handle)(char const * maybe_name, uint8_t flags, uint16_t index);
        le_img_resource_handle (*produce_img_resource_handle)(char const * maybe_name, uint8_t num_samples, le_img_resource_handle reference_handle, uint8_t flags);

        le_tlas_resource_handle (*produce_tlas_resource_handle)(char const * maybe_name);
        le_blas_resource_handle (*produce_blas_resource_handle)(char const * maybe_name);

		le_rtx_blas_info_handle        ( *create_rtx_blas_info ) (le_renderer_o* self, le_rtx_geometry_t* geometries, uint32_t geometries_count, le::BuildAccelerationStructureFlagsKHR const * flags);
		le_rtx_tlas_info_handle        ( *create_rtx_tlas_info ) (le_renderer_o* self, uint32_t instances_count, le::BuildAccelerationStructureFlagsKHR const* flags);

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
		void                            ( *use_resource         )( le_renderpass_o *obj, const le_resource_handle& resource_id,  le::AccessFlags2 const& access_flags);
		void                            ( *set_is_root          )( le_renderpass_o *obj, bool is_root );
		bool                            ( *get_is_root          )( const le_renderpass_o *obj);
		void                            ( *get_used_resources   )( const le_renderpass_o *obj, le_resource_handle const **pResourceIds,  le::AccessFlags2 const ** pResourcesAccess, size_t *count );
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

#endif // __cplusplus
#endif // GUARD_LE_RENDERER_H

#include "le_core.h"

#include "le_renderer.h"

#include "le_backend_vk.h" // for GPU allocators

#include <cstring>
#include <iostream>
#include <iomanip>
#include <assert.h>
#include <vector>
#include <algorithm>

#ifdef _WIN32
#	define __PRETTY_FUNCTION__ __FUNCSIG__
#endif //

#ifndef LE_MT
#	define LE_MT 0
#endif

#if ( LE_MT > 0 )
#	include "le_jobs.h"
#endif

// ----------------------------------------------------------------------
// return allocator offset based on current worker thread index.
static inline int fetch_allocator_index() {
#if ( LE_MT > 0 )
	int result = le_jobs::get_current_worker_id();
	assert( result >= 0 );
	return result;
#else
	return 0;
#endif
}

static inline le_allocator_o* fetch_allocator( le_allocator_o** ppAlloc ) {
	int             index  = fetch_allocator_index();
	le_allocator_o* result = *( ppAlloc + index );
	assert( result );
	return result;
}

// ----------------------------------------------------------------------

#define EMPLACE_CMD( x ) new ( &self->mCommandStream[ 0 ] + self->mCommandStreamSize )( x )

// ----------------------------------------------------------------------

struct le_shader_binding_table_o {

	union parameter_t {
		uint32_t u32;
		float    f32;
	};

	struct shader_record {
		uint32_t                 handle_idx; // tells us which handle to use from the pipeline's shader group data
		std::vector<parameter_t> parameters; // parameters to associate with this shader instance.
	};

	le_rtxpso_handle           pipeline;
	shader_record              ray_gen;
	std::vector<shader_record> hit;
	std::vector<shader_record> miss;
	std::vector<shader_record> callable;
	bool                       has_ray_gen;

	// stateful, used so that we can add parameters to the last shader_record
	shader_record* last_shader_record = nullptr;
};

// ----------------------------------------------------------------------

struct le_command_buffer_encoder_o {
	char                                    mCommandStream[ 4096 * 512 ]; // 512 pages of memory = 2MB
	size_t                                  mCommandStreamSize = 0;
	size_t                                  mCommandCount      = 0;
	le_allocator_o**                        ppAllocator        = nullptr; // allocator list is owned by backend, externally
	le_pipeline_manager_o*                  pipelineManager    = nullptr; // non-owning: owned by backend.
	le_staging_allocator_o*                 stagingAllocator   = nullptr; // Borrowed from backend - used for larger, permanent resources, shared amongst encoders
	le::Extent2D                            extent             = {};      // Renderpass extent, otherwise swapchain extent inferred via renderer, this may be queried by users of encoder.
	std::vector<le_shader_binding_table_o*> shader_binding_tables;        // owning
};

// ----------------------------------------------------------------------

static le_command_buffer_encoder_o* cbe_create( le_allocator_o** allocator, le_pipeline_manager_o* pipelineManager, le_staging_allocator_o* stagingAllocator, le::Extent2D const& extent = {} ) {
	auto self              = new le_command_buffer_encoder_o;
	self->ppAllocator      = allocator;
	self->pipelineManager  = pipelineManager;
	self->stagingAllocator = stagingAllocator;
	self->extent           = extent;
	return self;
};

// ----------------------------------------------------------------------

static void cbe_destroy( le_command_buffer_encoder_o* self ) {
	for ( auto sbt : self->shader_binding_tables ) {
		delete ( sbt );
	}

	delete ( self );
}

// ----------------------------------------------------------------------
// Returns extent to which this encoder has been set up to
static le::Extent2D const& cbe_get_extent( le_command_buffer_encoder_o* self ) {
	return self->extent;
}

// ----------------------------------------------------------------------

static void cbe_set_line_width( le_command_buffer_encoder_o* self, float lineWidth ) {

	auto cmd        = EMPLACE_CMD( le::CommandSetLineWidth ); // placement new into data array
	cmd->info.width = lineWidth;

	self->mCommandStreamSize += sizeof( le::CommandSetLineWidth );
	self->mCommandCount++;
}

// ----------------------------------------------------------------------

static void cbe_dispatch( le_command_buffer_encoder_o* self, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ ) {

	auto cmd  = EMPLACE_CMD( le::CommandDispatch ); // placement new!
	cmd->info = { groupCountX, groupCountY, groupCountZ, 0 };

	self->mCommandStreamSize += sizeof( le::CommandDispatch );
	self->mCommandCount++;
}

static void cbe_buffer_memory_barrier( le_command_buffer_encoder_o*   self,
                                       le::PipelineStageFlags2 const& srcStageMask,
                                       le::PipelineStageFlags2 const& dstStageMask,
                                       le::AccessFlags2 const&        dstAccessMask,
                                       le_buf_resource_handle const&  buffer,
                                       uint64_t const&                offset,
                                       uint64_t const&                range ) {

	auto cmd = EMPLACE_CMD( le::CommandBufferMemoryBarrier ); // placement new!

	cmd->info.srcStageMask  = srcStageMask;
	cmd->info.dstStageMask  = dstStageMask;
	cmd->info.dstAccessMask = dstAccessMask;
	cmd->info.buffer        = buffer;
	cmd->info.offset        = offset;
	cmd->info.range         = range;

	self->mCommandStreamSize += sizeof( le::CommandBufferMemoryBarrier );
	self->mCommandCount++;
}

// ----------------------------------------------------------------------
static void cbe_trace_rays( le_command_buffer_encoder_o* self, uint32_t width, uint32_t height, uint32_t depth ) {

	auto cmd  = EMPLACE_CMD( le::CommandTraceRays ); // placement new!
	cmd->info = { width, height, depth, 0 };

	self->mCommandStreamSize += sizeof( le::CommandTraceRays );
	self->mCommandCount++;
}
// ----------------------------------------------------------------------

static void cbe_draw( le_command_buffer_encoder_o* self,
                      uint32_t                     vertexCount,
                      uint32_t                     instanceCount,
                      uint32_t                     firstVertex,
                      uint32_t                     firstInstance ) {

	auto cmd  = EMPLACE_CMD( le::CommandDraw ); // placement new!
	cmd->info = { vertexCount, instanceCount, firstVertex, firstInstance };

	self->mCommandStreamSize += sizeof( le::CommandDraw );
	self->mCommandCount++;
}

// ----------------------------------------------------------------------

static void cbe_draw_indexed( le_command_buffer_encoder_o* self,
                              uint32_t                     indexCount,
                              uint32_t                     instanceCount,
                              uint32_t                     firstIndex,
                              int32_t                      vertexOffset,
                              uint32_t                     firstInstance ) {

	auto cmd  = EMPLACE_CMD( le::CommandDrawIndexed );
	cmd->info = {
	    indexCount,
	    instanceCount,
	    firstIndex,
	    vertexOffset,
	    firstInstance,
	    0 // padding must be set to zero
	};

	self->mCommandStreamSize += sizeof( le::CommandDrawIndexed );
	self->mCommandCount++;
}

// ----------------------------------------------------------------------
static void cbe_draw_mesh_tasks( le_command_buffer_encoder_o* self,
                                 uint32_t                     taskCount,
                                 uint32_t                     firstTask ) {

	auto cmd  = EMPLACE_CMD( le::CommandDrawMeshTasks ); // placement new!
	cmd->info = { taskCount, firstTask };

	self->mCommandStreamSize += sizeof( le::CommandDrawMeshTasks );
	self->mCommandCount++;
}
// ----------------------------------------------------------------------

static void cbe_set_viewport( le_command_buffer_encoder_o* self,
                              uint32_t                     firstViewport,
                              const uint32_t               viewportCount,
                              const le::Viewport*          pViewports ) {

	auto cmd = EMPLACE_CMD( le::CommandSetViewport ); // placement new!

	// We point data to the next available position in the data stream
	// so that we can store the data for viewports inline.
	void*  data     = ( cmd + 1 ); // note: this increments a le::CommandSetViewport pointer by one time its object size, then gets the address
	size_t dataSize = sizeof( le::Viewport ) * viewportCount;

	cmd->info = { firstViewport, viewportCount };
	cmd->header.info.size += dataSize; // we must increase the size of this command by its payload size

	if ( /* DISABLES CODE */ ( true ) ) {

		memcpy( data, pViewports, dataSize );

	} else {

		// Copy viewport data whilst flipping viewports,
		//
		// see: <https://www.saschawillems.de/blog/2019/03/29/flipping-the-vulkan-viewport/>
		// We do this instead of directly moving data, because it allows us to keep our
		// shaders to have the Y-Axis go up as it does in OpenGL, whereas otherwise
		// we would have the Vulkan default, with the Y-axis pointing down.

		auto const    pViewportsEnd   = pViewports + viewportCount;
		le::Viewport* pTargetViewport = static_cast<le::Viewport*>( data );

		for ( auto v = pViewports; v != pViewportsEnd; v++, pTargetViewport++ ) {
			*pTargetViewport = *v;
			pTargetViewport->y += pTargetViewport->height;
			pTargetViewport->height = -pTargetViewport->height;
		}
	}

	self->mCommandStreamSize += cmd->header.info.size;
	self->mCommandCount++;
};

// ----------------------------------------------------------------------

static void cbe_set_scissor( le_command_buffer_encoder_o* self,
                             uint32_t                     firstScissor,
                             const uint32_t               scissorCount,
                             le::Rect2D const*            pScissors ) {

	auto cmd = EMPLACE_CMD( le::CommandSetScissor ); // placement new!

	// We point to the next available position in the data stream
	// so that we can store the data for scissors inline.
	void*  data     = ( cmd + 1 );
	size_t dataSize = sizeof( le::Rect2D ) * scissorCount;

	cmd->info = { firstScissor, scissorCount };
	cmd->header.info.size += dataSize; // we must increase the size of this command by its payload size

	memcpy( data, pScissors, dataSize );

	self->mCommandStreamSize += cmd->header.info.size;
	self->mCommandCount++;
}

// ----------------------------------------------------------------------

static void cbe_bind_vertex_buffers( le_command_buffer_encoder_o*  self,
                                     uint32_t                      firstBinding,
                                     uint32_t                      bindingCount,
                                     le_buf_resource_handle const* pBuffers,
                                     uint64_t const*               pOffsets ) {

	// NOTE: pBuffers will hold ids for virtual buffers, we must match these
	// in the backend to actual vulkan buffer ids.
	// Buffer must be annotated whether it is transient or not

	auto cmd = EMPLACE_CMD( le::CommandBindVertexBuffers ); // placement new!

	size_t dataBuffersSize = ( sizeof( le_resource_handle ) ) * bindingCount;
	size_t dataOffsetsSize = ( sizeof( uint64_t ) ) * bindingCount;

	void* dataBuffers = ( cmd + 1 );
	void* dataOffsets = ( static_cast<char*>( dataBuffers ) + dataBuffersSize ); // start address for offset data

	cmd->info = { firstBinding, bindingCount, static_cast<le_buf_resource_handle*>( dataBuffers ), static_cast<uint64_t*>( dataOffsets ) };
	cmd->header.info.size += dataBuffersSize + dataOffsetsSize; // we must increase the size of this command by its payload size

	memcpy( dataBuffers, pBuffers, dataBuffersSize );
	memcpy( dataOffsets, pOffsets, dataOffsetsSize );

	self->mCommandStreamSize += cmd->header.info.size;
	self->mCommandCount++;
}

// ----------------------------------------------------------------------
static void cbe_bind_index_buffer( le_command_buffer_encoder_o* self,
                                   le_buf_resource_handle const buffer,
                                   uint64_t                     offset,
                                   le::IndexType const&         indexType ) {

	auto cmd = EMPLACE_CMD( le::CommandBindIndexBuffer );

	// Note: indexType==0 means uint16, indexType==1 means uint32
	cmd->info = { buffer, offset, indexType, 0 };

	self->mCommandStreamSize += cmd->header.info.size;
	self->mCommandCount++;
}

// ----------------------------------------------------------------------

static void cbe_set_vertex_data( le_command_buffer_encoder_o*                                                self,
                                 void const*                                                                 data,
                                 uint64_t                                                                    numBytes,
                                 uint32_t                                                                    bindingIndex,
                                 le_renderer_api::command_buffer_encoder_interface_t::buffer_binding_info_o* readback ) {

	// -- Allocate data on scratch buffer
	// -- Upload data via scratch allocator
	// -- Bind vertex buffers to scratch allocator

	using namespace le_backend_vk; // for le_allocator_linear_i

	if ( data == nullptr || numBytes == 0 )
		return;

	// --------| invariant: there are some bytes to set

	void*    memAddr      = nullptr;
	uint64_t bufferOffset = 0;

	le_allocator_o* allocator = fetch_allocator( self->ppAllocator );

	if ( le_allocator_linear_i.allocate( allocator, numBytes, &memAddr, &bufferOffset ) ) {

		memcpy( memAddr, data, numBytes );

		le_buf_resource_handle allocatorBufferId = le_allocator_linear_i.get_le_resource_id( allocator );

		cbe_bind_vertex_buffers( self, bindingIndex, 1, &allocatorBufferId, &bufferOffset );

		if ( readback ) {
			readback->offset   = bufferOffset;
			readback->resource = allocatorBufferId;
		}

	} else {
		std::cerr << "ERROR " << __PRETTY_FUNCTION__ << " could not allocate " << numBytes << " Bytes." << std::endl
		          << std::flush;
	}
}

// ----------------------------------------------------------------------

static void cbe_set_index_data( le_command_buffer_encoder_o*                                                self,
                                void const*                                                                 data,
                                uint64_t                                                                    numBytes,
                                le::IndexType const&                                                        indexType,
                                le_renderer_api::command_buffer_encoder_interface_t::buffer_binding_info_o* readback ) {

	using namespace le_backend_vk; // for le_allocator_linear_i

	if ( data == nullptr || numBytes == 0 )
		return;

	// --------| invariant: there are some bytes to set

	void*    memAddr;
	uint64_t bufferOffset = 0;

	le_allocator_o* allocator = fetch_allocator( self->ppAllocator );

	// -- Allocate data on scratch buffer
	if ( le_allocator_linear_i.allocate( allocator, numBytes, &memAddr, &bufferOffset ) ) {

		// -- Upload data via scratch allocator
		memcpy( memAddr, data, numBytes );

		le_buf_resource_handle allocatorBufferId = le_allocator_linear_i.get_le_resource_id( allocator );

		// -- Bind index buffer to scratch allocator
		cbe_bind_index_buffer( self, allocatorBufferId, bufferOffset, indexType );

		if ( readback ) {
			readback->offset   = bufferOffset;
			readback->resource = allocatorBufferId;
		}

	} else {
		std::cerr << "ERROR " << __PRETTY_FUNCTION__ << " could not allocate " << numBytes << " Bytes." << std::endl
		          << std::flush;
	}
}

// ----------------------------------------------------------------------

static void cbe_bind_argument_buffer( le_command_buffer_encoder_o* self, le_buf_resource_handle const bufferId, uint64_t argumentName, uint64_t offset, uint64_t range ) {

	auto cmd = EMPLACE_CMD( le::CommandBindArgumentBuffer );

	cmd->info.argument_name_id = argumentName;
	cmd->info.buffer_id        = bufferId;
	cmd->info.offset           = offset;
	cmd->info.range            = range;

	self->mCommandStreamSize += sizeof( le::CommandBindArgumentBuffer );
	self->mCommandCount++;
}

// ----------------------------------------------------------------------
static void cbe_set_argument_data( le_command_buffer_encoder_o* self,
                                   uint64_t                     argumentNameId, // hash id of argument name
                                   void const*                  data,
                                   size_t                       numBytes ) {

	using namespace le_backend_vk; // for le_allocator_linear_i

	if ( data == nullptr || numBytes == 0 )
		return;

	// --------| invariant: there are some bytes to set

	void*    memAddr;
	uint64_t bufferOffset = 0;

	le_allocator_o* allocator = fetch_allocator( self->ppAllocator );

	// -- Allocate memory on scratch buffer for ubo
	//
	// Note that we might want to have specialised ubo memory eventually if that
	// made a performance difference.
	if ( le_allocator_linear_i.allocate( allocator, numBytes, &memAddr, &bufferOffset ) ) {

		// -- Store ubo data to scratch allocator
		memcpy( memAddr, data, numBytes );

		le_buf_resource_handle allocatorBuffer = le_allocator_linear_i.get_le_resource_id( allocator );

		cbe_bind_argument_buffer( self, allocatorBuffer, argumentNameId, uint32_t( bufferOffset ), uint32_t( numBytes ) );

	} else {
		std::cerr << "ERROR " << __PRETTY_FUNCTION__ << " could not allocate " << numBytes << " Bytes." << std::endl
		          << std::flush;
		return;
	}
}

// ----------------------------------------------------------------------

static void cbe_set_argument_texture( le_command_buffer_encoder_o* self, le_texture_handle const textureId, uint64_t argumentName, uint64_t arrayIndex ) {

	auto cmd = EMPLACE_CMD( le::CommandSetArgumentTexture );

	cmd->info.argument_name_id = argumentName;
	cmd->info.texture_id       = textureId;
	cmd->info.array_index      = arrayIndex;

	self->mCommandStreamSize += sizeof( le::CommandSetArgumentTexture );
	self->mCommandCount++;
}

// ----------------------------------------------------------------------

static void cbe_set_argument_image( le_command_buffer_encoder_o* self, le_img_resource_handle const imageId, uint64_t argumentName, uint64_t arrayIndex ) {

	auto cmd = EMPLACE_CMD( le::CommandSetArgumentImage );

	cmd->info.argument_name_id = argumentName;
	cmd->info.image_id         = imageId;
	cmd->info.array_index      = arrayIndex;

	self->mCommandStreamSize += sizeof( le::CommandSetArgumentImage );
	self->mCommandCount++;
}

// ----------------------------------------------------------------------

static void cbe_set_argument_tlas( le_command_buffer_encoder_o* self, le_tlas_resource_handle const tlasId, uint64_t argumentName, uint64_t arrayIndex ) {

	auto cmd = EMPLACE_CMD( le::CommandSetArgumentTlas );

	cmd->info.argument_name_id = argumentName;
	cmd->info.tlas_id          = tlasId;
	cmd->info.array_index      = arrayIndex;

	self->mCommandStreamSize += sizeof( le::CommandSetArgumentTlas );
	self->mCommandCount++;
}

// ----------------------------------------------------------------------

static void cbe_bind_graphics_pipeline( le_command_buffer_encoder_o* self, le_gpso_handle gpsoHandle ) {

	// -- insert graphics PSO pointer into command stream
	auto cmd = EMPLACE_CMD( le::CommandBindGraphicsPipeline );

	cmd->info.gpsoHandle = gpsoHandle;

	self->mCommandStreamSize += sizeof( le::CommandBindGraphicsPipeline );
	self->mCommandCount++;
}

// ----------------------------------------------------------------------

static void cbe_bind_rtx_pipeline( le_command_buffer_encoder_o* self, le_shader_binding_table_o* sbt ) {

	// -- insert rtx PSO pointer into command stream
	auto cmd = EMPLACE_CMD( le::CommandBindRtxPipeline );

	using namespace le_backend_vk;

	// -- query pipeline for shader group data

	char* shader_group_data = nullptr;

	{
		// Store pipeline information in command buffer stream, as we don't want create pipeline in
		// backend.

		auto pipeline = le_pipeline_manager_i.produce_rtx_pipeline( self->pipelineManager, sbt->pipeline, &shader_group_data );

		cmd->info.pipeline_native_handle = pipeline.pipeline;
		cmd->info.pipeline_layout_key    = pipeline.layout_info.pipeline_layout_key;

		static_assert( sizeof( cmd->info.descriptor_set_layout_keys ) == sizeof( uint64_t ) * 8, "must be 8 * 64bit" );

		memcpy( cmd->info.descriptor_set_layout_keys, pipeline.layout_info.set_layout_keys, sizeof( cmd->info.descriptor_set_layout_keys ) );
		cmd->info.descriptor_set_layout_count = pipeline.layout_info.set_layout_count;
	}

	auto sbt_data_header = reinterpret_cast<LeShaderGroupDataHeader*>( shader_group_data );

	// Calculate memory requirements for buffer

	uint32_t ray_gen_shader_binding_offset      = 0;
	uint32_t ray_gen_shader_binding_stride      = 0;
	uint32_t ray_gen_shader_binding_byte_count  = 0;
	uint32_t ray_gen_shader_max_param_count     = 0;
	uint32_t miss_shader_binding_offset         = 0;
	uint32_t miss_shader_binding_stride         = 0;
	uint32_t miss_shader_binding_byte_count     = 0;
	uint32_t miss_shader_max_param_count        = 0;
	uint32_t hit_shader_binding_offset          = 0;
	uint32_t hit_shader_binding_stride          = 0;
	uint32_t hit_shader_binding_byte_count      = 0;
	uint32_t hit_shader_max_param_count         = 0;
	uint32_t callable_shader_binding_offset     = 0;
	uint32_t callable_shader_binding_stride     = 0;
	uint32_t callable_shader_binding_byte_count = 0;
	uint32_t callable_shader_max_param_count    = 0;

	// Returns the maximum number of parameters for a given vector of shader records
	auto max_params_fun = []( const auto& shader_vec ) -> uint32_t {
		auto compare_fun = []( const auto& lhs, const auto& rhs ) -> bool {
			return lhs.parameters.size() < rhs.parameters.size();
		};
		if ( shader_vec.empty() ) {
			return 0;
		}
		// ----------| invariant: shader vec is not empty
		return uint32_t( std::max_element( shader_vec.begin(), shader_vec.end(), compare_fun )->parameters.size() );
	};

	ray_gen_shader_max_param_count  = uint32_t( sbt->ray_gen.parameters.size() );
	miss_shader_max_param_count     = max_params_fun( sbt->miss );
	hit_shader_max_param_count      = max_params_fun( sbt->hit );
	callable_shader_max_param_count = max_params_fun( sbt->callable );

	// Stride is set per type, and must be a multiple of
	// sbt_data_header->rtx_shader_group_handle_size - the stride is the same for all
	// shaders of a type, and is based on the largest stride per type.

	auto round_up_to = []( uint32_t val, uint32_t stride ) -> uint32_t {
		assert( stride != 0 && "stride must not be zero" );
		return stride * ( ( val + stride - 1 ) / stride );
	};

	// Each shader group may have 0..n  parameters inline. Parameters must use 4 Bytes.
	//
	// The stride within a shader group buffer is uniform, we must therefore accommodate
	// for the shader group with the largest paramter count, and make this the stride.
	//
	// The stride must be a multiple of shaderGroupHandleSize.
	//
	// See Chapter 33.1, Valid Usage for `VkTraceRays()`
	// "[hit|callable|miss]ShaderBindingStride must be a multiple of ::shaderGroupHandleSize"
	//
	uint32_t group_handle_size = sbt_data_header->rtx_shader_group_handle_size;

	ray_gen_shader_binding_stride  = group_handle_size + round_up_to( ray_gen_shader_max_param_count * sizeof( uint32_t ), group_handle_size );
	miss_shader_binding_stride     = group_handle_size + round_up_to( miss_shader_max_param_count * sizeof( uint32_t ), group_handle_size );
	hit_shader_binding_stride      = group_handle_size + round_up_to( hit_shader_max_param_count * sizeof( uint32_t ), group_handle_size );
	callable_shader_binding_stride = group_handle_size + round_up_to( callable_shader_max_param_count * sizeof( uint32_t ), group_handle_size );

	// -- calculate how much data we will need in total.
	uint32_t required_byte_count = 0;
	uint32_t base_alignment      = sbt_data_header->rtx_shader_group_base_alignment;

	// Offsets for each shader group buffer must be multiples of
	// shaderGroupBaseAlignment.
	// See Chapter 33.1, Valid Usage for `VkTraceRays()`

	ray_gen_shader_binding_offset     = required_byte_count;
	ray_gen_shader_binding_byte_count = round_up_to( ray_gen_shader_binding_stride, base_alignment );
	required_byte_count += ray_gen_shader_binding_byte_count;

	miss_shader_binding_offset     = required_byte_count;
	miss_shader_binding_byte_count = round_up_to( uint32_t( miss_shader_binding_stride * sbt->miss.size() ), base_alignment );
	required_byte_count += miss_shader_binding_byte_count;

	hit_shader_binding_offset     = required_byte_count;
	hit_shader_binding_byte_count = round_up_to( uint32_t( hit_shader_binding_stride * sbt->hit.size() ), base_alignment );
	required_byte_count += hit_shader_binding_byte_count;

	callable_shader_binding_offset     = required_byte_count;
	callable_shader_binding_byte_count = round_up_to( uint32_t( callable_shader_binding_stride * sbt->callable.size() ), base_alignment );
	required_byte_count += callable_shader_binding_byte_count;

	//	std::cout << "sbt memory requirement: " << std::dec << required_byte_count << "Bytes" << std::endl
	//	          << std::flush;

	// -- allocate buffer from scratch memory

	le_allocator_o* allocator = fetch_allocator( self->ppAllocator );

	void*    memAddr          = nullptr;
	uint64_t bufferBaseOffset = 0;

	// Write shader handles and shader instance parameters to memory buffer, based on shader binding data.
	auto write_to_buffer = []( char*                                     base_addr,
	                           uint32_t                                  binding_stride,
	                           uint32_t                                  handle_size,
	                           char*                                     group_data,
	                           le_shader_binding_table_o::shader_record* shader_records,
	                           size_t                                    shader_records_count ) -> bool {
		for ( size_t i = 0; i != shader_records_count; i++ ) {

			auto& record = shader_records[ i ];

			char* record_base_address = base_addr + i * binding_stride;
			memcpy( record_base_address, group_data + record.handle_idx * handle_size, handle_size );

			assert( handle_size + record.parameters.size() * sizeof( uint32_t ) <= binding_stride );

			// store parameters for this shader, if any
			if ( !record.parameters.empty() ) {
				memcpy( record_base_address + handle_size, record.parameters.data(), record.parameters.size() * sizeof( uint32_t ) );
			}
		}
		return true;
	};

	if ( le_allocator_linear_i.allocate( allocator, required_byte_count, &memAddr, &bufferBaseOffset ) ) {

		char* base_addr = static_cast<char*>( memAddr );

		// first we must make sure that bufferOffset is a multiple of base_alignment
		assert( 0 == ( bufferBaseOffset % base_alignment ) && "buffer offset must be aligned to shader group base alignment" );

		// -- write shader binding table to renderpass scratch buffer.

		// NOTE: we increase the sbt_buffer_data to the position one past the header,
		// which is where the payload for the shader buffer data begins.
		char* shader_group_data_payload = reinterpret_cast<char*>( sbt_data_header + 1 );

		write_to_buffer( base_addr + ray_gen_shader_binding_offset, ray_gen_shader_binding_stride, group_handle_size, shader_group_data_payload, &sbt->ray_gen, 1 );
		write_to_buffer( base_addr + miss_shader_binding_offset, miss_shader_binding_stride, group_handle_size, shader_group_data_payload, sbt->miss.data(), sbt->miss.size() );
		write_to_buffer( base_addr + hit_shader_binding_offset, hit_shader_binding_stride, group_handle_size, shader_group_data_payload, sbt->hit.data(), sbt->hit.size() );
		write_to_buffer( base_addr + callable_shader_binding_offset, callable_shader_binding_stride, group_handle_size, shader_group_data_payload, sbt->callable.data(), sbt->callable.size() );

		// -- store buffer, and offsets with command info

		le_buf_resource_handle allocatorBuffer = le_allocator_linear_i.get_le_resource_id( allocator );

		cmd->info.sbt_buffer          = allocatorBuffer;
		cmd->info.ray_gen_sbt_offset  = bufferBaseOffset + ray_gen_shader_binding_offset;
		cmd->info.ray_gen_sbt_size    = ray_gen_shader_binding_byte_count;
		cmd->info.miss_sbt_offset     = bufferBaseOffset + miss_shader_binding_offset;
		cmd->info.miss_sbt_stride     = miss_shader_binding_stride;
		cmd->info.miss_sbt_size       = miss_shader_binding_byte_count;
		cmd->info.hit_sbt_offset      = bufferBaseOffset + hit_shader_binding_offset;
		cmd->info.hit_sbt_stride      = hit_shader_binding_stride;
		cmd->info.hit_sbt_size        = hit_shader_binding_byte_count;
		cmd->info.callable_sbt_offset = bufferBaseOffset + callable_shader_binding_offset;
		cmd->info.callable_sbt_stride = callable_shader_binding_stride;
		cmd->info.callable_sbt_size   = callable_shader_binding_byte_count;

	} else {
		assert( false && "Could not allocate scratch memory for rtx shader binding table." );
	}

	// -- build actual memory for shader binding table

	// TODO: build actual sbt based on data in sbt,
	// query handles from pipeline manager.
	// allocate data, point to data.

	self->mCommandStreamSize += sizeof( le::CommandBindRtxPipeline );
	self->mCommandCount++;
}

// ----------------------------------------------------------------------

static void cbe_bind_compute_pipeline( le_command_buffer_encoder_o* self, le_cpso_handle cpsoHandle ) {

	// -- insert compute PSO pointer into command stream
	auto cmd = EMPLACE_CMD( le::CommandBindComputePipeline );

	cmd->info.cpsoHandle = cpsoHandle;

	self->mCommandStreamSize += sizeof( le::CommandBindComputePipeline );
	self->mCommandCount++;
}

// ----------------------------------------------------------------------

static void cbe_write_to_buffer( le_command_buffer_encoder_o* self, le_buf_resource_handle const& dst_buffer, size_t dst_offset, void const* data, size_t numBytes ) {

	auto cmd = EMPLACE_CMD( le::CommandWriteToBuffer );

	using namespace le_backend_vk; // for le_allocator_linear_i
	void*                  memAddr;
	le_buf_resource_handle srcResourceId;

	// -- Allocate memory using staging allocator
	//
	// We don't use the encoder local scratch linear allocator, since memory written to buffers is
	// typically a lot larger than uniforms and other small settings structs. Staging memory is
	// allocated so that it is only used for TRANSFER_SRC, and shared amongst encoders so that we
	// use available memory more efficiently.
	//
	if ( le_staging_allocator_i.map( self->stagingAllocator, numBytes, &memAddr, &srcResourceId ) ) {
		// -- Write data to scratch memory now
		memcpy( memAddr, data, numBytes );

		cmd->info.src_buffer_id = srcResourceId;
		cmd->info.src_offset    = 0; // staging allocator will give us a fresh buffer, and src memory will be placed at its start
		cmd->info.dst_offset    = dst_offset;
		cmd->info.numBytes      = numBytes;
		cmd->info.dst_buffer_id = dst_buffer;
	} else {
		std::cerr << "ERROR " << __PRETTY_FUNCTION__ << " could not allocate " << numBytes << " Bytes." << std::endl
		          << std::flush;
		return;
	}

	self->mCommandStreamSize += sizeof( le::CommandWriteToBuffer );
	self->mCommandCount++;
}

// ----------------------------------------------------------------------

static void cbe_write_to_image( le_command_buffer_encoder_o*        self,
                                le_img_resource_handle const&       dst_img,
                                le_write_to_image_settings_t const& writeInfo,
                                void const*                         data,
                                size_t                              numBytes ) {

	// ----------| invariant: resource info represents an image

	auto cmd = EMPLACE_CMD( le::CommandWriteToImage );

	using namespace le_backend_vk; // for le_allocator_linear_i
	void*                  memAddr;
	le_buf_resource_handle stagingBufferId;

	// -- Allocate memory using staging allocator
	//
	// We don't use the encoder local scratch linear allocator, since memory written to buffers is
	// typically a lot larger than uniforms and other small settings structs. Staging memory is also
	// allocated so that it is only used for TRANSFER_SRC, and shared amongst encoders so that we
	// use available memory more efficiently.
	//
	if ( le_staging_allocator_i.map( self->stagingAllocator, numBytes, &memAddr, &stagingBufferId ) ) {

		// -- Write data to the freshly allocated buffer
		memcpy( memAddr, data, numBytes );

		assert( writeInfo.num_miplevels != 0 ); // number of miplevels must be at least 1.

		cmd->info.src_buffer_id   = stagingBufferId;           // resource id of staging buffer
		cmd->info.numBytes        = numBytes;                  // total number of bytes from staging buffer which need to be synchronised.
		cmd->info.dst_image_id    = dst_img;                   // resouce id for target image resource
		cmd->info.dst_miplevel    = writeInfo.dst_miplevel;    // default 0, use higher number to manually upload higher mip levels.
		cmd->info.dst_array_layer = writeInfo.dst_array_layer; // default 0, use higher number to manually upload to array layer / or mipmap face.
		cmd->info.num_miplevels   = writeInfo.num_miplevels;   // default is 1, *must not* be 0. More than 1 means to auto-generate these miplevels
		cmd->info.image_w         = writeInfo.image_w;         // image extent
		cmd->info.image_h         = writeInfo.image_h;         // image extent
		cmd->info.image_d         = writeInfo.image_d;         // image depth
		cmd->info.offset_x        = writeInfo.offset_x;        // x offset into image where to place data
		cmd->info.offset_y        = writeInfo.offset_y;        // y offset into image where to place data
		cmd->info.offset_z        = writeInfo.offset_z;        // z offset into target image

	} else {
		std::cerr << "ERROR " << __PRETTY_FUNCTION__ << " could not allocate " << numBytes << " Bytes." << std::endl
		          << std::flush;
		return;
	}
	// increase command stream size by size of command, plus size of regions attached to command.
	self->mCommandStreamSize += cmd->header.info.size;
	self->mCommandCount++;
}

// ----------------------------------------------------------------------
static void cbe_set_push_constant_data( le_command_buffer_encoder_o* self, void const* src_data, uint64_t num_bytes ) {

	auto cmd = EMPLACE_CMD( le::CommandSetPushConstantData ); // placement new!

	// We point data to the next available position in the data stream
	// so that we can store the data for push constants inline.
	void* data = ( cmd + 1 ); // one after size of command struct

	cmd->info = { num_bytes };
	cmd->header.info.size += num_bytes; // we must increase the size of this command by its payload size

	// copy data into command stream
	memcpy( data, src_data, num_bytes );

	self->mCommandStreamSize += cmd->header.info.size;
	self->mCommandCount++;
}
// ----------------------------------------------------------------------

static void cbe_build_rtx_blas( le_command_buffer_encoder_o*         self,
                                le_blas_resource_handle const* const p_blas_handles,
                                const uint32_t                       handles_count ) {

	if ( handles_count == 0 || nullptr == p_blas_handles ) {
		assert( p_blas_handles && handles_count > 0 && "must provide handles, and handles_count must be at least 1" );
		// no-op: no handles specified to be built.
		return;
	}

	auto   cmd       = EMPLACE_CMD( le::CommandBuildRtxBlas );
	void*  data      = cmd + 1;
	size_t data_size = sizeof( le_resource_handle ) * handles_count;

	cmd->info                    = {};
	cmd->info.blas_handles_count = handles_count;
	cmd->header.info.size += data_size;

	memcpy( data, p_blas_handles, data_size );

	self->mCommandStreamSize += cmd->header.info.size;
	self->mCommandCount++;
}

// ----------------------------------------------------------------------

void cbe_build_rtx_tlas( le_command_buffer_encoder_o*      self,
                         le_tlas_resource_handle const*    tlas_handle,
                         le_rtx_geometry_instance_t const* instances,
                         le_blas_resource_handle const*    blas_handles,
                         uint32_t                          instances_count ) {

	auto cmd = EMPLACE_CMD( le::CommandBuildRtxTlas );

	cmd->info                          = {};
	cmd->info.tlas_handle              = *tlas_handle;
	cmd->info.geometry_instances_count = instances_count;

	// We allocate memory from our scratch allocator, and write geometry instance data into the allocated memory.
	// since instance data contains le_resource_handles for blas instances, these need to be resolved
	// in the backend when processing the command, and patched in-place on GPU visible memory, before that memory
	// is used to build the tlas.

	// We can access that memory with confidence since that area of memory is associated with that command,
	// and there will ever only be one thread processing ever processing the command.
	//
	// Command buffer encoder writes only to that memory, then its ownership moves - together with the frame -
	// to the backend, where the backend takes over exclusive ownership of the memory.

	size_t gpu_memory_bytes_required = sizeof( le_rtx_geometry_instance_t ) * instances_count;

	le_allocator_o* allocator = fetch_allocator( self->ppAllocator );
	uint64_t        offset    = 0;

	using namespace le_backend_vk; // for le_allocator_linear_i

	if ( le_allocator_linear_i.allocate( allocator, gpu_memory_bytes_required, &cmd->info.staging_buffer_mapped_memory, &offset ) ) {

		// Store geometry instances data in GPU mapped scratch buffer - we will patch
		// blas references in the backend later, once we know how to resolve them.
		memcpy( cmd->info.staging_buffer_mapped_memory, instances, gpu_memory_bytes_required );

		cmd->info.staging_buffer_offset = uint32_t( offset );
		cmd->info.staging_buffer_id     = le_allocator_linear_i.get_le_resource_id( allocator );

	} else {
		std::cerr << "ERROR " << __PRETTY_FUNCTION__ << " could not allocate " << gpu_memory_bytes_required << " Bytes." << std::endl
		          << std::flush;
		return;
	}

	// We store the blas handles inline with the command - so that we can patch them with actual
	// VkAccelerationStructureHandles in the backend, where the names of the actual objects
	// are known.

	size_t payload_size = sizeof( le_resource_handle ) * instances_count;
	cmd->header.info.size += payload_size;

	void* memAddr = cmd + 1; // move to position just after command
	memcpy( memAddr, blas_handles, payload_size );

	self->mCommandStreamSize += cmd->header.info.size;
	self->mCommandCount++;
}

// ----------------------------------------------------------------------

static void cbe_get_encoded_data( le_command_buffer_encoder_o* self,
                                  void**                       data,
                                  size_t*                      numBytes,
                                  size_t*                      numCommands ) {

	*data        = self->mCommandStream;
	*numBytes    = self->mCommandStreamSize;
	*numCommands = self->mCommandCount;
}

// ----------------------------------------------------------------------

static le_pipeline_manager_o* cbe_get_pipeline_manager( le_command_buffer_encoder_o* self ) {
	return self->pipelineManager;
}

// ----------------------------------------------------------------------

le_shader_binding_table_o* cbe_build_shader_binding_table( le_command_buffer_encoder_o* self, le_rtxpso_handle pipeline ) {
	auto sbt      = new le_shader_binding_table_o{};
	sbt->pipeline = pipeline;
	self->shader_binding_tables.emplace_back( sbt );
	return sbt;
}

void sbt_set_ray_gen( le_shader_binding_table_o* sbt, uint32_t shader_group_idx ) {
	sbt->ray_gen.handle_idx = shader_group_idx;
	sbt->has_ray_gen        = true;
	sbt->last_shader_record = &sbt->ray_gen;
}
void sbt_add_hit( le_shader_binding_table_o* sbt, uint32_t shader_group_idx ) {
	sbt->hit.push_back( { shader_group_idx, {} } );
	sbt->last_shader_record = &sbt->hit.back();
}
void sbt_add_callable( le_shader_binding_table_o* sbt, uint32_t shader_group_idx ) {
	sbt->callable.push_back( { shader_group_idx, {} } );
	sbt->last_shader_record = &sbt->callable.back();
}
void sbt_add_miss( le_shader_binding_table_o* sbt, uint32_t shader_group_idx ) {
	sbt->miss.push_back( { shader_group_idx, {} } );
	sbt->last_shader_record = &sbt->miss.back();
}
void sbt_add_u32_param( le_shader_binding_table_o* sbt, uint32_t param ) {
	le_shader_binding_table_o::parameter_t p;
	p.u32 = param;
	sbt->last_shader_record->parameters.emplace_back( p );
}
void sbt_add_f32_param( le_shader_binding_table_o* sbt, float param ) {
	le_shader_binding_table_o::parameter_t p;
	p.f32 = param;
	sbt->last_shader_record->parameters.emplace_back( p );
}
le_shader_binding_table_o* sbt_validate( le_shader_binding_table_o* sbt ) {

	assert( sbt && "sbt must be valid handle" );
	assert( sbt->has_ray_gen && "sbt must have ray_gen shader group" );
	assert( !sbt->hit.empty() && "sbt must specify at least one hit shader group" );
	assert( !sbt->miss.empty() && "sbt must specify at least one miss shader group" );

	return sbt;
}
// ----------------------------------------------------------------------

void register_le_command_buffer_encoder_api( void* api_ ) {

	auto& cbe_i = static_cast<le_renderer_api*>( api_ )->le_command_buffer_encoder_i;

	cbe_i.create                 = cbe_create;
	cbe_i.destroy                = cbe_destroy;
	cbe_i.draw                   = cbe_draw;
	cbe_i.draw_indexed           = cbe_draw_indexed;
	cbe_i.draw_mesh_tasks        = cbe_draw_mesh_tasks;
	cbe_i.dispatch               = cbe_dispatch;
	cbe_i.buffer_memory_barrier  = cbe_buffer_memory_barrier;
	cbe_i.trace_rays             = cbe_trace_rays;
	cbe_i.get_extent             = cbe_get_extent;
	cbe_i.set_line_width         = cbe_set_line_width;
	cbe_i.set_viewport           = cbe_set_viewport;
	cbe_i.set_scissor            = cbe_set_scissor;
	cbe_i.bind_vertex_buffers    = cbe_bind_vertex_buffers;
	cbe_i.bind_index_buffer      = cbe_bind_index_buffer;
	cbe_i.set_index_data         = cbe_set_index_data;
	cbe_i.set_vertex_data        = cbe_set_vertex_data;
	cbe_i.set_argument_data      = cbe_set_argument_data;
	cbe_i.bind_argument_buffer   = cbe_bind_argument_buffer;
	cbe_i.set_argument_texture   = cbe_set_argument_texture;
	cbe_i.set_argument_image     = cbe_set_argument_image;
	cbe_i.set_argument_tlas      = cbe_set_argument_tlas;
	cbe_i.bind_graphics_pipeline = cbe_bind_graphics_pipeline;
	cbe_i.bind_compute_pipeline  = cbe_bind_compute_pipeline;
	cbe_i.bind_rtx_pipeline      = cbe_bind_rtx_pipeline;
	cbe_i.get_encoded_data       = cbe_get_encoded_data;
	cbe_i.write_to_buffer        = cbe_write_to_buffer;
	cbe_i.write_to_image         = cbe_write_to_image;
	cbe_i.set_push_constant_data = cbe_set_push_constant_data;
	cbe_i.build_rtx_blas         = cbe_build_rtx_blas;
	cbe_i.build_rtx_tlas         = cbe_build_rtx_tlas;
	cbe_i.get_pipeline_manager   = cbe_get_pipeline_manager;

	cbe_i.build_sbt         = cbe_build_shader_binding_table;
	cbe_i.sbt_set_ray_gen   = sbt_set_ray_gen;
	cbe_i.sbt_add_hit       = sbt_add_hit;
	cbe_i.sbt_add_callable  = sbt_add_callable;
	cbe_i.sbt_add_miss      = sbt_add_miss;
	cbe_i.sbt_add_u32_param = sbt_add_u32_param;
	cbe_i.sbt_add_f32_param = sbt_add_f32_param;
	cbe_i.sbt_validate      = sbt_validate;
}

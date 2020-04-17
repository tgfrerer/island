#include "le_rtx_pipeline_builder.h"
#include "le_core/le_core.h"
#include "le_renderer/private/le_renderer_types.h"
/*

Here is what we need to do to allocate memory using the vma memory allocator:

VK_NV_ray_tracing is a custom extension from Nvidia, not part of core Vulkan API and as such it's not directly supported by VMA. To use VMA to allocate memory for acceleration structure, use following steps:

    Call vkCreateAccelerationStructureKHR, get your VkAccelerationStructureKHR accelStruct.
    Call vkGetAccelerationStructureMemoryRequirementsNV, get VkMemoryRequirements2KHR memReq.
    Fill VmaAllocationCreateInfo allocCreateInfo: set memoryTypeBits = memReq.memoryTypeBits, set rest of fields to zero.
    Call vmaAllocateMemory - pass your memReq.memoryRequirements along with allocCreateInfo, get your VmaAllocation alloc and VmaAllocationInfo allocInfo.
    Call vkBindAccelerationStructureMemoryNV to bind your accelStruct to allocInfo.deviceMemory, allocInfo.offset.

This is all assuming that you do all your memory allocation, mapping, and binding on one thread. If you use multiple threads, then please note that a memory for different acceleration structures or regular buffers and images may come from a single device memory block. Binding is synchronized internally when using functions like vmaCreateBuffer or vmaBindBufferMemory, but not when you call Vulkan function directly, like vkBindAccelerationStructureMemoryNV. In that case you need to either protect any allocation/mapping/binding with a mutex on your own, or use separate custom VmaPool for your resources used on one thread, or create each such allocation as VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT.

Don't forget to destroy both acceleration structure and the allocation when no longer needed, using functions vkDestroyAccelerationStructureKHR, vmaFreeMemory respectively.

<see: https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/issues/63>

----------------------------------------------------------------------

maybe we can create our pipelines in a similar way as to how we create the graphics pipelines:

first specify all (symbolic) resources and how they interconnect so that we can fingerprint
then, in the backend, materialize symbolic resources and create an actual pipeline and associate
it with the fingerprint.

if things change in the symbolic front level, update in the backend accordingly.

----------------------------------------------------------------------

What makes the gestalt for a rtx pipeline?

+ descriptorSets[]
+ pipelinelayout made out of descriptorSets[]
+ shaderstages[]


*/

struct le_rtx_pipeline_builder_o {
	// members
};

// ----------------------------------------------------------------------

static le_rtx_pipeline_builder_o *le_rtx_pipeline_builder_create() {
	auto self = new le_rtx_pipeline_builder_o();
	return self;
}

// ----------------------------------------------------------------------

static void le_rtx_pipeline_builder_destroy( le_rtx_pipeline_builder_o *self ) {
	delete self;
}

/*
 *  
 * Set parameters for builder - so that builder can build pipeline.
 * create_acceleration_structure()
 * 
 * -- tells backend to build acceleration structure
 * -- data ownership must transfer to backend. it must stay available
 *    until upload complete / until build complete:
 * 
 * An acceleration structure is-a resource: memory for it must be allocated, 
 * it needs to be created, and destroyed, and its life-time managed by the backend.
 * 
 * Acceleration structures are built from existing buffers - we can tell
 * the backend to build acceleration structures using buffer_handles, and 
 * the backend needs to then patch the buffer. 
 * 
 * Buffers need to be uploaded first, then we can create acceleration structures.
 * 
 * Steps: upload vertex data, remember keep resource ids for buffer.
 * 
 * backend: build vk_geometries using le_rtx_geometry, patch resource ids to actual vkBuffers
 * 
 * allocate space for allocation structure before transfer pass
 * populate allocation structure in transfer pass (will need read access to buffer)
 *
 * build_acceleration_structures() -- needs to happen in backend

*/

//static void le_rtx_pipeline_builder_set_from_stage(le_rtx_pipeline_builder_o* self, le_stage_o* stage){

//}

// This gets passed into a command buffer - a command buffer for a transfer pass, so that we can keep track
// of the resource.

static void build_blas( le_rtx_geometry_t *geom, uint32_t geometry_count ) {

	// for each mesh: get primitives and record their buffer, and vertex offset and count information.
}

// ----------------------------------------------------------------------

static le_rtx_pso_handle le_rtx_pipeline_builder_build( le_rtx_pipeline_builder_o *self ) {
	// do something with self

	uint64_t value{};
	// here we need to register this pipeline state object with the backend, which will return
	// a handle to us - this handle we must forward.
	return reinterpret_cast<le_rtx_pso_handle>( value );
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_rtx_pipeline_builder, api ) {
	auto &le_rtx_pipeline_builder_i = static_cast<le_rtx_pipeline_builder_api *>( api )->le_rtx_pipeline_builder_i;

	le_rtx_pipeline_builder_i.create  = le_rtx_pipeline_builder_create;
	le_rtx_pipeline_builder_i.destroy = le_rtx_pipeline_builder_destroy;
	le_rtx_pipeline_builder_i.build   = le_rtx_pipeline_builder_build;
}

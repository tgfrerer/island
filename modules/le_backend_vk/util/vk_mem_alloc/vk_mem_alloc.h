//
// Copyright (c) 2017-2020 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#ifndef AMD_VULKAN_MEMORY_ALLOCATOR_H
#define AMD_VULKAN_MEMORY_ALLOCATOR_H

#ifdef __cplusplus
extern "C" {
#endif

/** \mainpage Vulkan Memory Allocator

<b>Version 2.3.0</b> (2019-12-04)

Copyright (c) 2017-2019 Advanced Micro Devices, Inc. All rights reserved. \n
License: MIT

Documentation of all members: vk_mem_alloc.h

\section main_table_of_contents Table of contents

- <b>User guide</b>
  - \subpage quick_start
    - [Project setup](@ref quick_start_project_setup)
    - [Initialization](@ref quick_start_initialization)
    - [Resource allocation](@ref quick_start_resource_allocation)
  - \subpage choosing_memory_type
    - [Usage](@ref choosing_memory_type_usage)
    - [Required and preferred flags](@ref choosing_memory_type_required_preferred_flags)
    - [Explicit memory types](@ref choosing_memory_type_explicit_memory_types)
    - [Custom memory pools](@ref choosing_memory_type_custom_memory_pools)
    - [Dedicated allocations](@ref choosing_memory_type_dedicated_allocations)
  - \subpage memory_mapping
    - [Mapping functions](@ref memory_mapping_mapping_functions)
    - [Persistently mapped memory](@ref memory_mapping_persistently_mapped_memory)
    - [Cache flush and invalidate](@ref memory_mapping_cache_control)
    - [Finding out if memory is mappable](@ref memory_mapping_finding_if_memory_mappable)
  - \subpage staying_within_budget
    - [Querying for budget](@ref staying_within_budget_querying_for_budget)
    - [Controlling memory usage](@ref staying_within_budget_controlling_memory_usage)
  - \subpage custom_memory_pools
    - [Choosing memory type index](@ref custom_memory_pools_MemTypeIndex)
    - [Linear allocation algorithm](@ref linear_algorithm)
      - [Free-at-once](@ref linear_algorithm_free_at_once)
      - [Stack](@ref linear_algorithm_stack)
      - [Double stack](@ref linear_algorithm_double_stack)
      - [Ring buffer](@ref linear_algorithm_ring_buffer)
    - [Buddy allocation algorithm](@ref buddy_algorithm)
  - \subpage defragmentation
  	- [Defragmenting CPU memory](@ref defragmentation_cpu)
  	- [Defragmenting GPU memory](@ref defragmentation_gpu)
  	- [Additional notes](@ref defragmentation_additional_notes)
  	- [Writing custom allocation algorithm](@ref defragmentation_custom_algorithm)
  - \subpage lost_allocations
  - \subpage statistics
    - [Numeric statistics](@ref statistics_numeric_statistics)
    - [JSON dump](@ref statistics_json_dump)
  - \subpage allocation_annotation
    - [Allocation user data](@ref allocation_user_data)
    - [Allocation names](@ref allocation_names)
  - \subpage debugging_memory_usage
    - [Memory initialization](@ref debugging_memory_usage_initialization)
    - [Margins](@ref debugging_memory_usage_margins)
    - [Corruption detection](@ref debugging_memory_usage_corruption_detection)
  - \subpage record_and_replay
- \subpage usage_patterns
  - [Common mistakes](@ref usage_patterns_common_mistakes)
  - [Simple patterns](@ref usage_patterns_simple)
  - [Advanced patterns](@ref usage_patterns_advanced)
- \subpage configuration
  - [Pointers to Vulkan functions](@ref config_Vulkan_functions)
  - [Custom host memory allocator](@ref custom_memory_allocator)
  - [Device memory allocation callbacks](@ref allocation_callbacks)
  - [Device heap memory limit](@ref heap_memory_limit)
  - \subpage vk_khr_dedicated_allocation
  - \subpage vk_amd_device_coherent_memory
- \subpage general_considerations
  - [Thread safety](@ref general_considerations_thread_safety)
  - [Validation layer warnings](@ref general_considerations_validation_layer_warnings)
  - [Allocation algorithm](@ref general_considerations_allocation_algorithm)
  - [Features not supported](@ref general_considerations_features_not_supported)

\section main_see_also See also

- [Product page on GPUOpen](https://gpuopen.com/gaming-product/vulkan-memory-allocator/)
- [Source repository on GitHub](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)




\page quick_start Quick start

\section quick_start_project_setup Project setup

Vulkan Memory Allocator comes in form of a "stb-style" single header file.
You don't need to build it as a separate library project.
You can add this file directly to your project and submit it to code repository next to your other source files.

"Single header" doesn't mean that everything is contained in C/C++ declarations,
like it tends to be in case of inline functions or C++ templates.
It means that implementation is bundled with interface in a single file and needs to be extracted using preprocessor macro.
If you don't do it properly, you will get linker errors.

To do it properly:

-# Include "vk_mem_alloc.h" file in each CPP file where you want to use the library.
   This includes declarations of all members of the library.
-# In exacly one CPP file define following macro before this include.
   It enables also internal definitions.

\code
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
\endcode

It may be a good idea to create dedicated CPP file just for this purpose.

Note on language: This library is written in C++, but has C-compatible interface.
Thus you can include and use vk_mem_alloc.h in C or C++ code, but full
implementation with `VMA_IMPLEMENTATION` macro must be compiled as C++, NOT as C.

Please note that this library includes header `<vulkan/vulkan.h>`, which in turn
includes `<windows.h>` on Windows. If you need some specific macros defined
before including these headers (like `WIN32_LEAN_AND_MEAN` or
`WINVER` for Windows, `VK_USE_PLATFORM_WIN32_KHR` for Vulkan), you must define
them before every `#include` of this library.


\section quick_start_initialization Initialization

At program startup:

-# Initialize Vulkan to have `VkPhysicalDevice` and `VkDevice` object.
-# Fill VmaAllocatorCreateInfo structure and create #VmaAllocator object by
   calling vmaCreateAllocator().

\code
VmaAllocatorCreateInfo allocatorInfo = {};
allocatorInfo.physicalDevice = physicalDevice;
allocatorInfo.device = device;

VmaAllocator allocator;
vmaCreateAllocator(&allocatorInfo, &allocator);
\endcode

\section quick_start_resource_allocation Resource allocation

When you want to create a buffer or image:

-# Fill `VkBufferCreateInfo` / `VkImageCreateInfo` structure.
-# Fill VmaAllocationCreateInfo structure.
-# Call vmaCreateBuffer() / vmaCreateImage() to get `VkBuffer`/`VkImage` with memory
   already allocated and bound to it.

\code
VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
bufferInfo.size = 65536;
bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

VmaAllocationCreateInfo allocInfo = {};
allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

VkBuffer buffer;
VmaAllocation allocation;
vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &buffer, &allocation, nullptr);
\endcode

Don't forget to destroy your objects when no longer needed:

\code
vmaDestroyBuffer(allocator, buffer, allocation);
vmaDestroyAllocator(allocator);
\endcode


\page choosing_memory_type Choosing memory type

Physical devices in Vulkan support various combinations of memory heaps and
types. Help with choosing correct and optimal memory type for your specific
resource is one of the key features of this library. You can use it by filling
appropriate members of VmaAllocationCreateInfo structure, as described below.
You can also combine multiple methods.

-# If you just want to find memory type index that meets your requirements, you
   can use function: vmaFindMemoryTypeIndex(), vmaFindMemoryTypeIndexForBufferInfo(),
   vmaFindMemoryTypeIndexForImageInfo().
-# If you want to allocate a region of device memory without association with any
   specific image or buffer, you can use function vmaAllocateMemory(). Usage of
   this function is not recommended and usually not needed.
   vmaAllocateMemoryPages() function is also provided for creating multiple allocations at once,
   which may be useful for sparse binding.
-# If you already have a buffer or an image created, you want to allocate memory
   for it and then you will bind it yourself, you can use function
   vmaAllocateMemoryForBuffer(), vmaAllocateMemoryForImage().
   For binding you should use functions: vmaBindBufferMemory(), vmaBindImageMemory()
   or their extended versions: vmaBindBufferMemory2(), vmaBindImageMemory2().
-# If you want to create a buffer or an image, allocate memory for it and bind
   them together, all in one call, you can use function vmaCreateBuffer(),
   vmaCreateImage(). This is the easiest and recommended way to use this library.

When using 3. or 4., the library internally queries Vulkan for memory types
supported for that buffer or image (function `vkGetBufferMemoryRequirements()`)
and uses only one of these types.

If no memory type can be found that meets all the requirements, these functions
return `VK_ERROR_FEATURE_NOT_PRESENT`.

You can leave VmaAllocationCreateInfo structure completely filled with zeros.
It means no requirements are specified for memory type.
It is valid, although not very useful.

\section choosing_memory_type_usage Usage

The easiest way to specify memory requirements is to fill member
VmaAllocationCreateInfo::usage using one of the values of enum #VmaMemoryUsage.
It defines high level, common usage types.
For more details, see description of this enum.

For example, if you want to create a uniform buffer that will be filled using
transfer only once or infrequently and used for rendering every frame, you can
do it using following code:

\code
VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
bufferInfo.size = 65536;
bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

VmaAllocationCreateInfo allocInfo = {};
allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

VkBuffer buffer;
VmaAllocation allocation;
vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &buffer, &allocation, nullptr);
\endcode

\section choosing_memory_type_required_preferred_flags Required and preferred flags

You can specify more detailed requirements by filling members
VmaAllocationCreateInfo::requiredFlags and VmaAllocationCreateInfo::preferredFlags
with a combination of bits from enum `VkMemoryPropertyFlags`. For example,
if you want to create a buffer that will be persistently mapped on host (so it
must be `HOST_VISIBLE`) and preferably will also be `HOST_COHERENT` and `HOST_CACHED`,
use following code:

\code
VmaAllocationCreateInfo allocInfo = {};
allocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
allocInfo.preferredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

VkBuffer buffer;
VmaAllocation allocation;
vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &buffer, &allocation, nullptr);
\endcode

A memory type is chosen that has all the required flags and as many preferred
flags set as possible.

If you use VmaAllocationCreateInfo::usage, it is just internally converted to
a set of required and preferred flags.

\section choosing_memory_type_explicit_memory_types Explicit memory types

If you inspected memory types available on the physical device and you have
a preference for memory types that you want to use, you can fill member
VmaAllocationCreateInfo::memoryTypeBits. It is a bit mask, where each bit set
means that a memory type with that index is allowed to be used for the
allocation. Special value 0, just like `UINT32_MAX`, means there are no
restrictions to memory type index.

Please note that this member is NOT just a memory type index.
Still you can use it to choose just one, specific memory type.
For example, if you already determined that your buffer should be created in
memory type 2, use following code:

\code
uint32_t memoryTypeIndex = 2;

VmaAllocationCreateInfo allocInfo = {};
allocInfo.memoryTypeBits = 1u << memoryTypeIndex;

VkBuffer buffer;
VmaAllocation allocation;
vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &buffer, &allocation, nullptr);
\endcode

\section choosing_memory_type_custom_memory_pools Custom memory pools

If you allocate from custom memory pool, all the ways of specifying memory
requirements described above are not applicable and the aforementioned members
of VmaAllocationCreateInfo structure are ignored. Memory type is selected
explicitly when creating the pool and then used to make all the allocations from
that pool. For further details, see \ref custom_memory_pools.

\section choosing_memory_type_dedicated_allocations Dedicated allocations

Memory for allocations is reserved out of larger block of `VkDeviceMemory`
allocated from Vulkan internally. That's the main feature of this whole library.
You can still request a separate memory block to be created for an allocation,
just like you would do in a trivial solution without using any allocator.
In that case, a buffer or image is always bound to that memory at offset 0.
This is called a "dedicated allocation".
You can explicitly request it by using flag #VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT.
The library can also internally decide to use dedicated allocation in some cases, e.g.:

- When the size of the allocation is large.
- When [VK_KHR_dedicated_allocation](@ref vk_khr_dedicated_allocation) extension is enabled
  and it reports that dedicated allocation is required or recommended for the resource.
- When allocation of next big memory block fails due to not enough device memory,
  but allocation with the exact requested size succeeds.


\page memory_mapping Memory mapping

To "map memory" in Vulkan means to obtain a CPU pointer to `VkDeviceMemory`,
to be able to read from it or write to it in CPU code.
Mapping is possible only of memory allocated from a memory type that has
`VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT` flag.
Functions `vkMapMemory()`, `vkUnmapMemory()` are designed for this purpose.
You can use them directly with memory allocated by this library,
but it is not recommended because of following issue:
Mapping the same `VkDeviceMemory` block multiple times is illegal - only one mapping at a time is allowed.
This includes mapping disjoint regions. Mapping is not reference-counted internally by Vulkan.
Because of this, Vulkan Memory Allocator provides following facilities:

\section memory_mapping_mapping_functions Mapping functions

The library provides following functions for mapping of a specific #VmaAllocation: vmaMapMemory(), vmaUnmapMemory().
They are safer and more convenient to use than standard Vulkan functions.
You can map an allocation multiple times simultaneously - mapping is reference-counted internally.
You can also map different allocations simultaneously regardless of whether they use the same `VkDeviceMemory` block.
The way it's implemented is that the library always maps entire memory block, not just region of the allocation.
For further details, see description of vmaMapMemory() function.
Example:

\code
// Having these objects initialized:

struct ConstantBuffer
{
    ...
};
ConstantBuffer constantBufferData;

VmaAllocator allocator;
VkBuffer constantBuffer;
VmaAllocation constantBufferAllocation;

// You can map and fill your buffer using following code:

void* mappedData;
vmaMapMemory(allocator, constantBufferAllocation, &mappedData);
memcpy(mappedData, &constantBufferData, sizeof(constantBufferData));
vmaUnmapMemory(allocator, constantBufferAllocation);
\endcode

When mapping, you may see a warning from Vulkan validation layer similar to this one:

<i>Mapping an image with layout VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL can result in undefined behavior if this memory is used by the device. Only GENERAL or PREINITIALIZED should be used.</i>

It happens because the library maps entire `VkDeviceMemory` block, where different
types of images and buffers may end up together, especially on GPUs with unified memory like Intel.
You can safely ignore it if you are sure you access only memory of the intended
object that you wanted to map.


\section memory_mapping_persistently_mapped_memory Persistently mapped memory

Kepping your memory persistently mapped is generally OK in Vulkan.
You don't need to unmap it before using its data on the GPU.
The library provides a special feature designed for that:
Allocations made with #VMA_ALLOCATION_CREATE_MAPPED_BIT flag set in
VmaAllocationCreateInfo::flags stay mapped all the time,
so you can just access CPU pointer to it any time
without a need to call any "map" or "unmap" function.
Example:

\code
VkBufferCreateInfo bufCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
bufCreateInfo.size = sizeof(ConstantBuffer);
bufCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

VmaAllocationCreateInfo allocCreateInfo = {};
allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
allocCreateInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

VkBuffer buf;
VmaAllocation alloc;
VmaAllocationInfo allocInfo;
vmaCreateBuffer(allocator, &bufCreateInfo, &allocCreateInfo, &buf, &alloc, &allocInfo);

// Buffer is already mapped. You can access its memory.
memcpy(allocInfo.pMappedData, &constantBufferData, sizeof(constantBufferData));
\endcode

There are some exceptions though, when you should consider mapping memory only for a short period of time:

- When operating system is Windows 7 or 8.x (Windows 10 is not affected because it uses WDDM2),
  device is discrete AMD GPU,
  and memory type is the special 256 MiB pool of `DEVICE_LOCAL + HOST_VISIBLE` memory
  (selected when you use #VMA_MEMORY_USAGE_CPU_TO_GPU),
  then whenever a memory block allocated from this memory type stays mapped
  for the time of any call to `vkQueueSubmit()` or `vkQueuePresentKHR()`, this
  block is migrated by WDDM to system RAM, which degrades performance. It doesn't
  matter if that particular memory block is actually used by the command buffer
  being submitted.
- On Mac/MoltenVK there is a known bug - [Issue #175](https://github.com/KhronosGroup/MoltenVK/issues/175)
  which requires unmapping before GPU can see updated texture.
- Keeping many large memory blocks mapped may impact performance or stability of some debugging tools.

\section memory_mapping_cache_control Cache flush and invalidate
  
Memory in Vulkan doesn't need to be unmapped before using it on GPU,
but unless a memory types has `VK_MEMORY_PROPERTY_HOST_COHERENT_BIT` flag set,
you need to manually **invalidate** cache before reading of mapped pointer
and **flush** cache after writing to mapped pointer.
Map/unmap operations don't do that automatically.
Vulkan provides following functions for this purpose `vkFlushMappedMemoryRanges()`,
`vkInvalidateMappedMemoryRanges()`, but this library provides more convenient
functions that refer to given allocation object: vmaFlushAllocation(),
vmaInvalidateAllocation().

Regions of memory specified for flush/invalidate must be aligned to
`VkPhysicalDeviceLimits::nonCoherentAtomSize`. This is automatically ensured by the library.
In any memory type that is `HOST_VISIBLE` but not `HOST_COHERENT`, all allocations
within blocks are aligned to this value, so their offsets are always multiply of
`nonCoherentAtomSize` and two different allocations never share same "line" of this size.

Please note that memory allocated with #VMA_MEMORY_USAGE_CPU_ONLY is guaranteed to be `HOST_COHERENT`.

Also, Windows drivers from all 3 **PC** GPU vendors (AMD, Intel, NVIDIA)
currently provide `HOST_COHERENT` flag on all memory types that are
`HOST_VISIBLE`, so on this platform you may not need to bother.

\section memory_mapping_finding_if_memory_mappable Finding out if memory is mappable

It may happen that your allocation ends up in memory that is `HOST_VISIBLE` (available for mapping)
despite it wasn't explicitly requested.
For example, application may work on integrated graphics with unified memory (like Intel) or
allocation from video memory might have failed, so the library chose system memory as fallback.

You can detect this case and map such allocation to access its memory on CPU directly,
instead of launching a transfer operation.
In order to do that: inspect `allocInfo.memoryType`, call vmaGetMemoryTypeProperties(),
and look for `VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT` flag in properties of that memory type.

\code
VkBufferCreateInfo bufCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
bufCreateInfo.size = sizeof(ConstantBuffer);
bufCreateInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

VmaAllocationCreateInfo allocCreateInfo = {};
allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
allocCreateInfo.preferredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;

VkBuffer buf;
VmaAllocation alloc;
VmaAllocationInfo allocInfo;
vmaCreateBuffer(allocator, &bufCreateInfo, &allocCreateInfo, &buf, &alloc, &allocInfo);

VkMemoryPropertyFlags memFlags;
vmaGetMemoryTypeProperties(allocator, allocInfo.memoryType, &memFlags);
if((memFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0)
{
    // Allocation ended up in mappable memory. You can map it and access it directly.
    void* mappedData;
    vmaMapMemory(allocator, alloc, &mappedData);
    memcpy(mappedData, &constantBufferData, sizeof(constantBufferData));
    vmaUnmapMemory(allocator, alloc);
}
else
{
    // Allocation ended up in non-mappable memory.
    // You need to create CPU-side buffer in VMA_MEMORY_USAGE_CPU_ONLY and make a transfer.
}
\endcode

You can even use #VMA_ALLOCATION_CREATE_MAPPED_BIT flag while creating allocations
that are not necessarily `HOST_VISIBLE` (e.g. using #VMA_MEMORY_USAGE_GPU_ONLY).
If the allocation ends up in memory type that is `HOST_VISIBLE`, it will be persistently mapped and you can use it directly.
If not, the flag is just ignored.
Example:

\code
VkBufferCreateInfo bufCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
bufCreateInfo.size = sizeof(ConstantBuffer);
bufCreateInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

VmaAllocationCreateInfo allocCreateInfo = {};
allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
allocCreateInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

VkBuffer buf;
VmaAllocation alloc;
VmaAllocationInfo allocInfo;
vmaCreateBuffer(allocator, &bufCreateInfo, &allocCreateInfo, &buf, &alloc, &allocInfo);

if(allocInfo.pUserData != nullptr)
{
    // Allocation ended up in mappable memory.
    // It's persistently mapped. You can access it directly.
    memcpy(allocInfo.pMappedData, &constantBufferData, sizeof(constantBufferData));
}
else
{
    // Allocation ended up in non-mappable memory.
    // You need to create CPU-side buffer in VMA_MEMORY_USAGE_CPU_ONLY and make a transfer.
}
\endcode


\page staying_within_budget Staying within budget

When developing a graphics-intensive game or program, it is important to avoid allocating
more GPU memory than it's physically available. When the memory is over-committed,
various bad things can happen, depending on the specific GPU, graphics driver, and
operating system:

- It may just work without any problems.
- The application may slow down because some memory blocks are moved to system RAM
  and the GPU has to access them through PCI Express bus.
- A new allocation may take very long time to complete, even few seconds, and possibly
  freeze entire system.
- The new allocation may fail with `VK_ERROR_OUT_OF_DEVICE_MEMORY`.
- It may even result in GPU crash (TDR), observed as `VK_ERROR_DEVICE_LOST`
  returned somewhere later.

\section staying_within_budget_querying_for_budget Querying for budget

To query for current memory usage and available budget, use function vmaGetBudget().
Returned structure #VmaBudget contains quantities expressed in bytes, per Vulkan memory heap.

Please note that this function returns different information and works faster than
vmaCalculateStats(). vmaGetBudget() can be called every frame or even before every
allocation, while vmaCalculateStats() is intended to be used rarely,
only to obtain statistical information, e.g. for debugging purposes.

It is recommended to use <b>VK_EXT_memory_budget</b> device extension to obtain information
about the budget from Vulkan device. VMA is able to use this extension automatically.
When not enabled, the allocator behaves same way, but then it estimates current usage
and available budget based on its internal information and Vulkan memory heap sizes,
which may be less precise. In order to use this extension:

1. Make sure extensions VK_EXT_memory_budget and VK_KHR_get_physical_device_properties2
   required by it are available and enable them. Please note that the first is a device
   extension and the second is instance extension!
2. Use flag #VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT when creating #VmaAllocator object.
3. Make sure to call vmaSetCurrentFrameIndex() every frame. Budget is queried from
   Vulkan inside of it to avoid overhead of querying it with every allocation.

\section staying_within_budget_controlling_memory_usage Controlling memory usage

There are many ways in which you can try to stay within the budget.

First, when making new allocation requires allocating a new memory block, the library
tries not to exceed the budget automatically. If a block with default recommended size
(e.g. 256 MB) would go over budget, a smaller block is allocated, possibly even
dedicated memory for just this resource.

If the size of the requested resource plus current memory usage is more than the
budget, by default the library still tries to create it, leaving it to the Vulkan
implementation whether the allocation succeeds or fails. You can change this behavior
by using #VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT flag. With it, the allocation is
not made if it would exceed the budget or if the budget is already exceeded.
Some other allocations become lost instead to make room for it, if the mechanism of
[lost allocations](@ref lost_allocations) is used.
If that is not possible, the allocation fails with `VK_ERROR_OUT_OF_DEVICE_MEMORY`.
Example usage pattern may be to pass the #VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT flag
when creating resources that are not essential for the application (e.g. the texture
of a specific object) and not to pass it when creating critically important resources
(e.g. render targets).

Finally, you can also use #VMA_ALLOCATION_CREATE_NEVER_ALLOCATE_BIT flag to make sure
a new allocation is created only when it fits inside one of the existing memory blocks.
If it would require to allocate a new block, if fails instead with `VK_ERROR_OUT_OF_DEVICE_MEMORY`.
This also ensures that the function call is very fast because it never goes to Vulkan
to obtain a new block.

Please note that creating \ref custom_memory_pools with VmaPoolCreateInfo::minBlockCount
set to more than 0 will try to allocate memory blocks without checking whether they
fit within budget.


\page custom_memory_pools Custom memory pools

A memory pool contains a number of `VkDeviceMemory` blocks.
The library automatically creates and manages default pool for each memory type available on the device.
Default memory pool automatically grows in size.
Size of allocated blocks is also variable and managed automatically.

You can create custom pool and allocate memory out of it.
It can be useful if you want to:

- Keep certain kind of allocations separate from others.
- Enforce particular, fixed size of Vulkan memory blocks.
- Limit maximum amount of Vulkan memory allocated for that pool.
- Reserve minimum or fixed amount of Vulkan memory always preallocated for that pool.

To use custom memory pools:

-# Fill VmaPoolCreateInfo structure.
-# Call vmaCreatePool() to obtain #VmaPool handle.
-# When making an allocation, set VmaAllocationCreateInfo::pool to this handle.
   You don't need to specify any other parameters of this structure, like `usage`.

Example:

\code
// Create a pool that can have at most 2 blocks, 128 MiB each.
VmaPoolCreateInfo poolCreateInfo = {};
poolCreateInfo.memoryTypeIndex = ...
poolCreateInfo.blockSize = 128ull * 1024 * 1024;
poolCreateInfo.maxBlockCount = 2;

VmaPool pool;
vmaCreatePool(allocator, &poolCreateInfo, &pool);

// Allocate a buffer out of it.
VkBufferCreateInfo bufCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
bufCreateInfo.size = 1024;
bufCreateInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

VmaAllocationCreateInfo allocCreateInfo = {};
allocCreateInfo.pool = pool;

VkBuffer buf;
VmaAllocation alloc;
VmaAllocationInfo allocInfo;
vmaCreateBuffer(allocator, &bufCreateInfo, &allocCreateInfo, &buf, &alloc, &allocInfo);
\endcode

You have to free all allocations made from this pool before destroying it.

\code
vmaDestroyBuffer(allocator, buf, alloc);
vmaDestroyPool(allocator, pool);
\endcode

\section custom_memory_pools_MemTypeIndex Choosing memory type index

When creating a pool, you must explicitly specify memory type index.
To find the one suitable for your buffers or images, you can use helper functions
vmaFindMemoryTypeIndexForBufferInfo(), vmaFindMemoryTypeIndexForImageInfo().
You need to provide structures with example parameters of buffers or images
that you are going to create in that pool.

\code
VkBufferCreateInfo exampleBufCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
exampleBufCreateInfo.size = 1024; // Whatever.
exampleBufCreateInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT; // Change if needed.

VmaAllocationCreateInfo allocCreateInfo = {};
allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY; // Change if needed.

uint32_t memTypeIndex;
vmaFindMemoryTypeIndexForBufferInfo(allocator, &exampleBufCreateInfo, &allocCreateInfo, &memTypeIndex);

VmaPoolCreateInfo poolCreateInfo = {};
poolCreateInfo.memoryTypeIndex = memTypeIndex;
// ...
\endcode

When creating buffers/images allocated in that pool, provide following parameters:

- `VkBufferCreateInfo`: Prefer to pass same parameters as above.
  Otherwise you risk creating resources in a memory type that is not suitable for them, which may result in undefined behavior.
  Using different `VK_BUFFER_USAGE_` flags may work, but you shouldn't create images in a pool intended for buffers
  or the other way around.
- VmaAllocationCreateInfo: You don't need to pass same parameters. Fill only `pool` member.
  Other members are ignored anyway.

\section linear_algorithm Linear allocation algorithm

Each Vulkan memory block managed by this library has accompanying metadata that
keeps track of used and unused regions. By default, the metadata structure and
algorithm tries to find best place for new allocations among free regions to
optimize memory usage. This way you can allocate and free objects in any order.

![Default allocation algorithm](../gfx/Linear_allocator_1_algo_default.png)

Sometimes there is a need to use simpler, linear allocation algorithm. You can
create custom pool that uses such algorithm by adding flag
#VMA_POOL_CREATE_LINEAR_ALGORITHM_BIT to VmaPoolCreateInfo::flags while creating
#VmaPool object. Then an alternative metadata management is used. It always
creates new allocations after last one and doesn't reuse free regions after
allocations freed in the middle. It results in better allocation performance and
less memory consumed by metadata.

![Linear allocation algorithm](../gfx/Linear_allocator_2_algo_linear.png)

With this one flag, you can create a custom pool that can be used in many ways:
free-at-once, stack, double stack, and ring buffer. See below for details.

\subsection linear_algorithm_free_at_once Free-at-once

In a pool that uses linear algorithm, you still need to free all the allocations
individually, e.g. by using vmaFreeMemory() or vmaDestroyBuffer(). You can free
them in any order. New allocations are always made after last one - free space
in the middle is not reused. However, when you release all the allocation and
the pool becomes empty, allocation starts from the beginning again. This way you
can use linear algorithm to speed up creation of allocations that you are going
to release all at once.

![Free-at-once](../gfx/Linear_allocator_3_free_at_once.png)

This mode is also available for pools created with VmaPoolCreateInfo::maxBlockCount
value that allows multiple memory blocks.

\subsection linear_algorithm_stack Stack

When you free an allocation that was created last, its space can be reused.
Thanks to this, if you always release allocations in the order opposite to their
creation (LIFO - Last In First Out), you can achieve behavior of a stack.

![Stack](../gfx/Linear_allocator_4_stack.png)

This mode is also available for pools created with VmaPoolCreateInfo::maxBlockCount
value that allows multiple memory blocks.

\subsection linear_algorithm_double_stack Double stack

The space reserved by a custom pool with linear algorithm may be used by two
stacks:

- First, default one, growing up from offset 0.
- Second, "upper" one, growing down from the end towards lower offsets.

To make allocation from upper stack, add flag #VMA_ALLOCATION_CREATE_UPPER_ADDRESS_BIT
to VmaAllocationCreateInfo::flags.

![Double stack](../gfx/Linear_allocator_7_double_stack.png)

Double stack is available only in pools with one memory block -
VmaPoolCreateInfo::maxBlockCount must be 1. Otherwise behavior is undefined.

When the two stacks' ends meet so there is not enough space between them for a
new allocation, such allocation fails with usual
`VK_ERROR_OUT_OF_DEVICE_MEMORY` error.

\subsection linear_algorithm_ring_buffer Ring buffer

When you free some allocations from the beginning and there is not enough free space
for a new one at the end of a pool, allocator's "cursor" wraps around to the
beginning and starts allocation there. Thanks to this, if you always release
allocations in the same order as you created them (FIFO - First In First Out),
you can achieve behavior of a ring buffer / queue.

![Ring buffer](../gfx/Linear_allocator_5_ring_buffer.png)

Pools with linear algorithm support [lost allocations](@ref lost_allocations) when used as ring buffer.
If there is not enough free space for a new allocation, but existing allocations
from the front of the queue can become lost, they become lost and the allocation
succeeds.

![Ring buffer with lost allocations](../gfx/Linear_allocator_6_ring_buffer_lost.png)

Ring buffer is available only in pools with one memory block -
VmaPoolCreateInfo::maxBlockCount must be 1. Otherwise behavior is undefined.

\section buddy_algorithm Buddy allocation algorithm

There is another allocation algorithm that can be used with custom pools, called
"buddy". Its internal data structure is based on a tree of blocks, each having
size that is a power of two and a half of its parent's size. When you want to
allocate memory of certain size, a free node in the tree is located. If it's too
large, it is recursively split into two halves (called "buddies"). However, if
requested allocation size is not a power of two, the size of a tree node is
aligned up to the nearest power of two and the remaining space is wasted. When
two buddy nodes become free, they are merged back into one larger node.

![Buddy allocator](../gfx/Buddy_allocator.png)

The advantage of buddy allocation algorithm over default algorithm is faster
allocation and deallocation, as well as smaller external fragmentation. The
disadvantage is more wasted space (internal fragmentation).

For more information, please read ["Buddy memory allocation" on Wikipedia](https://en.wikipedia.org/wiki/Buddy_memory_allocation)
or other sources that describe this concept in general.

To use buddy allocation algorithm with a custom pool, add flag
#VMA_POOL_CREATE_BUDDY_ALGORITHM_BIT to VmaPoolCreateInfo::flags while creating
#VmaPool object.

Several limitations apply to pools that use buddy algorithm:

- It is recommended to use VmaPoolCreateInfo::blockSize that is a power of two.
  Otherwise, only largest power of two smaller than the size is used for
  allocations. The remaining space always stays unused.
- [Margins](@ref debugging_memory_usage_margins) and
  [corruption detection](@ref debugging_memory_usage_corruption_detection)
  don't work in such pools.
- [Lost allocations](@ref lost_allocations) don't work in such pools. You can
  use them, but they never become lost. Support may be added in the future.
- [Defragmentation](@ref defragmentation) doesn't work with allocations made from
  such pool.

\page defragmentation Defragmentation

Interleaved allocations and deallocations of many objects of varying size can
cause fragmentation over time, which can lead to a situation where the library is unable
to find a continuous range of free memory for a new allocation despite there is
enough free space, just scattered across many small free ranges between existing
allocations.

To mitigate this problem, you can use defragmentation feature:
structure #VmaDefragmentationInfo2, function vmaDefragmentationBegin(), vmaDefragmentationEnd().
Given set of allocations, 
this function can move them to compact used memory, ensure more continuous free
space and possibly also free some `VkDeviceMemory` blocks.

What the defragmentation does is:

- Updates #VmaAllocation objects to point to new `VkDeviceMemory` and offset.
  After allocation has been moved, its VmaAllocationInfo::deviceMemory and/or
  VmaAllocationInfo::offset changes. You must query them again using
  vmaGetAllocationInfo() if you need them.
- Moves actual data in memory.

What it doesn't do, so you need to do it yourself:

- Recreate buffers and images that were bound to allocations that were defragmented and
  bind them with their new places in memory.
  You must use `vkDestroyBuffer()`, `vkDestroyImage()`,
  `vkCreateBuffer()`, `vkCreateImage()`, vmaBindBufferMemory(), vmaBindImageMemory()
  for that purpose and NOT vmaDestroyBuffer(),
  vmaDestroyImage(), vmaCreateBuffer(), vmaCreateImage(), because you don't need to
  destroy or create allocation objects!
- Recreate views and update descriptors that point to these buffers and images.

\section defragmentation_cpu Defragmenting CPU memory

Following example demonstrates how you can run defragmentation on CPU.
Only allocations created in memory types that are `HOST_VISIBLE` can be defragmented.
Others are ignored.

The way it works is:

- It temporarily maps entire memory blocks when necessary.
- It moves data using `memmove()` function.

\code
// Given following variables already initialized:
VkDevice device;
VmaAllocator allocator;
std::vector<VkBuffer> buffers;
std::vector<VmaAllocation> allocations;


const uint32_t allocCount = (uint32_t)allocations.size();
std::vector<VkBool32> allocationsChanged(allocCount);

VmaDefragmentationInfo2 defragInfo = {};
defragInfo.allocationCount = allocCount;
defragInfo.pAllocations = allocations.data();
defragInfo.pAllocationsChanged = allocationsChanged.data();
defragInfo.maxCpuBytesToMove = VK_WHOLE_SIZE; // No limit.
defragInfo.maxCpuAllocationsToMove = UINT32_MAX; // No limit.

VmaDefragmentationContext defragCtx;
vmaDefragmentationBegin(allocator, &defragInfo, nullptr, &defragCtx);
vmaDefragmentationEnd(allocator, defragCtx);

for(uint32_t i = 0; i < allocCount; ++i)
{
    if(allocationsChanged[i])
    {
        // Destroy buffer that is immutably bound to memory region which is no longer valid.
        vkDestroyBuffer(device, buffers[i], nullptr);

        // Create new buffer with same parameters.
        VkBufferCreateInfo bufferInfo = ...;
        vkCreateBuffer(device, &bufferInfo, nullptr, &buffers[i]);
            
        // You can make dummy call to vkGetBufferMemoryRequirements here to silence validation layer warning.
            
        // Bind new buffer to new memory region. Data contained in it is already moved.
        VmaAllocationInfo allocInfo;
        vmaGetAllocationInfo(allocator, allocations[i], &allocInfo);
        vmaBindBufferMemory(allocator, allocations[i], buffers[i]);
    }
}
\endcode

Setting VmaDefragmentationInfo2::pAllocationsChanged is optional.
This output array tells whether particular allocation in VmaDefragmentationInfo2::pAllocations at the same index
has been modified during defragmentation.
You can pass null, but you then need to query every allocation passed to defragmentation
for new parameters using vmaGetAllocationInfo() if you might need to recreate and rebind a buffer or image associated with it.

If you use [Custom memory pools](@ref choosing_memory_type_custom_memory_pools),
you can fill VmaDefragmentationInfo2::poolCount and VmaDefragmentationInfo2::pPools
instead of VmaDefragmentationInfo2::allocationCount and VmaDefragmentationInfo2::pAllocations
to defragment all allocations in given pools.
You cannot use VmaDefragmentationInfo2::pAllocationsChanged in that case.
You can also combine both methods.

\section defragmentation_gpu Defragmenting GPU memory

It is also possible to defragment allocations created in memory types that are not `HOST_VISIBLE`.
To do that, you need to pass a command buffer that meets requirements as described in
VmaDefragmentationInfo2::commandBuffer. The way it works is:

- It creates temporary buffers and binds them to entire memory blocks when necessary.
- It issues `vkCmdCopyBuffer()` to passed command buffer.

Example:

\code
// Given following variables already initialized:
VkDevice device;
VmaAllocator allocator;
VkCommandBuffer commandBuffer;
std::vector<VkBuffer> buffers;
std::vector<VmaAllocation> allocations;


const uint32_t allocCount = (uint32_t)allocations.size();
std::vector<VkBool32> allocationsChanged(allocCount);

VkCommandBufferBeginInfo cmdBufBeginInfo = ...;
vkBeginCommandBuffer(commandBuffer, &cmdBufBeginInfo);

VmaDefragmentationInfo2 defragInfo = {};
defragInfo.allocationCount = allocCount;
defragInfo.pAllocations = allocations.data();
defragInfo.pAllocationsChanged = allocationsChanged.data();
defragInfo.maxGpuBytesToMove = VK_WHOLE_SIZE; // Notice it's "GPU" this time.
defragInfo.maxGpuAllocationsToMove = UINT32_MAX; // Notice it's "GPU" this time.
defragInfo.commandBuffer = commandBuffer;

VmaDefragmentationContext defragCtx;
vmaDefragmentationBegin(allocator, &defragInfo, nullptr, &defragCtx);

vkEndCommandBuffer(commandBuffer);

// Submit commandBuffer.
// Wait for a fence that ensures commandBuffer execution finished.

vmaDefragmentationEnd(allocator, defragCtx);

for(uint32_t i = 0; i < allocCount; ++i)
{
    if(allocationsChanged[i])
    {
        // Destroy buffer that is immutably bound to memory region which is no longer valid.
        vkDestroyBuffer(device, buffers[i], nullptr);

        // Create new buffer with same parameters.
        VkBufferCreateInfo bufferInfo = ...;
        vkCreateBuffer(device, &bufferInfo, nullptr, &buffers[i]);
            
        // You can make dummy call to vkGetBufferMemoryRequirements here to silence validation layer warning.
            
        // Bind new buffer to new memory region. Data contained in it is already moved.
        VmaAllocationInfo allocInfo;
        vmaGetAllocationInfo(allocator, allocations[i], &allocInfo);
        vmaBindBufferMemory(allocator, allocations[i], buffers[i]);
    }
}
\endcode

You can combine these two methods by specifying non-zero `maxGpu*` as well as `maxCpu*` parameters.
The library automatically chooses best method to defragment each memory pool.

You may try not to block your entire program to wait until defragmentation finishes,
but do it in the background, as long as you carefully fullfill requirements described
in function vmaDefragmentationBegin().

\section defragmentation_additional_notes Additional notes

It is only legal to defragment allocations bound to:

- buffers
- images created with `VK_IMAGE_CREATE_ALIAS_BIT`, `VK_IMAGE_TILING_LINEAR`, and
  being currently in `VK_IMAGE_LAYOUT_GENERAL` or `VK_IMAGE_LAYOUT_PREINITIALIZED`.

Defragmentation of images created with `VK_IMAGE_TILING_OPTIMAL` or in any other
layout may give undefined results.

If you defragment allocations bound to images, new images to be bound to new
memory region after defragmentation should be created with `VK_IMAGE_LAYOUT_PREINITIALIZED`
and then transitioned to their original layout from before defragmentation if
needed using an image memory barrier.

While using defragmentation, you may experience validation layer warnings, which you just need to ignore.
See [Validation layer warnings](@ref general_considerations_validation_layer_warnings).

Please don't expect memory to be fully compacted after defragmentation.
Algorithms inside are based on some heuristics that try to maximize number of Vulkan
memory blocks to make totally empty to release them, as well as to maximimze continuous
empty space inside remaining blocks, while minimizing the number and size of allocations that
need to be moved. Some fragmentation may still remain - this is normal.

\section defragmentation_custom_algorithm Writing custom defragmentation algorithm

If you want to implement your own, custom defragmentation algorithm,
there is infrastructure prepared for that,
but it is not exposed through the library API - you need to hack its source code.
Here are steps needed to do this:

-# Main thing you need to do is to define your own class derived from base abstract
   class `VmaDefragmentationAlgorithm` and implement your version of its pure virtual methods.
   See definition and comments of this class for details.
-# Your code needs to interact with device memory block metadata.
   If you need more access to its data than it's provided by its public interface,
   declare your new class as a friend class e.g. in class `VmaBlockMetadata_Generic`.
-# If you want to create a flag that would enable your algorithm or pass some additional
   flags to configure it, add them to `VmaDefragmentationFlagBits` and use them in
   VmaDefragmentationInfo2::flags.
-# Modify function `VmaBlockVectorDefragmentationContext::Begin` to create object
   of your new class whenever needed.


\page lost_allocations Lost allocations

If your game oversubscribes video memory, if may work OK in previous-generation
graphics APIs (DirectX 9, 10, 11, OpenGL) because resources are automatically
paged to system RAM. In Vulkan you can't do it because when you run out of
memory, an allocation just fails. If you have more data (e.g. textures) that can
fit into VRAM and you don't need it all at once, you may want to upload them to
GPU on demand and "push out" ones that are not used for a long time to make room
for the new ones, effectively using VRAM (or a cartain memory pool) as a form of
cache. Vulkan Memory Allocator can help you with that by supporting a concept of
"lost allocations".

To create an allocation that can become lost, include #VMA_ALLOCATION_CREATE_CAN_BECOME_LOST_BIT
flag in VmaAllocationCreateInfo::flags. Before using a buffer or image bound to
such allocation in every new frame, you need to query it if it's not lost.
To check it, call vmaTouchAllocation().
If the allocation is lost, you should not use it or buffer/image bound to it.
You mustn't forget to destroy this allocation and this buffer/image.
vmaGetAllocationInfo() can also be used for checking status of the allocation.
Allocation is lost when returned VmaAllocationInfo::deviceMemory == `VK_NULL_HANDLE`.

To create an allocation that can make some other allocations lost to make room
for it, use #VMA_ALLOCATION_CREATE_CAN_MAKE_OTHER_LOST_BIT flag. You will
usually use both flags #VMA_ALLOCATION_CREATE_CAN_MAKE_OTHER_LOST_BIT and
#VMA_ALLOCATION_CREATE_CAN_BECOME_LOST_BIT at the same time.

Warning! Current implementation uses quite naive, brute force algorithm,
which can make allocation calls that use #VMA_ALLOCATION_CREATE_CAN_MAKE_OTHER_LOST_BIT
flag quite slow. A new, more optimal algorithm and data structure to speed this
up is planned for the future.

<b>Q: When interleaving creation of new allocations with usage of existing ones,
how do you make sure that an allocation won't become lost while it's used in the
current frame?</b>

It is ensured because vmaTouchAllocation() / vmaGetAllocationInfo() not only returns allocation
status/parameters and checks whether it's not lost, but when it's not, it also
atomically marks it as used in the current frame, which makes it impossible to
become lost in that frame. It uses lockless algorithm, so it works fast and
doesn't involve locking any internal mutex.

<b>Q: What if my allocation may still be in use by the GPU when it's rendering a
previous frame while I already submit new frame on the CPU?</b>

You can make sure that allocations "touched" by vmaTouchAllocation() / vmaGetAllocationInfo() will not
become lost for a number of additional frames back from the current one by
specifying this number as VmaAllocatorCreateInfo::frameInUseCount (for default
memory pool) and VmaPoolCreateInfo::frameInUseCount (for custom pool).

<b>Q: How do you inform the library when new frame starts?</b>

You need to call function vmaSetCurrentFrameIndex().

Example code:

\code
struct MyBuffer
{
    VkBuffer m_Buf = nullptr;
    VmaAllocation m_Alloc = nullptr;

    // Called when the buffer is really needed in the current frame.
    void EnsureBuffer();
};

void MyBuffer::EnsureBuffer()
{
    // Buffer has been created.
    if(m_Buf != VK_NULL_HANDLE)
    {
        // Check if its allocation is not lost + mark it as used in current frame.
        if(vmaTouchAllocation(allocator, m_Alloc))
        {
            // It's all OK - safe to use m_Buf.
            return;
        }
    }

    // Buffer not yet exists or lost - destroy and recreate it.

    vmaDestroyBuffer(allocator, m_Buf, m_Alloc);

    VkBufferCreateInfo bufCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufCreateInfo.size = 1024;
    bufCreateInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo allocCreateInfo = {};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    allocCreateInfo.flags = VMA_ALLOCATION_CREATE_CAN_BECOME_LOST_BIT |
        VMA_ALLOCATION_CREATE_CAN_MAKE_OTHER_LOST_BIT;

    vmaCreateBuffer(allocator, &bufCreateInfo, &allocCreateInfo, &m_Buf, &m_Alloc, nullptr);
}
\endcode

When using lost allocations, you may see some Vulkan validation layer warnings
about overlapping regions of memory bound to different kinds of buffers and
images. This is still valid as long as you implement proper handling of lost
allocations (like in the example above) and don't use them.

You can create an allocation that is already in lost state from the beginning using function
vmaCreateLostAllocation(). It may be useful if you need a "dummy" allocation that is not null.

You can call function vmaMakePoolAllocationsLost() to set all eligible allocations
in a specified custom pool to lost state.
Allocations that have been "touched" in current frame or VmaPoolCreateInfo::frameInUseCount frames back
cannot become lost.

<b>Q: Can I touch allocation that cannot become lost?</b>

Yes, although it has no visible effect.
Calls to vmaGetAllocationInfo() and vmaTouchAllocation() update last use frame index
also for allocations that cannot become lost, but the only way to observe it is to dump
internal allocator state using vmaBuildStatsString().
You can use this feature for debugging purposes to explicitly mark allocations that you use
in current frame and then analyze JSON dump to see for how long each allocation stays unused.


\page statistics Statistics

This library contains functions that return information about its internal state,
especially the amount of memory allocated from Vulkan.
Please keep in mind that these functions need to traverse all internal data structures
to gather these information, so they may be quite time-consuming.
Don't call them too often.

\section statistics_numeric_statistics Numeric statistics

You can query for overall statistics of the allocator using function vmaCalculateStats().
Information are returned using structure #VmaStats.
It contains #VmaStatInfo - number of allocated blocks, number of allocations
(occupied ranges in these blocks), number of unused (free) ranges in these blocks,
number of bytes used and unused (but still allocated from Vulkan) and other information.
They are summed across memory heaps, memory types and total for whole allocator.

You can query for statistics of a custom pool using function vmaGetPoolStats().
Information are returned using structure #VmaPoolStats.

You can query for information about specific allocation using function vmaGetAllocationInfo().
It fill structure #VmaAllocationInfo.

\section statistics_json_dump JSON dump

You can dump internal state of the allocator to a string in JSON format using function vmaBuildStatsString().
The result is guaranteed to be correct JSON.
It uses ANSI encoding.
Any strings provided by user (see [Allocation names](@ref allocation_names))
are copied as-is and properly escaped for JSON, so if they use UTF-8, ISO-8859-2 or any other encoding,
this JSON string can be treated as using this encoding.
It must be freed using function vmaFreeStatsString().

The format of this JSON string is not part of official documentation of the library,
but it will not change in backward-incompatible way without increasing library major version number
and appropriate mention in changelog.

The JSON string contains all the data that can be obtained using vmaCalculateStats().
It can also contain detailed map of allocated memory blocks and their regions -
free and occupied by allocations.
This allows e.g. to visualize the memory or assess fragmentation.


\page allocation_annotation Allocation names and user data

\section allocation_user_data Allocation user data

You can annotate allocations with your own information, e.g. for debugging purposes.
To do that, fill VmaAllocationCreateInfo::pUserData field when creating
an allocation. It's an opaque `void*` pointer. You can use it e.g. as a pointer,
some handle, index, key, ordinal number or any other value that would associate
the allocation with your custom metadata.

\code
VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
// Fill bufferInfo...

MyBufferMetadata* pMetadata = CreateBufferMetadata();

VmaAllocationCreateInfo allocCreateInfo = {};
allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
allocCreateInfo.pUserData = pMetadata;

VkBuffer buffer;
VmaAllocation allocation;
vmaCreateBuffer(allocator, &bufferInfo, &allocCreateInfo, &buffer, &allocation, nullptr);
\endcode

The pointer may be later retrieved as VmaAllocationInfo::pUserData:

\code
VmaAllocationInfo allocInfo;
vmaGetAllocationInfo(allocator, allocation, &allocInfo);
MyBufferMetadata* pMetadata = (MyBufferMetadata*)allocInfo.pUserData;
\endcode

It can also be changed using function vmaSetAllocationUserData().

Values of (non-zero) allocations' `pUserData` are printed in JSON report created by
vmaBuildStatsString(), in hexadecimal form.

\section allocation_names Allocation names

There is alternative mode available where `pUserData` pointer is used to point to
a null-terminated string, giving a name to the allocation. To use this mode,
set #VMA_ALLOCATION_CREATE_USER_DATA_COPY_STRING_BIT flag in VmaAllocationCreateInfo::flags.
Then `pUserData` passed as VmaAllocationCreateInfo::pUserData or argument to
vmaSetAllocationUserData() must be either null or pointer to a null-terminated string.
The library creates internal copy of the string, so the pointer you pass doesn't need
to be valid for whole lifetime of the allocation. You can free it after the call.

\code
VkImageCreateInfo imageInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
// Fill imageInfo...

std::string imageName = "Texture: ";
imageName += fileName;

VmaAllocationCreateInfo allocCreateInfo = {};
allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
allocCreateInfo.flags = VMA_ALLOCATION_CREATE_USER_DATA_COPY_STRING_BIT;
allocCreateInfo.pUserData = imageName.c_str();

VkImage image;
VmaAllocation allocation;
vmaCreateImage(allocator, &imageInfo, &allocCreateInfo, &image, &allocation, nullptr);
\endcode

The value of `pUserData` pointer of the allocation will be different than the one
you passed when setting allocation's name - pointing to a buffer managed
internally that holds copy of the string.

\code
VmaAllocationInfo allocInfo;
vmaGetAllocationInfo(allocator, allocation, &allocInfo);
const char* imageName = (const char*)allocInfo.pUserData;
printf("Image name: %s\n", imageName);
\endcode

That string is also printed in JSON report created by vmaBuildStatsString().

\note Passing string name to VMA allocation doesn't automatically set it to the Vulkan buffer or image created with it.
You must do it manually using an extension like VK_EXT_debug_utils, which is independent of this library.


\page debugging_memory_usage Debugging incorrect memory usage

If you suspect a bug with memory usage, like usage of uninitialized memory or
memory being overwritten out of bounds of an allocation,
you can use debug features of this library to verify this.

\section debugging_memory_usage_initialization Memory initialization

If you experience a bug with incorrect and nondeterministic data in your program and you suspect uninitialized memory to be used,
you can enable automatic memory initialization to verify this.
To do it, define macro `VMA_DEBUG_INITIALIZE_ALLOCATIONS` to 1.

\code
#define VMA_DEBUG_INITIALIZE_ALLOCATIONS 1
#include "vk_mem_alloc.h"
\endcode

It makes memory of all new allocations initialized to bit pattern `0xDCDCDCDC`.
Before an allocation is destroyed, its memory is filled with bit pattern `0xEFEFEFEF`.
Memory is automatically mapped and unmapped if necessary.

If you find these values while debugging your program, good chances are that you incorrectly
read Vulkan memory that is allocated but not initialized, or already freed, respectively.

Memory initialization works only with memory types that are `HOST_VISIBLE`.
It works also with dedicated allocations.
It doesn't work with allocations created with #VMA_ALLOCATION_CREATE_CAN_BECOME_LOST_BIT flag,
as they cannot be mapped.

\section debugging_memory_usage_margins Margins

By default, allocations are laid out in memory blocks next to each other if possible
(considering required alignment, `bufferImageGranularity`, and `nonCoherentAtomSize`).

![Allocations without margin](../gfx/Margins_1.png)

Define macro `VMA_DEBUG_MARGIN` to some non-zero value (e.g. 16) to enforce specified
number of bytes as a margin before and after every allocation.

\code
#define VMA_DEBUG_MARGIN 16
#include "vk_mem_alloc.h"
\endcode

![Allocations with margin](../gfx/Margins_2.png)

If your bug goes away after enabling margins, it means it may be caused by memory
being overwritten outside of allocation boundaries. It is not 100% certain though.
Change in application behavior may also be caused by different order and distribution
of allocations across memory blocks after margins are applied.

The margin is applied also before first and after last allocation in a block.
It may occur only once between two adjacent allocations.

Margins work with all types of memory.

Margin is applied only to allocations made out of memory blocks and not to dedicated
allocations, which have their own memory block of specific size.
It is thus not applied to allocations made using #VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT flag
or those automatically decided to put into dedicated allocations, e.g. due to its
large size or recommended by VK_KHR_dedicated_allocation extension.
Margins are also not active in custom pools created with #VMA_POOL_CREATE_BUDDY_ALGORITHM_BIT flag.

Margins appear in [JSON dump](@ref statistics_json_dump) as part of free space.

Note that enabling margins increases memory usage and fragmentation.

\section debugging_memory_usage_corruption_detection Corruption detection

You can additionally define macro `VMA_DEBUG_DETECT_CORRUPTION` to 1 to enable validation
of contents of the margins.

\code
#define VMA_DEBUG_MARGIN 16
#define VMA_DEBUG_DETECT_CORRUPTION 1
#include "vk_mem_alloc.h"
\endcode

When this feature is enabled, number of bytes specified as `VMA_DEBUG_MARGIN`
(it must be multiply of 4) before and after every allocation is filled with a magic number.
This idea is also know as "canary".
Memory is automatically mapped and unmapped if necessary.

This number is validated automatically when the allocation is destroyed.
If it's not equal to the expected value, `VMA_ASSERT()` is executed.
It clearly means that either CPU or GPU overwritten the memory outside of boundaries of the allocation,
which indicates a serious bug.

You can also explicitly request checking margins of all allocations in all memory blocks
that belong to specified memory types by using function vmaCheckCorruption(),
or in memory blocks that belong to specified custom pool, by using function 
vmaCheckPoolCorruption().

Margin validation (corruption detection) works only for memory types that are
`HOST_VISIBLE` and `HOST_COHERENT`.


\page record_and_replay Record and replay

\section record_and_replay_introduction Introduction

While using the library, sequence of calls to its functions together with their
parameters can be recorded to a file and later replayed using standalone player
application. It can be useful to:

- Test correctness - check if same sequence of calls will not cause crash or
  failures on a target platform.
- Gather statistics - see number of allocations, peak memory usage, number of
  calls etc.
- Benchmark performance - see how much time it takes to replay the whole
  sequence.

\section record_and_replay_usage Usage

Recording functionality is disabled by default.
To enable it, define following macro before every include of this library:

\code
#define VMA_RECORDING_ENABLED 1
\endcode

<b>To record sequence of calls to a file:</b> Fill in
VmaAllocatorCreateInfo::pRecordSettings member while creating #VmaAllocator
object. File is opened and written during whole lifetime of the allocator.

<b>To replay file:</b> Use VmaReplay - standalone command-line program.
Precompiled binary can be found in "bin" directory.
Its source can be found in "src/VmaReplay" directory.
Its project is generated by Premake.
Command line syntax is printed when the program is launched without parameters.
Basic usage:

    VmaReplay.exe MyRecording.csv

<b>Documentation of file format</b> can be found in file: "docs/Recording file format.md".
It's a human-readable, text file in CSV format (Comma Separated Values).

\section record_and_replay_additional_considerations Additional considerations

- Replaying file that was recorded on a different GPU (with different parameters
  like `bufferImageGranularity`, `nonCoherentAtomSize`, and especially different
  set of memory heaps and types) may give different performance and memory usage
  results, as well as issue some warnings and errors.
- Current implementation of recording in VMA, as well as VmaReplay application, is
  coded and tested only on Windows. Inclusion of recording code is driven by
  `VMA_RECORDING_ENABLED` macro. Support for other platforms should be easy to
  add. Contributions are welcomed.


\page usage_patterns Recommended usage patterns

See also slides from talk:
[Sawicki, Adam. Advanced Graphics Techniques Tutorial: Memory management in Vulkan and DX12. Game Developers Conference, 2018](https://www.gdcvault.com/play/1025458/Advanced-Graphics-Techniques-Tutorial-New)


\section usage_patterns_common_mistakes Common mistakes

<b>Use of CPU_TO_GPU instead of CPU_ONLY memory</b>

#VMA_MEMORY_USAGE_CPU_TO_GPU is recommended only for resources that will be
mapped and written by the CPU, as well as read directly by the GPU - like some
buffers or textures updated every frame (dynamic). If you create a staging copy
of a resource to be written by CPU and then used as a source of transfer to
another resource placed in the GPU memory, that staging resource should be
created with #VMA_MEMORY_USAGE_CPU_ONLY. Please read the descriptions of these
enums carefully for details.

<b>Unnecessary use of custom pools</b>

\ref custom_memory_pools may be useful for special purposes - when you want to
keep certain type of resources separate e.g. to reserve minimum amount of memory
for them, limit maximum amount of memory they can occupy, or make some of them
push out the other through the mechanism of \ref lost_allocations. For most
resources this is not needed and so it is not recommended to create #VmaPool
objects and allocations out of them. Allocating from the default pool is sufficient.

\section usage_patterns_simple Simple patterns

\subsection usage_patterns_simple_render_targets Render targets

<b>When:</b>
Any resources that you frequently write and read on GPU,
e.g. images used as color attachments (aka "render targets"), depth-stencil attachments,
images/buffers used as storage image/buffer (aka "Unordered Access View (UAV)").

<b>What to do:</b>
Create them in video memory that is fastest to access from GPU using
#VMA_MEMORY_USAGE_GPU_ONLY.

Consider using [VK_KHR_dedicated_allocation](@ref vk_khr_dedicated_allocation) extension
and/or manually creating them as dedicated allocations using #VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
especially if they are large or if you plan to destroy and recreate them e.g. when
display resolution changes.
Prefer to create such resources first and all other GPU resources (like textures and vertex buffers) later.

\subsection usage_patterns_simple_immutable_resources Immutable resources

<b>When:</b>
Any resources that you fill on CPU only once (aka "immutable") or infrequently
and then read frequently on GPU,
e.g. textures, vertex and index buffers, constant buffers that don't change often.

<b>What to do:</b>
Create them in video memory that is fastest to access from GPU using
#VMA_MEMORY_USAGE_GPU_ONLY.

To initialize content of such resource, create a CPU-side (aka "staging") copy of it
in system memory - #VMA_MEMORY_USAGE_CPU_ONLY, map it, fill it,
and submit a transfer from it to the GPU resource.
You can keep the staging copy if you need it for another upload transfer in the future.
If you don't, you can destroy it or reuse this buffer for uploading different resource
after the transfer finishes.

Prefer to create just buffers in system memory rather than images, even for uploading textures.
Use `vkCmdCopyBufferToImage()`.
Dont use images with `VK_IMAGE_TILING_LINEAR`.

\subsection usage_patterns_dynamic_resources Dynamic resources

<b>When:</b>
Any resources that change frequently (aka "dynamic"), e.g. every frame or every draw call,
written on CPU, read on GPU.

<b>What to do:</b>
Create them using #VMA_MEMORY_USAGE_CPU_TO_GPU.
You can map it and write to it directly on CPU, as well as read from it on GPU.

This is a more complex situation. Different solutions are possible,
and the best one depends on specific GPU type, but you can use this simple approach for the start.
Prefer to write to such resource sequentially (e.g. using `memcpy`).
Don't perform random access or any reads from it on CPU, as it may be very slow.

\subsection usage_patterns_readback Readback

<b>When:</b>
Resources that contain data written by GPU that you want to read back on CPU,
e.g. results of some computations.

<b>What to do:</b>
Create them using #VMA_MEMORY_USAGE_GPU_TO_CPU.
You can write to them directly on GPU, as well as map and read them on CPU.

\section usage_patterns_advanced Advanced patterns

\subsection usage_patterns_integrated_graphics Detecting integrated graphics

You can support integrated graphics (like Intel HD Graphics, AMD APU) better
by detecting it in Vulkan.
To do it, call `vkGetPhysicalDeviceProperties()`, inspect
`VkPhysicalDeviceProperties::deviceType` and look for `VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU`.
When you find it, you can assume that memory is unified and all memory types are comparably fast
to access from GPU, regardless of `VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT`.

You can then sum up sizes of all available memory heaps and treat them as useful for
your GPU resources, instead of only `DEVICE_LOCAL` ones.
You can also prefer to create your resources in memory types that are `HOST_VISIBLE` to map them
directly instead of submitting explicit transfer (see below).

\subsection usage_patterns_direct_vs_transfer Direct access versus transfer

For resources that you frequently write on CPU and read on GPU, many solutions are possible:

-# Create one copy in video memory using #VMA_MEMORY_USAGE_GPU_ONLY,
   second copy in system memory using #VMA_MEMORY_USAGE_CPU_ONLY and submit explicit tranfer each time.
-# Create just single copy using #VMA_MEMORY_USAGE_CPU_TO_GPU, map it and fill it on CPU,
   read it directly on GPU.
-# Create just single copy using #VMA_MEMORY_USAGE_CPU_ONLY, map it and fill it on CPU,
   read it directly on GPU.

Which solution is the most efficient depends on your resource and especially on the GPU.
It is best to measure it and then make the decision.
Some general recommendations:

- On integrated graphics use (2) or (3) to avoid unnecesary time and memory overhead
  related to using a second copy and making transfer.
- For small resources (e.g. constant buffers) use (2).
  Discrete AMD cards have special 256 MiB pool of video memory that is directly mappable.
  Even if the resource ends up in system memory, its data may be cached on GPU after first
  fetch over PCIe bus.
- For larger resources (e.g. textures), decide between (1) and (2).
  You may want to differentiate NVIDIA and AMD, e.g. by looking for memory type that is
  both `DEVICE_LOCAL` and `HOST_VISIBLE`. When you find it, use (2), otherwise use (1).

Similarly, for resources that you frequently write on GPU and read on CPU, multiple
solutions are possible:

-# Create one copy in video memory using #VMA_MEMORY_USAGE_GPU_ONLY,
   second copy in system memory using #VMA_MEMORY_USAGE_GPU_TO_CPU and submit explicit tranfer each time.
-# Create just single copy using #VMA_MEMORY_USAGE_GPU_TO_CPU, write to it directly on GPU,
   map it and read it on CPU.

You should take some measurements to decide which option is faster in case of your specific
resource.

If you don't want to specialize your code for specific types of GPUs, you can still make
an simple optimization for cases when your resource ends up in mappable memory to use it
directly in this case instead of creating CPU-side staging copy.
For details see [Finding out if memory is mappable](@ref memory_mapping_finding_if_memory_mappable).


\page configuration Configuration

Please check "CONFIGURATION SECTION" in the code to find macros that you can define
before each include of this file or change directly in this file to provide
your own implementation of basic facilities like assert, `min()` and `max()` functions,
mutex, atomic etc.
The library uses its own implementation of containers by default, but you can switch to using
STL containers instead.

For example, define `VMA_ASSERT(expr)` before including the library to provide
custom implementation of the assertion, compatible with your project.
By default it is defined to standard C `assert(expr)` in `_DEBUG` configuration
and empty otherwise.

\section config_Vulkan_functions Pointers to Vulkan functions

The library uses Vulkan functions straight from the `vulkan.h` header by default.
If you want to provide your own pointers to these functions, e.g. fetched using
`vkGetInstanceProcAddr()` and `vkGetDeviceProcAddr()`:

-# Define `VMA_STATIC_VULKAN_FUNCTIONS 0`.
-# Provide valid pointers through VmaAllocatorCreateInfo::pVulkanFunctions.

\section custom_memory_allocator Custom host memory allocator

If you use custom allocator for CPU memory rather than default operator `new`
and `delete` from C++, you can make this library using your allocator as well
by filling optional member VmaAllocatorCreateInfo::pAllocationCallbacks. These
functions will be passed to Vulkan, as well as used by the library itself to
make any CPU-side allocations.

\section allocation_callbacks Device memory allocation callbacks

The library makes calls to `vkAllocateMemory()` and `vkFreeMemory()` internally.
You can setup callbacks to be informed about these calls, e.g. for the purpose
of gathering some statistics. To do it, fill optional member
VmaAllocatorCreateInfo::pDeviceMemoryCallbacks.

\section heap_memory_limit Device heap memory limit

When device memory of certain heap runs out of free space, new allocations may
fail (returning error code) or they may succeed, silently pushing some existing
memory blocks from GPU VRAM to system RAM (which degrades performance). This
behavior is implementation-dependant - it depends on GPU vendor and graphics
driver.

On AMD cards it can be controlled while creating Vulkan device object by using
VK_AMD_memory_overallocation_behavior extension, if available.

Alternatively, if you want to test how your program behaves with limited amount of Vulkan device
memory available without switching your graphics card to one that really has
smaller VRAM, you can use a feature of this library intended for this purpose.
To do it, fill optional member VmaAllocatorCreateInfo::pHeapSizeLimit.



\page vk_khr_dedicated_allocation VK_KHR_dedicated_allocation

VK_KHR_dedicated_allocation is a Vulkan extension which can be used to improve
performance on some GPUs. It augments Vulkan API with possibility to query
driver whether it prefers particular buffer or image to have its own, dedicated
allocation (separate `VkDeviceMemory` block) for better efficiency - to be able
to do some internal optimizations.

The extension is supported by this library. It will be used automatically when
enabled. To enable it:

1 . When creating Vulkan device, check if following 2 device extensions are
supported (call `vkEnumerateDeviceExtensionProperties()`).
If yes, enable them (fill `VkDeviceCreateInfo::ppEnabledExtensionNames`).

- VK_KHR_get_memory_requirements2
- VK_KHR_dedicated_allocation

If you enabled these extensions:

2 . Use #VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT flag when creating
your #VmaAllocator`to inform the library that you enabled required extensions
and you want the library to use them.

\code
allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT;

vmaCreateAllocator(&allocatorInfo, &allocator);
\endcode

That's all. The extension will be automatically used whenever you create a
buffer using vmaCreateBuffer() or image using vmaCreateImage().

When using the extension together with Vulkan Validation Layer, you will receive
warnings like this:

    vkBindBufferMemory(): Binding memory to buffer 0x33 but vkGetBufferMemoryRequirements() has not been called on that buffer.

It is OK, you should just ignore it. It happens because you use function
`vkGetBufferMemoryRequirements2KHR()` instead of standard
`vkGetBufferMemoryRequirements()`, while the validation layer seems to be
unaware of it.

To learn more about this extension, see:

- [VK_KHR_dedicated_allocation in Vulkan specification](https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/chap44.html#VK_KHR_dedicated_allocation)
- [VK_KHR_dedicated_allocation unofficial manual](http://asawicki.info/articles/VK_KHR_dedicated_allocation.php5)



\page vk_amd_device_coherent_memory VK_AMD_device_coherent_memory

VK_AMD_device_coherent_memory is a device extension that enables access to
additional memory types with `VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD` and
`VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD` flag. It is useful mostly for
allocation of buffers intended for writing "breadcrumb markers" in between passes
or draw calls, which in turn are useful for debugging GPU crash/hang/TDR cases.

When the extension is available but has not been enabled, Vulkan physical device
still exposes those memory types, but their usage is forbidden. VMA automatically
takes care of that - it returns `VK_ERROR_FEATURE_NOT_PRESENT` when an attempt
to allocate memory of such type is made.

If you want to use this extension in connection with VMA, follow these steps:

\section vk_amd_device_coherent_memory_initialization Initialization

1) Call `vkEnumerateDeviceExtensionProperties` for the physical device.
Check if the extension is supported - if returned array of `VkExtensionProperties` contains "VK_AMD_device_coherent_memory".

2) Call `vkGetPhysicalDeviceFeatures2` for the physical device instead of old `vkGetPhysicalDeviceFeatures`.
Attach additional structure `VkPhysicalDeviceCoherentMemoryFeaturesAMD` to `VkPhysicalDeviceFeatures2::pNext` to be returned.
Check if the device feature is really supported - check if `VkPhysicalDeviceCoherentMemoryFeaturesAMD::deviceCoherentMemory` is true.

3) While creating device with `vkCreateDevice`, enable this extension - add "VK_AMD_device_coherent_memory"
to the list passed as `VkDeviceCreateInfo::ppEnabledExtensionNames`.

4) While creating the device, also don't set `VkDeviceCreateInfo::pEnabledFeatures`.
Fill in `VkPhysicalDeviceFeatures2` structure instead and pass it as `VkDeviceCreateInfo::pNext`.
Enable this device feature - attach additional structure `VkPhysicalDeviceCoherentMemoryFeaturesAMD` to
`VkPhysicalDeviceFeatures2::pNext` and set its member `deviceCoherentMemory` to `VK_TRUE`.

5) While creating #VmaAllocator with vmaCreateAllocator() inform VMA that you
have enabled this extension and feature - add #VMA_ALLOCATOR_CREATE_AMD_DEVICE_COHERENT_MEMORY_BIT
to VmaAllocatorCreateInfo::flags.

\section vk_amd_device_coherent_memory_usage Usage

After following steps described above, you can create VMA allocations and custom pools
out of the special `DEVICE_COHERENT` and `DEVICE_UNCACHED` memory types on eligible
devices. There are multiple ways to do it, for example:

- You can request or prefer to allocate out of such memory types by adding
  `VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD` to VmaAllocationCreateInfo::requiredFlags
  or VmaAllocationCreateInfo::preferredFlags. Those flags can be freely mixed with
  other ways of \ref choosing_memory_type, like setting VmaAllocationCreateInfo::usage.
- If you manually found memory type index to use for this purpose, force allocation
  from this specific index by setting VmaAllocationCreateInfo::memoryTypeBits `= 1u << index`.

\section vk_amd_device_coherent_memory_more_information More information

To learn more about this extension, see [VK_AMD_device_coherent_memory in Vulkan specification](https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/chap44.html#VK_AMD_device_coherent_memory)

Example use of this extension can be found in the code of the sample and test suite
accompanying this library.


\page general_considerations General considerations

\section general_considerations_thread_safety Thread safety

- The library has no global state, so separate #VmaAllocator objects can be used
  independently.
  There should be no need to create multiple such objects though - one per `VkDevice` is enough.
- By default, all calls to functions that take #VmaAllocator as first parameter
  are safe to call from multiple threads simultaneously because they are
  synchronized internally when needed.
- When the allocator is created with #VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT
  flag, calls to functions that take such #VmaAllocator object must be
  synchronized externally.
- Access to a #VmaAllocation object must be externally synchronized. For example,
  you must not call vmaGetAllocationInfo() and vmaMapMemory() from different
  threads at the same time if you pass the same #VmaAllocation object to these
  functions.

\section general_considerations_validation_layer_warnings Validation layer warnings

When using this library, you can meet following types of warnings issued by
Vulkan validation layer. They don't necessarily indicate a bug, so you may need
to just ignore them.

- *vkBindBufferMemory(): Binding memory to buffer 0xeb8e4 but vkGetBufferMemoryRequirements() has not been called on that buffer.*
  - It happens when VK_KHR_dedicated_allocation extension is enabled.
    `vkGetBufferMemoryRequirements2KHR` function is used instead, while validation layer seems to be unaware of it.
- *Mapping an image with layout VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL can result in undefined behavior if this memory is used by the device. Only GENERAL or PREINITIALIZED should be used.*
  - It happens when you map a buffer or image, because the library maps entire
    `VkDeviceMemory` block, where different types of images and buffers may end
    up together, especially on GPUs with unified memory like Intel.
- *Non-linear image 0xebc91 is aliased with linear buffer 0xeb8e4 which may indicate a bug.*
  - It happens when you use lost allocations, and a new image or buffer is
    created in place of an existing object that bacame lost.
  - It may happen also when you use [defragmentation](@ref defragmentation).

\section general_considerations_allocation_algorithm Allocation algorithm

The library uses following algorithm for allocation, in order:

-# Try to find free range of memory in existing blocks.
-# If failed, try to create a new block of `VkDeviceMemory`, with preferred block size.
-# If failed, try to create such block with size/2, size/4, size/8.
-# If failed and #VMA_ALLOCATION_CREATE_CAN_MAKE_OTHER_LOST_BIT flag was
   specified, try to find space in existing blocks, possilby making some other
   allocations lost.
-# If failed, try to allocate separate `VkDeviceMemory` for this allocation,
   just like when you use #VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT.
-# If failed, choose other memory type that meets the requirements specified in
   VmaAllocationCreateInfo and go to point 1.
-# If failed, return `VK_ERROR_OUT_OF_DEVICE_MEMORY`.

\section general_considerations_features_not_supported Features not supported

Features deliberately excluded from the scope of this library:

- Data transfer. Uploading (straming) and downloading data of buffers and images
  between CPU and GPU memory and related synchronization is responsibility of the user.
  Defining some "texture" object that would automatically stream its data from a
  staging copy in CPU memory to GPU memory would rather be a feature of another,
  higher-level library implemented on top of VMA.
- Allocations for imported/exported external memory. They tend to require
  explicit memory type index and dedicated allocation anyway, so they don't
  interact with main features of this library. Such special purpose allocations
  should be made manually, using `vkCreateBuffer()` and `vkAllocateMemory()`.
- Recreation of buffers and images. Although the library has functions for
  buffer and image creation (vmaCreateBuffer(), vmaCreateImage()), you need to
  recreate these objects yourself after defragmentation. That's because the big
  structures `VkBufferCreateInfo`, `VkImageCreateInfo` are not stored in
  #VmaAllocation object.
- Handling CPU memory allocation failures. When dynamically creating small C++
  objects in CPU memory (not Vulkan memory), allocation failures are not checked
  and handled gracefully, because that would complicate code significantly and
  is usually not needed in desktop PC applications anyway.
- Code free of any compiler warnings. Maintaining the library to compile and
  work correctly on so many different platforms is hard enough. Being free of 
  any warnings, on any version of any compiler, is simply not feasible.
- This is a C++ library with C interface.
  Bindings or ports to any other programming languages are welcomed as external projects and
  are not going to be included into this repository.

*/

/*
Define this macro to 0/1 to disable/enable support for recording functionality,
available through VmaAllocatorCreateInfo::pRecordSettings.
*/
#ifndef VMA_RECORDING_ENABLED
#	define VMA_RECORDING_ENABLED 0
#endif

#ifndef NOMINMAX
#	define NOMINMAX // For windows.h
#endif

#ifndef VULKAN_H_
#	include <vulkan/vulkan.h>
#endif

#if VMA_RECORDING_ENABLED
#	include <windows.h>
#endif

// Define this macro to declare maximum supported Vulkan version in format AAABBBCCC,
// where AAA = major, BBB = minor, CCC = patch.
// If you want to use version > 1.0, it still needs to be enabled via VmaAllocatorCreateInfo::vulkanApiVersion.
#if !defined( VMA_VULKAN_VERSION )
#	if defined( VK_VERSION_1_1 )
#		define VMA_VULKAN_VERSION 1001000
#	else
#		define VMA_VULKAN_VERSION 1000000
#	endif
#endif

#if !defined( VMA_DEDICATED_ALLOCATION )
#	if VK_KHR_get_memory_requirements2 && VK_KHR_dedicated_allocation
#		define VMA_DEDICATED_ALLOCATION 1
#	else
#		define VMA_DEDICATED_ALLOCATION 0
#	endif
#endif

#if !defined( VMA_BIND_MEMORY2 )
#	if VK_KHR_bind_memory2
#		define VMA_BIND_MEMORY2 1
#	else
#		define VMA_BIND_MEMORY2 0
#	endif
#endif

#if !defined( VMA_MEMORY_BUDGET )
#	if VK_EXT_memory_budget && ( VK_KHR_get_physical_device_properties2 || VMA_VULKAN_VERSION >= 1001000 )
#		define VMA_MEMORY_BUDGET 1
#	else
#		define VMA_MEMORY_BUDGET 0
#	endif
#endif

// Define these macros to decorate all public functions with additional code,
// before and after returned type, appropriately. This may be useful for
// exporing the functions when compiling VMA as a separate library. Example:
// #define VMA_CALL_PRE  __declspec(dllexport)
// #define VMA_CALL_POST __cdecl
#ifndef VMA_CALL_PRE
#	define VMA_CALL_PRE
#endif
#ifndef VMA_CALL_POST
#	define VMA_CALL_POST
#endif

/** \struct VmaAllocator
\brief Represents main object of this library initialized.

Fill structure #VmaAllocatorCreateInfo and call function vmaCreateAllocator() to create it.
Call function vmaDestroyAllocator() to destroy it.

It is recommended to create just one object of this type per `VkDevice` object,
right after Vulkan is initialized and keep it alive until before Vulkan device is destroyed.
*/
VK_DEFINE_HANDLE( VmaAllocator )

/// Callback function called after successful vkAllocateMemory.
typedef void( VKAPI_PTR *PFN_vmaAllocateDeviceMemoryFunction )(
    VmaAllocator   allocator,
    uint32_t       memoryType,
    VkDeviceMemory memory,
    VkDeviceSize   size );
/// Callback function called before vkFreeMemory.
typedef void( VKAPI_PTR *PFN_vmaFreeDeviceMemoryFunction )(
    VmaAllocator   allocator,
    uint32_t       memoryType,
    VkDeviceMemory memory,
    VkDeviceSize   size );

/** \brief Set of callbacks that the library will call for `vkAllocateMemory` and `vkFreeMemory`.

Provided for informative purpose, e.g. to gather statistics about number of
allocations or total amount of memory allocated in Vulkan.

Used in VmaAllocatorCreateInfo::pDeviceMemoryCallbacks.
*/
typedef struct VmaDeviceMemoryCallbacks {
	/// Optional, can be null.
	PFN_vmaAllocateDeviceMemoryFunction pfnAllocate;
	/// Optional, can be null.
	PFN_vmaFreeDeviceMemoryFunction pfnFree;
} VmaDeviceMemoryCallbacks;

/// Flags for created #VmaAllocator.
typedef enum VmaAllocatorCreateFlagBits {
	/** \brief Allocator and all objects created from it will not be synchronized internally, so you must guarantee they are used from only one thread at a time or synchronized externally by you.

    Using this flag may increase performance because internal mutexes are not used.
    */
	VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT = 0x00000001,
	/** \brief Enables usage of VK_KHR_dedicated_allocation extension.

    The flag works only if VmaAllocatorCreateInfo::vulkanApiVersion `== VK_API_VERSION_1_0`.
    When it's `VK_API_VERSION_1_1`, the flag is ignored because the extension has been promoted to Vulkan 1.1.

    Using this extenion will automatically allocate dedicated blocks of memory for
    some buffers and images instead of suballocating place for them out of bigger
    memory blocks (as if you explicitly used #VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT
    flag) when it is recommended by the driver. It may improve performance on some
    GPUs.

    You may set this flag only if you found out that following device extensions are
    supported, you enabled them while creating Vulkan device passed as
    VmaAllocatorCreateInfo::device, and you want them to be used internally by this
    library:

    - VK_KHR_get_memory_requirements2 (device extension)
    - VK_KHR_dedicated_allocation (device extension)

    When this flag is set, you can experience following warnings reported by Vulkan
    validation layer. You can ignore them.

    > vkBindBufferMemory(): Binding memory to buffer 0x2d but vkGetBufferMemoryRequirements() has not been called on that buffer.
    */
	VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT = 0x00000002,
	/**
    Enables usage of VK_KHR_bind_memory2 extension.

    The flag works only if VmaAllocatorCreateInfo::vulkanApiVersion `== VK_API_VERSION_1_0`.
    When it's `VK_API_VERSION_1_1`, the flag is ignored because the extension has been promoted to Vulkan 1.1.

    You may set this flag only if you found out that this device extension is supported,
    you enabled it while creating Vulkan device passed as VmaAllocatorCreateInfo::device,
    and you want it to be used internally by this library.

    The extension provides functions `vkBindBufferMemory2KHR` and `vkBindImageMemory2KHR`,
    which allow to pass a chain of `pNext` structures while binding.
    This flag is required if you use `pNext` parameter in vmaBindBufferMemory2() or vmaBindImageMemory2().
    */
	VMA_ALLOCATOR_CREATE_KHR_BIND_MEMORY2_BIT = 0x00000004,
	/**
    Enables usage of VK_EXT_memory_budget extension.

    You may set this flag only if you found out that this device extension is supported,
    you enabled it while creating Vulkan device passed as VmaAllocatorCreateInfo::device,
    and you want it to be used internally by this library, along with another instance extension
    VK_KHR_get_physical_device_properties2, which is required by it (or Vulkan 1.1, where this extension is promoted).

    The extension provides query for current memory usage and budget, which will probably
    be more accurate than an estimation used by the library otherwise.
    */
	VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT = 0x00000008,
	/**
    Enabled usage of VK_AMD_device_coherent_memory extension.
    
    You may set this flag only if you:

    - found out that this device extension is supported and enabled it while creating Vulkan device passed as VmaAllocatorCreateInfo::device,
    - checked that `VkPhysicalDeviceCoherentMemoryFeaturesAMD::deviceCoherentMemory` is true and set it while creating the Vulkan device,
    - want it to be used internally by this library.

    The extension and accompanying device feature provide access to memory types with
    `VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD` and `VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD` flags.
    They are useful mostly for writing breadcrumb markers - a common method for debugging GPU crash/hang/TDR.
    
    When the extension is not enabled, such memory types are still enumerated, but their usage is illegal.
    To protect from this error, if you don't create the allocator with this flag, it will refuse to allocate any memory or create a custom pool in such memory type,
    returning `VK_ERROR_FEATURE_NOT_PRESENT`.
    */
	VMA_ALLOCATOR_CREATE_AMD_DEVICE_COHERENT_MEMORY_BIT = 0x00000010,

	VMA_ALLOCATOR_CREATE_FLAG_BITS_MAX_ENUM = 0x7FFFFFFF
} VmaAllocatorCreateFlagBits;
typedef VkFlags VmaAllocatorCreateFlags;

/** \brief Pointers to some Vulkan functions - a subset used by the library.

Used in VmaAllocatorCreateInfo::pVulkanFunctions.
*/
typedef struct VmaVulkanFunctions {
	PFN_vkGetPhysicalDeviceProperties       vkGetPhysicalDeviceProperties;
	PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties;
	PFN_vkAllocateMemory                    vkAllocateMemory;
	PFN_vkFreeMemory                        vkFreeMemory;
	PFN_vkMapMemory                         vkMapMemory;
	PFN_vkUnmapMemory                       vkUnmapMemory;
	PFN_vkFlushMappedMemoryRanges           vkFlushMappedMemoryRanges;
	PFN_vkInvalidateMappedMemoryRanges      vkInvalidateMappedMemoryRanges;
	PFN_vkBindBufferMemory                  vkBindBufferMemory;
	PFN_vkBindImageMemory                   vkBindImageMemory;
	PFN_vkGetBufferMemoryRequirements       vkGetBufferMemoryRequirements;
	PFN_vkGetImageMemoryRequirements        vkGetImageMemoryRequirements;
	PFN_vkCreateBuffer                      vkCreateBuffer;
	PFN_vkDestroyBuffer                     vkDestroyBuffer;
	PFN_vkCreateImage                       vkCreateImage;
	PFN_vkDestroyImage                      vkDestroyImage;
	PFN_vkCmdCopyBuffer                     vkCmdCopyBuffer;
#if VMA_DEDICATED_ALLOCATION || VMA_VULKAN_VERSION >= 1001000
	PFN_vkGetBufferMemoryRequirements2KHR vkGetBufferMemoryRequirements2KHR;
	PFN_vkGetImageMemoryRequirements2KHR  vkGetImageMemoryRequirements2KHR;
#endif
#if VMA_BIND_MEMORY2 || VMA_VULKAN_VERSION >= 1001000
	PFN_vkBindBufferMemory2KHR vkBindBufferMemory2KHR;
	PFN_vkBindImageMemory2KHR  vkBindImageMemory2KHR;
#endif
#if VMA_MEMORY_BUDGET || VMA_VULKAN_VERSION >= 1001000
	PFN_vkGetPhysicalDeviceMemoryProperties2KHR vkGetPhysicalDeviceMemoryProperties2KHR;
#endif
} VmaVulkanFunctions;

/// Flags to be used in VmaRecordSettings::flags.
typedef enum VmaRecordFlagBits {
	/** \brief Enables flush after recording every function call.

    Enable it if you expect your application to crash, which may leave recording file truncated.
    It may degrade performance though.
    */
	VMA_RECORD_FLUSH_AFTER_CALL_BIT = 0x00000001,

	VMA_RECORD_FLAG_BITS_MAX_ENUM = 0x7FFFFFFF
} VmaRecordFlagBits;
typedef VkFlags VmaRecordFlags;

/// Parameters for recording calls to VMA functions. To be used in VmaAllocatorCreateInfo::pRecordSettings.
typedef struct VmaRecordSettings {
	/// Flags for recording. Use #VmaRecordFlagBits enum.
	VmaRecordFlags flags;
	/** \brief Path to the file that should be written by the recording.

    Suggested extension: "csv".
    If the file already exists, it will be overwritten.
    It will be opened for the whole time #VmaAllocator object is alive.
    If opening this file fails, creation of the whole allocator object fails.
    */
	const char *pFilePath;
} VmaRecordSettings;

/// Description of a Allocator to be created.
typedef struct VmaAllocatorCreateInfo {
	/// Flags for created allocator. Use #VmaAllocatorCreateFlagBits enum.
	VmaAllocatorCreateFlags flags;
	/// Vulkan physical device.
	/** It must be valid throughout whole lifetime of created allocator. */
	VkPhysicalDevice physicalDevice;
	/// Vulkan device.
	/** It must be valid throughout whole lifetime of created allocator. */
	VkDevice device;
	/// Preferred size of a single `VkDeviceMemory` block to be allocated from large heaps > 1 GiB. Optional.
	/** Set to 0 to use default, which is currently 256 MiB. */
	VkDeviceSize preferredLargeHeapBlockSize;
	/// Custom CPU memory allocation callbacks. Optional.
	/** Optional, can be null. When specified, will also be used for all CPU-side memory allocations. */
	const VkAllocationCallbacks *pAllocationCallbacks;
	/// Informative callbacks for `vkAllocateMemory`, `vkFreeMemory`. Optional.
	/** Optional, can be null. */
	const VmaDeviceMemoryCallbacks *pDeviceMemoryCallbacks;
	/** \brief Maximum number of additional frames that are in use at the same time as current frame.

    This value is used only when you make allocations with
    VMA_ALLOCATION_CREATE_CAN_BECOME_LOST_BIT flag. Such allocation cannot become
    lost if allocation.lastUseFrameIndex >= allocator.currentFrameIndex - frameInUseCount.

    For example, if you double-buffer your command buffers, so resources used for
    rendering in previous frame may still be in use by the GPU at the moment you
    allocate resources needed for the current frame, set this value to 1.

    If you want to allow any allocations other than used in the current frame to
    become lost, set this value to 0.
    */
	uint32_t frameInUseCount;
	/** \brief Either null or a pointer to an array of limits on maximum number of bytes that can be allocated out of particular Vulkan memory heap.

    If not NULL, it must be a pointer to an array of
    `VkPhysicalDeviceMemoryProperties::memoryHeapCount` elements, defining limit on
    maximum number of bytes that can be allocated out of particular Vulkan memory
    heap.

    Any of the elements may be equal to `VK_WHOLE_SIZE`, which means no limit on that
    heap. This is also the default in case of `pHeapSizeLimit` = NULL.

    If there is a limit defined for a heap:

    - If user tries to allocate more memory from that heap using this allocator,
      the allocation fails with `VK_ERROR_OUT_OF_DEVICE_MEMORY`.
    - If the limit is smaller than heap size reported in `VkMemoryHeap::size`, the
      value of this limit will be reported instead when using vmaGetMemoryProperties().

    Warning! Using this feature may not be equivalent to installing a GPU with
    smaller amount of memory, because graphics driver doesn't necessary fail new
    allocations with `VK_ERROR_OUT_OF_DEVICE_MEMORY` result when memory capacity is
    exceeded. It may return success and just silently migrate some device memory
    blocks to system RAM. This driver behavior can also be controlled using
    VK_AMD_memory_overallocation_behavior extension.
    */
	const VkDeviceSize *pHeapSizeLimit;
	/** \brief Pointers to Vulkan functions. Can be null if you leave define `VMA_STATIC_VULKAN_FUNCTIONS 1`.

    If you leave define `VMA_STATIC_VULKAN_FUNCTIONS 1` in configuration section,
    you can pass null as this member, because the library will fetch pointers to
    Vulkan functions internally in a static way, like:

        vulkanFunctions.vkAllocateMemory = &vkAllocateMemory;

    Fill this member if you want to provide your own pointers to Vulkan functions,
    e.g. fetched using `vkGetInstanceProcAddr()` and `vkGetDeviceProcAddr()`.
    */
	const VmaVulkanFunctions *pVulkanFunctions;
	/** \brief Parameters for recording of VMA calls. Can be null.

    If not null, it enables recording of calls to VMA functions to a file.
    If support for recording is not enabled using `VMA_RECORDING_ENABLED` macro,
    creation of the allocator object fails with `VK_ERROR_FEATURE_NOT_PRESENT`.
    */
	const VmaRecordSettings *pRecordSettings;
	/** \brief Optional handle to Vulkan instance object.

    Optional, can be null. Must be set if #VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT flas is used
    or if `vulkanApiVersion >= VK_MAKE_VERSION(1, 1, 0)`.
    */
	VkInstance instance;
	/** \brief Optional. The highest version of Vulkan that the application is designed to use.
    
    It must be a value in the format as created by macro `VK_MAKE_VERSION` or a constant like: `VK_API_VERSION_1_1`, `VK_API_VERSION_1_0`.
    The patch version number specified is ignored. Only the major and minor versions are considered.
    It must be less or euqal (preferably equal) to value as passed to `vkCreateInstance` as `VkApplicationInfo::apiVersion`.
    Only versions 1.0 and 1.1 are supported by the current implementation.
    Leaving it initialized to zero is equivalent to `VK_API_VERSION_1_0`.
    */
	uint32_t vulkanApiVersion;
} VmaAllocatorCreateInfo;

/// Creates Allocator object.
VMA_CALL_PRE VkResult VMA_CALL_POST vmaCreateAllocator(
    const VmaAllocatorCreateInfo *pCreateInfo,
    VmaAllocator *                pAllocator );

/// Destroys allocator object.
VMA_CALL_PRE void VMA_CALL_POST vmaDestroyAllocator(
    VmaAllocator allocator );

/**
PhysicalDeviceProperties are fetched from physicalDevice by the allocator.
You can access it here, without fetching it again on your own.
*/
VMA_CALL_PRE void VMA_CALL_POST vmaGetPhysicalDeviceProperties(
    VmaAllocator                       allocator,
    const VkPhysicalDeviceProperties **ppPhysicalDeviceProperties );

/**
PhysicalDeviceMemoryProperties are fetched from physicalDevice by the allocator.
You can access it here, without fetching it again on your own.
*/
VMA_CALL_PRE void VMA_CALL_POST vmaGetMemoryProperties(
    VmaAllocator                             allocator,
    const VkPhysicalDeviceMemoryProperties **ppPhysicalDeviceMemoryProperties );

/**
\brief Given Memory Type Index, returns Property Flags of this memory type.

This is just a convenience function. Same information can be obtained using
vmaGetMemoryProperties().
*/
VMA_CALL_PRE void VMA_CALL_POST vmaGetMemoryTypeProperties(
    VmaAllocator           allocator,
    uint32_t               memoryTypeIndex,
    VkMemoryPropertyFlags *pFlags );

/** \brief Sets index of the current frame.

This function must be used if you make allocations with
#VMA_ALLOCATION_CREATE_CAN_BECOME_LOST_BIT and
#VMA_ALLOCATION_CREATE_CAN_MAKE_OTHER_LOST_BIT flags to inform the allocator
when a new frame begins. Allocations queried using vmaGetAllocationInfo() cannot
become lost in the current frame.
*/
VMA_CALL_PRE void VMA_CALL_POST vmaSetCurrentFrameIndex(
    VmaAllocator allocator,
    uint32_t     frameIndex );

/** \brief Calculated statistics of memory usage in entire allocator.
*/
typedef struct VmaStatInfo {
	/// Number of `VkDeviceMemory` Vulkan memory blocks allocated.
	uint32_t blockCount;
	/// Number of #VmaAllocation allocation objects allocated.
	uint32_t allocationCount;
	/// Number of free ranges of memory between allocations.
	uint32_t unusedRangeCount;
	/// Total number of bytes occupied by all allocations.
	VkDeviceSize usedBytes;
	/// Total number of bytes occupied by unused ranges.
	VkDeviceSize unusedBytes;
	VkDeviceSize allocationSizeMin, allocationSizeAvg, allocationSizeMax;
	VkDeviceSize unusedRangeSizeMin, unusedRangeSizeAvg, unusedRangeSizeMax;
} VmaStatInfo;

/// General statistics from current state of Allocator.
typedef struct VmaStats {
	VmaStatInfo memoryType[ VK_MAX_MEMORY_TYPES ];
	VmaStatInfo memoryHeap[ VK_MAX_MEMORY_HEAPS ];
	VmaStatInfo total;
} VmaStats;

/** \brief Retrieves statistics from current state of the Allocator.

This function is called "calculate" not "get" because it has to traverse all
internal data structures, so it may be quite slow. For faster but more brief statistics
suitable to be called every frame or every allocation, use vmaGetBudget().

Note that when using allocator from multiple threads, returned information may immediately
become outdated.
*/
VMA_CALL_PRE void VMA_CALL_POST vmaCalculateStats(
    VmaAllocator allocator,
    VmaStats *   pStats );

/** \brief Statistics of current memory usage and available budget, in bytes, for specific memory heap.
*/
typedef struct VmaBudget {
	/** \brief Sum size of all `VkDeviceMemory` blocks allocated from particular heap, in bytes.
    */
	VkDeviceSize blockBytes;

	/** \brief Sum size of all allocations created in particular heap, in bytes.
    
    Usually less or equal than `blockBytes`.
    Difference `blockBytes - allocationBytes` is the amount of memory allocated but unused -
    available for new allocations or wasted due to fragmentation.
    
    It might be greater than `blockBytes` if there are some allocations in lost state, as they account
    to this value as well.
    */
	VkDeviceSize allocationBytes;

	/** \brief Estimated current memory usage of the program, in bytes.
    
    Fetched from system using `VK_EXT_memory_budget` extension if enabled.
    
    It might be different than `blockBytes` (usually higher) due to additional implicit objects
    also occupying the memory, like swapchain, pipelines, descriptor heaps, command buffers, or
    `VkDeviceMemory` blocks allocated outside of this library, if any.
    */
	VkDeviceSize usage;

	/** \brief Estimated amount of memory available to the program, in bytes.
    
    Fetched from system using `VK_EXT_memory_budget` extension if enabled.
    
    It might be different (most probably smaller) than `VkMemoryHeap::size[heapIndex]` due to factors
    external to the program, like other programs also consuming system resources.
    Difference `budget - usage` is the amount of additional memory that can probably
    be allocated without problems. Exceeding the budget may result in various problems.
    */
	VkDeviceSize budget;
} VmaBudget;

/** \brief Retrieves information about current memory budget for all memory heaps.

\param[out] pBudget Must point to array with number of elements at least equal to number of memory heaps in physical device used.

This function is called "get" not "calculate" because it is very fast, suitable to be called
every frame or every allocation. For more detailed statistics use vmaCalculateStats().

Note that when using allocator from multiple threads, returned information may immediately
become outdated.
*/
VMA_CALL_PRE void VMA_CALL_POST vmaGetBudget(
    VmaAllocator allocator,
    VmaBudget *  pBudget );

#ifndef VMA_STATS_STRING_ENABLED
#	define VMA_STATS_STRING_ENABLED 1
#endif

#if VMA_STATS_STRING_ENABLED

/// Builds and returns statistics as string in JSON format.
/** @param[out] ppStatsString Must be freed using vmaFreeStatsString() function.
*/
VMA_CALL_PRE void VMA_CALL_POST vmaBuildStatsString(
    VmaAllocator allocator,
    char **      ppStatsString,
    VkBool32     detailedMap );

VMA_CALL_PRE void VMA_CALL_POST vmaFreeStatsString(
    VmaAllocator allocator,
    char *       pStatsString );

#endif // #if VMA_STATS_STRING_ENABLED

/** \struct VmaPool
\brief Represents custom memory pool

Fill structure VmaPoolCreateInfo and call function vmaCreatePool() to create it.
Call function vmaDestroyPool() to destroy it.

For more information see [Custom memory pools](@ref choosing_memory_type_custom_memory_pools).
*/
VK_DEFINE_HANDLE( VmaPool )

typedef enum VmaMemoryUsage {
	/** No intended memory usage specified.
    Use other members of VmaAllocationCreateInfo to specify your requirements.
    */
	VMA_MEMORY_USAGE_UNKNOWN = 0,
	/** Memory will be used on device only, so fast access from the device is preferred.
    It usually means device-local GPU (video) memory.
    No need to be mappable on host.
    It is roughly equivalent of `D3D12_HEAP_TYPE_DEFAULT`.

    Usage:
    
    - Resources written and read by device, e.g. images used as attachments.
    - Resources transferred from host once (immutable) or infrequently and read by
      device multiple times, e.g. textures to be sampled, vertex buffers, uniform
      (constant) buffers, and majority of other types of resources used on GPU.

    Allocation may still end up in `HOST_VISIBLE` memory on some implementations.
    In such case, you are free to map it.
    You can use #VMA_ALLOCATION_CREATE_MAPPED_BIT with this usage type.
    */
	VMA_MEMORY_USAGE_GPU_ONLY = 1,
	/** Memory will be mappable on host.
    It usually means CPU (system) memory.
    Guarantees to be `HOST_VISIBLE` and `HOST_COHERENT`.
    CPU access is typically uncached. Writes may be write-combined.
    Resources created in this pool may still be accessible to the device, but access to them can be slow.
    It is roughly equivalent of `D3D12_HEAP_TYPE_UPLOAD`.

    Usage: Staging copy of resources used as transfer source.
    */
	VMA_MEMORY_USAGE_CPU_ONLY = 2,
	/**
    Memory that is both mappable on host (guarantees to be `HOST_VISIBLE`) and preferably fast to access by GPU.
    CPU access is typically uncached. Writes may be write-combined.

    Usage: Resources written frequently by host (dynamic), read by device. E.g. textures, vertex buffers, uniform buffers updated every frame or every draw call.
    */
	VMA_MEMORY_USAGE_CPU_TO_GPU = 3,
	/** Memory mappable on host (guarantees to be `HOST_VISIBLE`) and cached.
    It is roughly equivalent of `D3D12_HEAP_TYPE_READBACK`.

    Usage:

    - Resources written by device, read by host - results of some computations, e.g. screen capture, average scene luminance for HDR tone mapping.
    - Any resources read or accessed randomly on host, e.g. CPU-side copy of vertex buffer used as source of transfer, but also used for collision detection.
    */
	VMA_MEMORY_USAGE_GPU_TO_CPU = 4,
	/** CPU memory - memory that is preferably not `DEVICE_LOCAL`, but also not guaranteed to be `HOST_VISIBLE`.

    Usage: Staging copy of resources moved from GPU memory to CPU memory as part
    of custom paging/residency mechanism, to be moved back to GPU memory when needed.
    */
	VMA_MEMORY_USAGE_CPU_COPY = 5,
	/** Lazily allocated GPU memory having `VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT`.
    Exists mostly on mobile platforms. Using it on desktop PC or other GPUs with no such memory type present will fail the allocation.
    
    Usage: Memory for transient attachment images (color attachments, depth attachments etc.), created with `VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT`.

    Allocations with this usage are always created as dedicated - it implies #VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT.
    */
	VMA_MEMORY_USAGE_GPU_LAZILY_ALLOCATED = 6,

	VMA_MEMORY_USAGE_MAX_ENUM = 0x7FFFFFFF
} VmaMemoryUsage;

/// Flags to be passed as VmaAllocationCreateInfo::flags.
typedef enum VmaAllocationCreateFlagBits {
	/** \brief Set this flag if the allocation should have its own memory block.
    
    Use it for special, big resources, like fullscreen images used as attachments.
   
    You should not use this flag if VmaAllocationCreateInfo::pool is not null.
    */
	VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT = 0x00000001,

	/** \brief Set this flag to only try to allocate from existing `VkDeviceMemory` blocks and never create new such block.
    
    If new allocation cannot be placed in any of the existing blocks, allocation
    fails with `VK_ERROR_OUT_OF_DEVICE_MEMORY` error.
    
    You should not use #VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT and
    #VMA_ALLOCATION_CREATE_NEVER_ALLOCATE_BIT at the same time. It makes no sense.
    
    If VmaAllocationCreateInfo::pool is not null, this flag is implied and ignored. */
	VMA_ALLOCATION_CREATE_NEVER_ALLOCATE_BIT = 0x00000002,
	/** \brief Set this flag to use a memory that will be persistently mapped and retrieve pointer to it.
    
    Pointer to mapped memory will be returned through VmaAllocationInfo::pMappedData.

    Is it valid to use this flag for allocation made from memory type that is not
    `HOST_VISIBLE`. This flag is then ignored and memory is not mapped. This is
    useful if you need an allocation that is efficient to use on GPU
    (`DEVICE_LOCAL`) and still want to map it directly if possible on platforms that
    support it (e.g. Intel GPU).

    You should not use this flag together with #VMA_ALLOCATION_CREATE_CAN_BECOME_LOST_BIT.
    */
	VMA_ALLOCATION_CREATE_MAPPED_BIT = 0x00000004,
	/** Allocation created with this flag can become lost as a result of another
    allocation with #VMA_ALLOCATION_CREATE_CAN_MAKE_OTHER_LOST_BIT flag, so you
    must check it before use.

    To check if allocation is not lost, call vmaGetAllocationInfo() and check if
    VmaAllocationInfo::deviceMemory is not `VK_NULL_HANDLE`.

    For details about supporting lost allocations, see Lost Allocations
    chapter of User Guide on Main Page.

    You should not use this flag together with #VMA_ALLOCATION_CREATE_MAPPED_BIT.
    */
	VMA_ALLOCATION_CREATE_CAN_BECOME_LOST_BIT = 0x00000008,
	/** While creating allocation using this flag, other allocations that were
    created with flag #VMA_ALLOCATION_CREATE_CAN_BECOME_LOST_BIT can become lost.

    For details about supporting lost allocations, see Lost Allocations
    chapter of User Guide on Main Page.
    */
	VMA_ALLOCATION_CREATE_CAN_MAKE_OTHER_LOST_BIT = 0x00000010,
	/** Set this flag to treat VmaAllocationCreateInfo::pUserData as pointer to a
    null-terminated string. Instead of copying pointer value, a local copy of the
    string is made and stored in allocation's `pUserData`. The string is automatically
    freed together with the allocation. It is also used in vmaBuildStatsString().
    */
	VMA_ALLOCATION_CREATE_USER_DATA_COPY_STRING_BIT = 0x00000020,
	/** Allocation will be created from upper stack in a double stack pool.

    This flag is only allowed for custom pools created with #VMA_POOL_CREATE_LINEAR_ALGORITHM_BIT flag.
    */
	VMA_ALLOCATION_CREATE_UPPER_ADDRESS_BIT = 0x00000040,
	/** Create both buffer/image and allocation, but don't bind them together.
    It is useful when you want to bind yourself to do some more advanced binding, e.g. using some extensions.
    The flag is meaningful only with functions that bind by default: vmaCreateBuffer(), vmaCreateImage().
    Otherwise it is ignored.
    */
	VMA_ALLOCATION_CREATE_DONT_BIND_BIT = 0x00000080,
	/** Create allocation only if additional device memory required for it, if any, won't exceed
    memory budget. Otherwise return `VK_ERROR_OUT_OF_DEVICE_MEMORY`.
    */
	VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT = 0x00000100,

	/** Allocation strategy that chooses smallest possible free range for the
    allocation.
    */
	VMA_ALLOCATION_CREATE_STRATEGY_BEST_FIT_BIT = 0x00010000,
	/** Allocation strategy that chooses biggest possible free range for the
    allocation.
    */
	VMA_ALLOCATION_CREATE_STRATEGY_WORST_FIT_BIT = 0x00020000,
	/** Allocation strategy that chooses first suitable free range for the
    allocation.

    "First" doesn't necessarily means the one with smallest offset in memory,
    but rather the one that is easiest and fastest to find.
    */
	VMA_ALLOCATION_CREATE_STRATEGY_FIRST_FIT_BIT = 0x00040000,

	/** Allocation strategy that tries to minimize memory usage.
    */
	VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT = VMA_ALLOCATION_CREATE_STRATEGY_BEST_FIT_BIT,
	/** Allocation strategy that tries to minimize allocation time.
    */
	VMA_ALLOCATION_CREATE_STRATEGY_MIN_TIME_BIT = VMA_ALLOCATION_CREATE_STRATEGY_FIRST_FIT_BIT,
	/** Allocation strategy that tries to minimize memory fragmentation.
    */
	VMA_ALLOCATION_CREATE_STRATEGY_MIN_FRAGMENTATION_BIT = VMA_ALLOCATION_CREATE_STRATEGY_WORST_FIT_BIT,

	/** A bit mask to extract only `STRATEGY` bits from entire set of flags.
    */
	VMA_ALLOCATION_CREATE_STRATEGY_MASK =
	    VMA_ALLOCATION_CREATE_STRATEGY_BEST_FIT_BIT |
	    VMA_ALLOCATION_CREATE_STRATEGY_WORST_FIT_BIT |
	    VMA_ALLOCATION_CREATE_STRATEGY_FIRST_FIT_BIT,

	VMA_ALLOCATION_CREATE_FLAG_BITS_MAX_ENUM = 0x7FFFFFFF
} VmaAllocationCreateFlagBits;
typedef VkFlags VmaAllocationCreateFlags;

typedef struct VmaAllocationCreateInfo {
	/// Use #VmaAllocationCreateFlagBits enum.
	VmaAllocationCreateFlags flags;
	/** \brief Intended usage of memory.
    
    You can leave #VMA_MEMORY_USAGE_UNKNOWN if you specify memory requirements in other way. \n
    If `pool` is not null, this member is ignored.
    */
	VmaMemoryUsage usage;
	/** \brief Flags that must be set in a Memory Type chosen for an allocation.
    
    Leave 0 if you specify memory requirements in other way. \n
    If `pool` is not null, this member is ignored.*/
	VkMemoryPropertyFlags requiredFlags;
	/** \brief Flags that preferably should be set in a memory type chosen for an allocation.
    
    Set to 0 if no additional flags are prefered. \n
    If `pool` is not null, this member is ignored. */
	VkMemoryPropertyFlags preferredFlags;
	/** \brief Bitmask containing one bit set for every memory type acceptable for this allocation.

    Value 0 is equivalent to `UINT32_MAX` - it means any memory type is accepted if
    it meets other requirements specified by this structure, with no further
    restrictions on memory type index. \n
    If `pool` is not null, this member is ignored.
    */
	uint32_t memoryTypeBits;
	/** \brief Pool that this allocation should be created in.

    Leave `VK_NULL_HANDLE` to allocate from default pool. If not null, members:
    `usage`, `requiredFlags`, `preferredFlags`, `memoryTypeBits` are ignored.
    */
	VmaPool pool;
	/** \brief Custom general-purpose pointer that will be stored in #VmaAllocation, can be read as VmaAllocationInfo::pUserData and changed using vmaSetAllocationUserData().
    
    If #VMA_ALLOCATION_CREATE_USER_DATA_COPY_STRING_BIT is used, it must be either
    null or pointer to a null-terminated string. The string will be then copied to
    internal buffer, so it doesn't need to be valid after allocation call.
    */
	void *pUserData;
} VmaAllocationCreateInfo;

/**
\brief Helps to find memoryTypeIndex, given memoryTypeBits and VmaAllocationCreateInfo.

This algorithm tries to find a memory type that:

- Is allowed by memoryTypeBits.
- Contains all the flags from pAllocationCreateInfo->requiredFlags.
- Matches intended usage.
- Has as many flags from pAllocationCreateInfo->preferredFlags as possible.

\return Returns VK_ERROR_FEATURE_NOT_PRESENT if not found. Receiving such result
from this function or any other allocating function probably means that your
device doesn't support any memory type with requested features for the specific
type of resource you want to use it for. Please check parameters of your
resource, like image layout (OPTIMAL versus LINEAR) or mip level count.
*/
VMA_CALL_PRE VkResult VMA_CALL_POST vmaFindMemoryTypeIndex(
    VmaAllocator                   allocator,
    uint32_t                       memoryTypeBits,
    const VmaAllocationCreateInfo *pAllocationCreateInfo,
    uint32_t *                     pMemoryTypeIndex );

/**
\brief Helps to find memoryTypeIndex, given VkBufferCreateInfo and VmaAllocationCreateInfo.

It can be useful e.g. to determine value to be used as VmaPoolCreateInfo::memoryTypeIndex.
It internally creates a temporary, dummy buffer that never has memory bound.
It is just a convenience function, equivalent to calling:

- `vkCreateBuffer`
- `vkGetBufferMemoryRequirements`
- `vmaFindMemoryTypeIndex`
- `vkDestroyBuffer`
*/
VMA_CALL_PRE VkResult VMA_CALL_POST vmaFindMemoryTypeIndexForBufferInfo(
    VmaAllocator                   allocator,
    const VkBufferCreateInfo *     pBufferCreateInfo,
    const VmaAllocationCreateInfo *pAllocationCreateInfo,
    uint32_t *                     pMemoryTypeIndex );

/**
\brief Helps to find memoryTypeIndex, given VkImageCreateInfo and VmaAllocationCreateInfo.

It can be useful e.g. to determine value to be used as VmaPoolCreateInfo::memoryTypeIndex.
It internally creates a temporary, dummy image that never has memory bound.
It is just a convenience function, equivalent to calling:

- `vkCreateImage`
- `vkGetImageMemoryRequirements`
- `vmaFindMemoryTypeIndex`
- `vkDestroyImage`
*/
VMA_CALL_PRE VkResult VMA_CALL_POST vmaFindMemoryTypeIndexForImageInfo(
    VmaAllocator                   allocator,
    const VkImageCreateInfo *      pImageCreateInfo,
    const VmaAllocationCreateInfo *pAllocationCreateInfo,
    uint32_t *                     pMemoryTypeIndex );

/// Flags to be passed as VmaPoolCreateInfo::flags.
typedef enum VmaPoolCreateFlagBits {
	/** \brief Use this flag if you always allocate only buffers and linear images or only optimal images out of this pool and so Buffer-Image Granularity can be ignored.

    This is an optional optimization flag.

    If you always allocate using vmaCreateBuffer(), vmaCreateImage(),
    vmaAllocateMemoryForBuffer(), then you don't need to use it because allocator
    knows exact type of your allocations so it can handle Buffer-Image Granularity
    in the optimal way.

    If you also allocate using vmaAllocateMemoryForImage() or vmaAllocateMemory(),
    exact type of such allocations is not known, so allocator must be conservative
    in handling Buffer-Image Granularity, which can lead to suboptimal allocation
    (wasted memory). In that case, if you can make sure you always allocate only
    buffers and linear images or only optimal images out of this pool, use this flag
    to make allocator disregard Buffer-Image Granularity and so make allocations
    faster and more optimal.
    */
	VMA_POOL_CREATE_IGNORE_BUFFER_IMAGE_GRANULARITY_BIT = 0x00000002,

	/** \brief Enables alternative, linear allocation algorithm in this pool.

    Specify this flag to enable linear allocation algorithm, which always creates
    new allocations after last one and doesn't reuse space from allocations freed in
    between. It trades memory consumption for simplified algorithm and data
    structure, which has better performance and uses less memory for metadata.

    By using this flag, you can achieve behavior of free-at-once, stack,
    ring buffer, and double stack. For details, see documentation chapter
    \ref linear_algorithm.

    When using this flag, you must specify VmaPoolCreateInfo::maxBlockCount == 1 (or 0 for default).

    For more details, see [Linear allocation algorithm](@ref linear_algorithm).
    */
	VMA_POOL_CREATE_LINEAR_ALGORITHM_BIT = 0x00000004,

	/** \brief Enables alternative, buddy allocation algorithm in this pool.

    It operates on a tree of blocks, each having size that is a power of two and
    a half of its parent's size. Comparing to default algorithm, this one provides
    faster allocation and deallocation and decreased external fragmentation,
    at the expense of more memory wasted (internal fragmentation).

    For more details, see [Buddy allocation algorithm](@ref buddy_algorithm).
    */
	VMA_POOL_CREATE_BUDDY_ALGORITHM_BIT = 0x00000008,

	/** Bit mask to extract only `ALGORITHM` bits from entire set of flags.
    */
	VMA_POOL_CREATE_ALGORITHM_MASK =
	    VMA_POOL_CREATE_LINEAR_ALGORITHM_BIT |
	    VMA_POOL_CREATE_BUDDY_ALGORITHM_BIT,

	VMA_POOL_CREATE_FLAG_BITS_MAX_ENUM = 0x7FFFFFFF
} VmaPoolCreateFlagBits;
typedef VkFlags VmaPoolCreateFlags;

/** \brief Describes parameter of created #VmaPool.
*/
typedef struct VmaPoolCreateInfo {
	/** \brief Vulkan memory type index to allocate this pool from.
    */
	uint32_t memoryTypeIndex;
	/** \brief Use combination of #VmaPoolCreateFlagBits.
    */
	VmaPoolCreateFlags flags;
	/** \brief Size of a single `VkDeviceMemory` block to be allocated as part of this pool, in bytes. Optional.

    Specify nonzero to set explicit, constant size of memory blocks used by this
    pool.

    Leave 0 to use default and let the library manage block sizes automatically.
    Sizes of particular blocks may vary.
    */
	VkDeviceSize blockSize;
	/** \brief Minimum number of blocks to be always allocated in this pool, even if they stay empty.

    Set to 0 to have no preallocated blocks and allow the pool be completely empty.
    */
	size_t minBlockCount;
	/** \brief Maximum number of blocks that can be allocated in this pool. Optional.

    Set to 0 to use default, which is `SIZE_MAX`, which means no limit.
    
    Set to same value as VmaPoolCreateInfo::minBlockCount to have fixed amount of memory allocated
    throughout whole lifetime of this pool.
    */
	size_t maxBlockCount;
	/** \brief Maximum number of additional frames that are in use at the same time as current frame.

    This value is used only when you make allocations with
    #VMA_ALLOCATION_CREATE_CAN_BECOME_LOST_BIT flag. Such allocation cannot become
    lost if allocation.lastUseFrameIndex >= allocator.currentFrameIndex - frameInUseCount.

    For example, if you double-buffer your command buffers, so resources used for
    rendering in previous frame may still be in use by the GPU at the moment you
    allocate resources needed for the current frame, set this value to 1.

    If you want to allow any allocations other than used in the current frame to
    become lost, set this value to 0.
    */
	uint32_t frameInUseCount;
} VmaPoolCreateInfo;

/** \brief Describes parameter of existing #VmaPool.
*/
typedef struct VmaPoolStats {
	/** \brief Total amount of `VkDeviceMemory` allocated from Vulkan for this pool, in bytes.
    */
	VkDeviceSize size;
	/** \brief Total number of bytes in the pool not used by any #VmaAllocation.
    */
	VkDeviceSize unusedSize;
	/** \brief Number of #VmaAllocation objects created from this pool that were not destroyed or lost.
    */
	size_t allocationCount;
	/** \brief Number of continuous memory ranges in the pool not used by any #VmaAllocation.
    */
	size_t unusedRangeCount;
	/** \brief Size of the largest continuous free memory region available for new allocation.

    Making a new allocation of that size is not guaranteed to succeed because of
    possible additional margin required to respect alignment and buffer/image
    granularity.
    */
	VkDeviceSize unusedRangeSizeMax;
	/** \brief Number of `VkDeviceMemory` blocks allocated for this pool.
    */
	size_t blockCount;
} VmaPoolStats;

/** \brief Allocates Vulkan device memory and creates #VmaPool object.

@param allocator Allocator object.
@param pCreateInfo Parameters of pool to create.
@param[out] pPool Handle to created pool.
*/
VMA_CALL_PRE VkResult VMA_CALL_POST vmaCreatePool(
    VmaAllocator             allocator,
    const VmaPoolCreateInfo *pCreateInfo,
    VmaPool *                pPool );

/** \brief Destroys #VmaPool object and frees Vulkan device memory.
*/
VMA_CALL_PRE void VMA_CALL_POST vmaDestroyPool(
    VmaAllocator allocator,
    VmaPool      pool );

/** \brief Retrieves statistics of existing #VmaPool object.

@param allocator Allocator object.
@param pool Pool object.
@param[out] pPoolStats Statistics of specified pool.
*/
VMA_CALL_PRE void VMA_CALL_POST vmaGetPoolStats(
    VmaAllocator  allocator,
    VmaPool       pool,
    VmaPoolStats *pPoolStats );

/** \brief Marks all allocations in given pool as lost if they are not used in current frame or VmaPoolCreateInfo::frameInUseCount back from now.

@param allocator Allocator object.
@param pool Pool.
@param[out] pLostAllocationCount Number of allocations marked as lost. Optional - pass null if you don't need this information.
*/
VMA_CALL_PRE void VMA_CALL_POST vmaMakePoolAllocationsLost(
    VmaAllocator allocator,
    VmaPool      pool,
    size_t *     pLostAllocationCount );

/** \brief Checks magic number in margins around all allocations in given memory pool in search for corruptions.

Corruption detection is enabled only when `VMA_DEBUG_DETECT_CORRUPTION` macro is defined to nonzero,
`VMA_DEBUG_MARGIN` is defined to nonzero and the pool is created in memory type that is
`HOST_VISIBLE` and `HOST_COHERENT`. For more information, see [Corruption detection](@ref debugging_memory_usage_corruption_detection).

Possible return values:

- `VK_ERROR_FEATURE_NOT_PRESENT` - corruption detection is not enabled for specified pool.
- `VK_SUCCESS` - corruption detection has been performed and succeeded.
- `VK_ERROR_VALIDATION_FAILED_EXT` - corruption detection has been performed and found memory corruptions around one of the allocations.
  `VMA_ASSERT` is also fired in that case.
- Other value: Error returned by Vulkan, e.g. memory mapping failure.
*/
VMA_CALL_PRE VkResult VMA_CALL_POST vmaCheckPoolCorruption( VmaAllocator allocator, VmaPool pool );

/** \brief Retrieves name of a custom pool.

After the call `ppName` is either null or points to an internally-owned null-terminated string
containing name of the pool that was previously set. The pointer becomes invalid when the pool is
destroyed or its name is changed using vmaSetPoolName().
*/
VMA_CALL_PRE void VMA_CALL_POST vmaGetPoolName(
    VmaAllocator allocator,
    VmaPool      pool,
    const char **ppName );

/** \brief Sets name of a custom pool.

`pName` can be either null or pointer to a null-terminated string with new name for the pool.
Function makes internal copy of the string, so it can be changed or freed immediately after this call.
*/
VMA_CALL_PRE void VMA_CALL_POST vmaSetPoolName(
    VmaAllocator allocator,
    VmaPool      pool,
    const char * pName );

/** \struct VmaAllocation
\brief Represents single memory allocation.

It may be either dedicated block of `VkDeviceMemory` or a specific region of a bigger block of this type
plus unique offset.

There are multiple ways to create such object.
You need to fill structure VmaAllocationCreateInfo.
For more information see [Choosing memory type](@ref choosing_memory_type).

Although the library provides convenience functions that create Vulkan buffer or image,
allocate memory for it and bind them together,
binding of the allocation to a buffer or an image is out of scope of the allocation itself.
Allocation object can exist without buffer/image bound,
binding can be done manually by the user, and destruction of it can be done
independently of destruction of the allocation.

The object also remembers its size and some other information.
To retrieve this information, use function vmaGetAllocationInfo() and inspect
returned structure VmaAllocationInfo.

Some kinds allocations can be in lost state.
For more information, see [Lost allocations](@ref lost_allocations).
*/
VK_DEFINE_HANDLE( VmaAllocation )

/** \brief Parameters of #VmaAllocation objects, that can be retrieved using function vmaGetAllocationInfo().
*/
typedef struct VmaAllocationInfo {
	/** \brief Memory type index that this allocation was allocated from.
    
    It never changes.
    */
	uint32_t memoryType;
	/** \brief Handle to Vulkan memory object.

    Same memory object can be shared by multiple allocations.
    
    It can change after call to vmaDefragment() if this allocation is passed to the function, or if allocation is lost.

    If the allocation is lost, it is equal to `VK_NULL_HANDLE`.
    */
	VkDeviceMemory deviceMemory;
	/** \brief Offset into deviceMemory object to the beginning of this allocation, in bytes. (deviceMemory, offset) pair is unique to this allocation.

    It can change after call to vmaDefragment() if this allocation is passed to the function, or if allocation is lost.
    */
	VkDeviceSize offset;
	/** \brief Size of this allocation, in bytes.

    It never changes, unless allocation is lost.
    */
	VkDeviceSize size;
	/** \brief Pointer to the beginning of this allocation as mapped data.

    If the allocation hasn't been mapped using vmaMapMemory() and hasn't been
    created with #VMA_ALLOCATION_CREATE_MAPPED_BIT flag, this value null.

    It can change after call to vmaMapMemory(), vmaUnmapMemory().
    It can also change after call to vmaDefragment() if this allocation is passed to the function.
    */
	void *pMappedData;
	/** \brief Custom general-purpose pointer that was passed as VmaAllocationCreateInfo::pUserData or set using vmaSetAllocationUserData().

    It can change after call to vmaSetAllocationUserData() for this allocation.
    */
	void *pUserData;
} VmaAllocationInfo;

/** \brief General purpose memory allocation.

@param[out] pAllocation Handle to allocated memory.
@param[out] pAllocationInfo Optional. Information about allocated memory. It can be later fetched using function vmaGetAllocationInfo().

You should free the memory using vmaFreeMemory() or vmaFreeMemoryPages().

It is recommended to use vmaAllocateMemoryForBuffer(), vmaAllocateMemoryForImage(),
vmaCreateBuffer(), vmaCreateImage() instead whenever possible.
*/
VMA_CALL_PRE VkResult VMA_CALL_POST vmaAllocateMemory(
    VmaAllocator                   allocator,
    const VkMemoryRequirements *   pVkMemoryRequirements,
    const VmaAllocationCreateInfo *pCreateInfo,
    VmaAllocation *                pAllocation,
    VmaAllocationInfo *            pAllocationInfo );

/** \brief General purpose memory allocation for multiple allocation objects at once.

@param allocator Allocator object.
@param pVkMemoryRequirements Memory requirements for each allocation.
@param pCreateInfo Creation parameters for each alloction.
@param allocationCount Number of allocations to make.
@param[out] pAllocations Pointer to array that will be filled with handles to created allocations.
@param[out] pAllocationInfo Optional. Pointer to array that will be filled with parameters of created allocations.

You should free the memory using vmaFreeMemory() or vmaFreeMemoryPages().

Word "pages" is just a suggestion to use this function to allocate pieces of memory needed for sparse binding.
It is just a general purpose allocation function able to make multiple allocations at once.
It may be internally optimized to be more efficient than calling vmaAllocateMemory() `allocationCount` times.

All allocations are made using same parameters. All of them are created out of the same memory pool and type.
If any allocation fails, all allocations already made within this function call are also freed, so that when
returned result is not `VK_SUCCESS`, `pAllocation` array is always entirely filled with `VK_NULL_HANDLE`.
*/
VMA_CALL_PRE VkResult VMA_CALL_POST vmaAllocateMemoryPages(
    VmaAllocator                   allocator,
    const VkMemoryRequirements *   pVkMemoryRequirements,
    const VmaAllocationCreateInfo *pCreateInfo,
    size_t                         allocationCount,
    VmaAllocation *                pAllocations,
    VmaAllocationInfo *            pAllocationInfo );

/**
@param[out] pAllocation Handle to allocated memory.
@param[out] pAllocationInfo Optional. Information about allocated memory. It can be later fetched using function vmaGetAllocationInfo().

You should free the memory using vmaFreeMemory().
*/
VMA_CALL_PRE VkResult VMA_CALL_POST vmaAllocateMemoryForBuffer(
    VmaAllocator                   allocator,
    VkBuffer                       buffer,
    const VmaAllocationCreateInfo *pCreateInfo,
    VmaAllocation *                pAllocation,
    VmaAllocationInfo *            pAllocationInfo );

/// Function similar to vmaAllocateMemoryForBuffer().
VMA_CALL_PRE VkResult VMA_CALL_POST vmaAllocateMemoryForImage(
    VmaAllocator                   allocator,
    VkImage                        image,
    const VmaAllocationCreateInfo *pCreateInfo,
    VmaAllocation *                pAllocation,
    VmaAllocationInfo *            pAllocationInfo );

/** \brief Frees memory previously allocated using vmaAllocateMemory(), vmaAllocateMemoryForBuffer(), or vmaAllocateMemoryForImage().

Passing `VK_NULL_HANDLE` as `allocation` is valid. Such function call is just skipped.
*/
VMA_CALL_PRE void VMA_CALL_POST vmaFreeMemory(
    VmaAllocator  allocator,
    VmaAllocation allocation );

/** \brief Frees memory and destroys multiple allocations.

Word "pages" is just a suggestion to use this function to free pieces of memory used for sparse binding.
It is just a general purpose function to free memory and destroy allocations made using e.g. vmaAllocateMemory(),
vmaAllocateMemoryPages() and other functions.
It may be internally optimized to be more efficient than calling vmaFreeMemory() `allocationCount` times.

Allocations in `pAllocations` array can come from any memory pools and types.
Passing `VK_NULL_HANDLE` as elements of `pAllocations` array is valid. Such entries are just skipped.
*/
VMA_CALL_PRE void VMA_CALL_POST vmaFreeMemoryPages(
    VmaAllocator   allocator,
    size_t         allocationCount,
    VmaAllocation *pAllocations );

/** \brief Deprecated.

\deprecated
In version 2.2.0 it used to try to change allocation's size without moving or reallocating it.
In current version it returns `VK_SUCCESS` only if `newSize` equals current allocation's size.
Otherwise returns `VK_ERROR_OUT_OF_POOL_MEMORY`, indicating that allocation's size could not be changed.
*/
VMA_CALL_PRE VkResult VMA_CALL_POST vmaResizeAllocation(
    VmaAllocator  allocator,
    VmaAllocation allocation,
    VkDeviceSize  newSize );

/** \brief Returns current information about specified allocation and atomically marks it as used in current frame.

Current paramters of given allocation are returned in `pAllocationInfo`.

This function also atomically "touches" allocation - marks it as used in current frame,
just like vmaTouchAllocation().
If the allocation is in lost state, `pAllocationInfo->deviceMemory == VK_NULL_HANDLE`.

Although this function uses atomics and doesn't lock any mutex, so it should be quite efficient,
you can avoid calling it too often.

- You can retrieve same VmaAllocationInfo structure while creating your resource, from function
  vmaCreateBuffer(), vmaCreateImage(). You can remember it if you are sure parameters don't change
  (e.g. due to defragmentation or allocation becoming lost).
- If you just want to check if allocation is not lost, vmaTouchAllocation() will work faster.
*/
VMA_CALL_PRE void VMA_CALL_POST vmaGetAllocationInfo(
    VmaAllocator       allocator,
    VmaAllocation      allocation,
    VmaAllocationInfo *pAllocationInfo );

/** \brief Returns `VK_TRUE` if allocation is not lost and atomically marks it as used in current frame.

If the allocation has been created with #VMA_ALLOCATION_CREATE_CAN_BECOME_LOST_BIT flag,
this function returns `VK_TRUE` if it's not in lost state, so it can still be used.
It then also atomically "touches" the allocation - marks it as used in current frame,
so that you can be sure it won't become lost in current frame or next `frameInUseCount` frames.

If the allocation is in lost state, the function returns `VK_FALSE`.
Memory of such allocation, as well as buffer or image bound to it, should not be used.
Lost allocation and the buffer/image still need to be destroyed.

If the allocation has been created without #VMA_ALLOCATION_CREATE_CAN_BECOME_LOST_BIT flag,
this function always returns `VK_TRUE`.
*/
VMA_CALL_PRE VkBool32 VMA_CALL_POST vmaTouchAllocation(
    VmaAllocator  allocator,
    VmaAllocation allocation );

/** \brief Sets pUserData in given allocation to new value.

If the allocation was created with VMA_ALLOCATION_CREATE_USER_DATA_COPY_STRING_BIT,
pUserData must be either null, or pointer to a null-terminated string. The function
makes local copy of the string and sets it as allocation's `pUserData`. String
passed as pUserData doesn't need to be valid for whole lifetime of the allocation -
you can free it after this call. String previously pointed by allocation's
pUserData is freed from memory.

If the flag was not used, the value of pointer `pUserData` is just copied to
allocation's `pUserData`. It is opaque, so you can use it however you want - e.g.
as a pointer, ordinal number or some handle to you own data.
*/
VMA_CALL_PRE void VMA_CALL_POST vmaSetAllocationUserData(
    VmaAllocator  allocator,
    VmaAllocation allocation,
    void *        pUserData );

/** \brief Creates new allocation that is in lost state from the beginning.

It can be useful if you need a dummy, non-null allocation.

You still need to destroy created object using vmaFreeMemory().

Returned allocation is not tied to any specific memory pool or memory type and
not bound to any image or buffer. It has size = 0. It cannot be turned into
a real, non-empty allocation.
*/
VMA_CALL_PRE void VMA_CALL_POST vmaCreateLostAllocation(
    VmaAllocator   allocator,
    VmaAllocation *pAllocation );

/** \brief Maps memory represented by given allocation and returns pointer to it.

Maps memory represented by given allocation to make it accessible to CPU code.
When succeeded, `*ppData` contains pointer to first byte of this memory.
If the allocation is part of bigger `VkDeviceMemory` block, the pointer is
correctly offseted to the beginning of region assigned to this particular
allocation.

Mapping is internally reference-counted and synchronized, so despite raw Vulkan
function `vkMapMemory()` cannot be used to map same block of `VkDeviceMemory`
multiple times simultaneously, it is safe to call this function on allocations
assigned to the same memory block. Actual Vulkan memory will be mapped on first
mapping and unmapped on last unmapping.

If the function succeeded, you must call vmaUnmapMemory() to unmap the
allocation when mapping is no longer needed or before freeing the allocation, at
the latest.

It also safe to call this function multiple times on the same allocation. You
must call vmaUnmapMemory() same number of times as you called vmaMapMemory().

It is also safe to call this function on allocation created with
#VMA_ALLOCATION_CREATE_MAPPED_BIT flag. Its memory stays mapped all the time.
You must still call vmaUnmapMemory() same number of times as you called
vmaMapMemory(). You must not call vmaUnmapMemory() additional time to free the
"0-th" mapping made automatically due to #VMA_ALLOCATION_CREATE_MAPPED_BIT flag.

This function fails when used on allocation made in memory type that is not
`HOST_VISIBLE`.

This function always fails when called for allocation that was created with
#VMA_ALLOCATION_CREATE_CAN_BECOME_LOST_BIT flag. Such allocations cannot be
mapped.

This function doesn't automatically flush or invalidate caches.
If the allocation is made from a memory types that is not `HOST_COHERENT`,
you also need to use vmaInvalidateAllocation() / vmaFlushAllocation(), as required by Vulkan specification.
*/
VMA_CALL_PRE VkResult VMA_CALL_POST vmaMapMemory(
    VmaAllocator  allocator,
    VmaAllocation allocation,
    void **       ppData );

/** \brief Unmaps memory represented by given allocation, mapped previously using vmaMapMemory().

For details, see description of vmaMapMemory().

This function doesn't automatically flush or invalidate caches.
If the allocation is made from a memory types that is not `HOST_COHERENT`,
you also need to use vmaInvalidateAllocation() / vmaFlushAllocation(), as required by Vulkan specification.
*/
VMA_CALL_PRE void VMA_CALL_POST vmaUnmapMemory(
    VmaAllocator  allocator,
    VmaAllocation allocation );

/** \brief Flushes memory of given allocation.

Calls `vkFlushMappedMemoryRanges()` for memory associated with given range of given allocation.
It needs to be called after writing to a mapped memory for memory types that are not `HOST_COHERENT`.
Unmap operation doesn't do that automatically.

- `offset` must be relative to the beginning of allocation.
- `size` can be `VK_WHOLE_SIZE`. It means all memory from `offset` the the end of given allocation.
- `offset` and `size` don't have to be aligned.
  They are internally rounded down/up to multiply of `nonCoherentAtomSize`.
- If `size` is 0, this call is ignored.
- If memory type that the `allocation` belongs to is not `HOST_VISIBLE` or it is `HOST_COHERENT`,
  this call is ignored.

Warning! `offset` and `size` are relative to the contents of given `allocation`.
If you mean whole allocation, you can pass 0 and `VK_WHOLE_SIZE`, respectively.
Do not pass allocation's offset as `offset`!!!
*/
VMA_CALL_PRE void VMA_CALL_POST vmaFlushAllocation( VmaAllocator allocator, VmaAllocation allocation, VkDeviceSize offset, VkDeviceSize size );

/** \brief Invalidates memory of given allocation.

Calls `vkInvalidateMappedMemoryRanges()` for memory associated with given range of given allocation.
It needs to be called before reading from a mapped memory for memory types that are not `HOST_COHERENT`.
Map operation doesn't do that automatically.

- `offset` must be relative to the beginning of allocation.
- `size` can be `VK_WHOLE_SIZE`. It means all memory from `offset` the the end of given allocation.
- `offset` and `size` don't have to be aligned.
  They are internally rounded down/up to multiply of `nonCoherentAtomSize`.
- If `size` is 0, this call is ignored.
- If memory type that the `allocation` belongs to is not `HOST_VISIBLE` or it is `HOST_COHERENT`,
  this call is ignored.

Warning! `offset` and `size` are relative to the contents of given `allocation`.
If you mean whole allocation, you can pass 0 and `VK_WHOLE_SIZE`, respectively.
Do not pass allocation's offset as `offset`!!!
*/
VMA_CALL_PRE void VMA_CALL_POST vmaInvalidateAllocation( VmaAllocator allocator, VmaAllocation allocation, VkDeviceSize offset, VkDeviceSize size );

/** \brief Checks magic number in margins around all allocations in given memory types (in both default and custom pools) in search for corruptions.

@param memoryTypeBits Bit mask, where each bit set means that a memory type with that index should be checked.

Corruption detection is enabled only when `VMA_DEBUG_DETECT_CORRUPTION` macro is defined to nonzero,
`VMA_DEBUG_MARGIN` is defined to nonzero and only for memory types that are
`HOST_VISIBLE` and `HOST_COHERENT`. For more information, see [Corruption detection](@ref debugging_memory_usage_corruption_detection).

Possible return values:

- `VK_ERROR_FEATURE_NOT_PRESENT` - corruption detection is not enabled for any of specified memory types.
- `VK_SUCCESS` - corruption detection has been performed and succeeded.
- `VK_ERROR_VALIDATION_FAILED_EXT` - corruption detection has been performed and found memory corruptions around one of the allocations.
  `VMA_ASSERT` is also fired in that case.
- Other value: Error returned by Vulkan, e.g. memory mapping failure.
*/
VMA_CALL_PRE VkResult VMA_CALL_POST vmaCheckCorruption( VmaAllocator allocator, uint32_t memoryTypeBits );

/** \struct VmaDefragmentationContext
\brief Represents Opaque object that represents started defragmentation process.

Fill structure #VmaDefragmentationInfo2 and call function vmaDefragmentationBegin() to create it.
Call function vmaDefragmentationEnd() to destroy it.
*/
VK_DEFINE_HANDLE( VmaDefragmentationContext )

/// Flags to be used in vmaDefragmentationBegin(). None at the moment. Reserved for future use.
typedef enum VmaDefragmentationFlagBits {
	VMA_DEFRAGMENTATION_FLAG_INCREMENTAL   = 0x1,
	VMA_DEFRAGMENTATION_FLAG_BITS_MAX_ENUM = 0x7FFFFFFF
} VmaDefragmentationFlagBits;
typedef VkFlags VmaDefragmentationFlags;

/** \brief Parameters for defragmentation.

To be used with function vmaDefragmentationBegin().
*/
typedef struct VmaDefragmentationInfo2 {
	/** \brief Reserved for future use. Should be 0.
    */
	VmaDefragmentationFlags flags;
	/** \brief Number of allocations in `pAllocations` array.
    */
	uint32_t allocationCount;
	/** \brief Pointer to array of allocations that can be defragmented.

    The array should have `allocationCount` elements.
    The array should not contain nulls.
    Elements in the array should be unique - same allocation cannot occur twice.
    It is safe to pass allocations that are in the lost state - they are ignored.
    All allocations not present in this array are considered non-moveable during this defragmentation.
    */
	VmaAllocation *pAllocations;
	/** \brief Optional, output. Pointer to array that will be filled with information whether the allocation at certain index has been changed during defragmentation.

    The array should have `allocationCount` elements.
    You can pass null if you are not interested in this information.
    */
	VkBool32 *pAllocationsChanged;
	/** \brief Numer of pools in `pPools` array.
    */
	uint32_t poolCount;
	/** \brief Either null or pointer to array of pools to be defragmented.

    All the allocations in the specified pools can be moved during defragmentation
    and there is no way to check if they were really moved as in `pAllocationsChanged`,
    so you must query all the allocations in all these pools for new `VkDeviceMemory`
    and offset using vmaGetAllocationInfo() if you might need to recreate buffers
    and images bound to them.

    The array should have `poolCount` elements.
    The array should not contain nulls.
    Elements in the array should be unique - same pool cannot occur twice.

    Using this array is equivalent to specifying all allocations from the pools in `pAllocations`.
    It might be more efficient.
    */
	VmaPool *pPools;
	/** \brief Maximum total numbers of bytes that can be copied while moving allocations to different places using transfers on CPU side, like `memcpy()`, `memmove()`.
    
    `VK_WHOLE_SIZE` means no limit.
    */
	VkDeviceSize maxCpuBytesToMove;
	/** \brief Maximum number of allocations that can be moved to a different place using transfers on CPU side, like `memcpy()`, `memmove()`.

    `UINT32_MAX` means no limit.
    */
	uint32_t maxCpuAllocationsToMove;
	/** \brief Maximum total numbers of bytes that can be copied while moving allocations to different places using transfers on GPU side, posted to `commandBuffer`.
    
    `VK_WHOLE_SIZE` means no limit.
    */
	VkDeviceSize maxGpuBytesToMove;
	/** \brief Maximum number of allocations that can be moved to a different place using transfers on GPU side, posted to `commandBuffer`.

    `UINT32_MAX` means no limit.
    */
	uint32_t maxGpuAllocationsToMove;
	/** \brief Optional. Command buffer where GPU copy commands will be posted.

    If not null, it must be a valid command buffer handle that supports Transfer queue type.
    It must be in the recording state and outside of a render pass instance.
    You need to submit it and make sure it finished execution before calling vmaDefragmentationEnd().

    Passing null means that only CPU defragmentation will be performed.
    */
	VkCommandBuffer commandBuffer;
} VmaDefragmentationInfo2;

typedef struct VmaDefragmentationPassMoveInfo {
	VmaAllocation  allocation;
	VkDeviceMemory memory;
	VkDeviceSize   offset;
} VmaDefragmentationPassMoveInfo;

/** \brief Parameters for incremental defragmentation steps.

To be used with function vmaBeginDefragmentationPass().
*/
typedef struct VmaDefragmentationPassInfo {
	uint32_t                        moveCount;
	VmaDefragmentationPassMoveInfo *pMoves;
} VmaDefragmentationPassInfo;

/** \brief Deprecated. Optional configuration parameters to be passed to function vmaDefragment().

\deprecated This is a part of the old interface. It is recommended to use structure #VmaDefragmentationInfo2 and function vmaDefragmentationBegin() instead.
*/
typedef struct VmaDefragmentationInfo {
	/** \brief Maximum total numbers of bytes that can be copied while moving allocations to different places.
    
    Default is `VK_WHOLE_SIZE`, which means no limit.
    */
	VkDeviceSize maxBytesToMove;
	/** \brief Maximum number of allocations that can be moved to different place.

    Default is `UINT32_MAX`, which means no limit.
    */
	uint32_t maxAllocationsToMove;
} VmaDefragmentationInfo;

/** \brief Statistics returned by function vmaDefragment(). */
typedef struct VmaDefragmentationStats {
	/// Total number of bytes that have been copied while moving allocations to different places.
	VkDeviceSize bytesMoved;
	/// Total number of bytes that have been released to the system by freeing empty `VkDeviceMemory` objects.
	VkDeviceSize bytesFreed;
	/// Number of allocations that have been moved to different places.
	uint32_t allocationsMoved;
	/// Number of empty `VkDeviceMemory` objects that have been released to the system.
	uint32_t deviceMemoryBlocksFreed;
} VmaDefragmentationStats;

/** \brief Begins defragmentation process.

@param allocator Allocator object.
@param pInfo Structure filled with parameters of defragmentation.
@param[out] pStats Optional. Statistics of defragmentation. You can pass null if you are not interested in this information.
@param[out] pContext Context object that must be passed to vmaDefragmentationEnd() to finish defragmentation.
@return `VK_SUCCESS` and `*pContext == null` if defragmentation finished within this function call. `VK_NOT_READY` and `*pContext != null` if defragmentation has been started and you need to call vmaDefragmentationEnd() to finish it. Negative value in case of error.

Use this function instead of old, deprecated vmaDefragment().

Warning! Between the call to vmaDefragmentationBegin() and vmaDefragmentationEnd():

- You should not use any of allocations passed as `pInfo->pAllocations` or
  any allocations that belong to pools passed as `pInfo->pPools`,
  including calling vmaGetAllocationInfo(), vmaTouchAllocation(), or access
  their data.
- Some mutexes protecting internal data structures may be locked, so trying to
  make or free any allocations, bind buffers or images, map memory, or launch
  another simultaneous defragmentation in between may cause stall (when done on
  another thread) or deadlock (when done on the same thread), unless you are
  100% sure that defragmented allocations are in different pools.
- Information returned via `pStats` and `pInfo->pAllocationsChanged` are undefined.
  They become valid after call to vmaDefragmentationEnd().
- If `pInfo->commandBuffer` is not null, you must submit that command buffer
  and make sure it finished execution before calling vmaDefragmentationEnd().

For more information and important limitations regarding defragmentation, see documentation chapter:
[Defragmentation](@ref defragmentation).
*/
VMA_CALL_PRE VkResult VMA_CALL_POST vmaDefragmentationBegin(
    VmaAllocator                   allocator,
    const VmaDefragmentationInfo2 *pInfo,
    VmaDefragmentationStats *      pStats,
    VmaDefragmentationContext *    pContext );

/** \brief Ends defragmentation process.

Use this function to finish defragmentation started by vmaDefragmentationBegin().
It is safe to pass `context == null`. The function then does nothing.
*/
VMA_CALL_PRE VkResult VMA_CALL_POST vmaDefragmentationEnd(
    VmaAllocator              allocator,
    VmaDefragmentationContext context );

VMA_CALL_PRE VkResult VMA_CALL_POST vmaBeginDefragmentationPass(
    VmaAllocator                allocator,
    VmaDefragmentationContext   context,
    VmaDefragmentationPassInfo *pInfo );
VMA_CALL_PRE VkResult VMA_CALL_POST vmaEndDefragmentationPass(
    VmaAllocator              allocator,
    VmaDefragmentationContext context );

/** \brief Deprecated. Compacts memory by moving allocations.

@param pAllocations Array of allocations that can be moved during this compation.
@param allocationCount Number of elements in pAllocations and pAllocationsChanged arrays.
@param[out] pAllocationsChanged Array of boolean values that will indicate whether matching allocation in pAllocations array has been moved. This parameter is optional. Pass null if you don't need this information.
@param pDefragmentationInfo Configuration parameters. Optional - pass null to use default values.
@param[out] pDefragmentationStats Statistics returned by the function. Optional - pass null if you don't need this information.
@return `VK_SUCCESS` if completed, negative error code in case of error.

\deprecated This is a part of the old interface. It is recommended to use structure #VmaDefragmentationInfo2 and function vmaDefragmentationBegin() instead.

This function works by moving allocations to different places (different
`VkDeviceMemory` objects and/or different offsets) in order to optimize memory
usage. Only allocations that are in `pAllocations` array can be moved. All other
allocations are considered nonmovable in this call. Basic rules:

- Only allocations made in memory types that have
  `VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT` and `VK_MEMORY_PROPERTY_HOST_COHERENT_BIT`
  flags can be compacted. You may pass other allocations but it makes no sense -
  these will never be moved.
- Custom pools created with #VMA_POOL_CREATE_LINEAR_ALGORITHM_BIT or
  #VMA_POOL_CREATE_BUDDY_ALGORITHM_BIT flag are not defragmented. Allocations
  passed to this function that come from such pools are ignored.
- Allocations created with #VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT or
  created as dedicated allocations for any other reason are also ignored.
- Both allocations made with or without #VMA_ALLOCATION_CREATE_MAPPED_BIT
  flag can be compacted. If not persistently mapped, memory will be mapped
  temporarily inside this function if needed.
- You must not pass same #VmaAllocation object multiple times in `pAllocations` array.

The function also frees empty `VkDeviceMemory` blocks.

Warning: This function may be time-consuming, so you shouldn't call it too often
(like after every resource creation/destruction).
You can call it on special occasions (like when reloading a game level or
when you just destroyed a lot of objects). Calling it every frame may be OK, but
you should measure that on your platform.

For more information, see [Defragmentation](@ref defragmentation) chapter.
*/
VMA_CALL_PRE VkResult VMA_CALL_POST vmaDefragment(
    VmaAllocator                  allocator,
    VmaAllocation *               pAllocations,
    size_t                        allocationCount,
    VkBool32 *                    pAllocationsChanged,
    const VmaDefragmentationInfo *pDefragmentationInfo,
    VmaDefragmentationStats *     pDefragmentationStats );

/** \brief Binds buffer to allocation.

Binds specified buffer to region of memory represented by specified allocation.
Gets `VkDeviceMemory` handle and offset from the allocation.
If you want to create a buffer, allocate memory for it and bind them together separately,
you should use this function for binding instead of standard `vkBindBufferMemory()`,
because it ensures proper synchronization so that when a `VkDeviceMemory` object is used by multiple
allocations, calls to `vkBind*Memory()` or `vkMapMemory()` won't happen from multiple threads simultaneously
(which is illegal in Vulkan).

It is recommended to use function vmaCreateBuffer() instead of this one.
*/
VMA_CALL_PRE VkResult VMA_CALL_POST vmaBindBufferMemory(
    VmaAllocator  allocator,
    VmaAllocation allocation,
    VkBuffer      buffer );

/** \brief Binds buffer to allocation with additional parameters.

@param allocationLocalOffset Additional offset to be added while binding, relative to the beginnig of the `allocation`. Normally it should be 0.
@param pNext A chain of structures to be attached to `VkBindBufferMemoryInfoKHR` structure used internally. Normally it should be null.

This function is similar to vmaBindBufferMemory(), but it provides additional parameters.

If `pNext` is not null, #VmaAllocator object must have been created with #VMA_ALLOCATOR_CREATE_KHR_BIND_MEMORY2_BIT flag
or with VmaAllocatorCreateInfo::vulkanApiVersion `== VK_API_VERSION_1_1`. Otherwise the call fails.
*/
VMA_CALL_PRE VkResult VMA_CALL_POST vmaBindBufferMemory2(
    VmaAllocator  allocator,
    VmaAllocation allocation,
    VkDeviceSize  allocationLocalOffset,
    VkBuffer      buffer,
    const void *  pNext );

/** \brief Binds image to allocation.

Binds specified image to region of memory represented by specified allocation.
Gets `VkDeviceMemory` handle and offset from the allocation.
If you want to create an image, allocate memory for it and bind them together separately,
you should use this function for binding instead of standard `vkBindImageMemory()`,
because it ensures proper synchronization so that when a `VkDeviceMemory` object is used by multiple
allocations, calls to `vkBind*Memory()` or `vkMapMemory()` won't happen from multiple threads simultaneously
(which is illegal in Vulkan).

It is recommended to use function vmaCreateImage() instead of this one.
*/
VMA_CALL_PRE VkResult VMA_CALL_POST vmaBindImageMemory(
    VmaAllocator  allocator,
    VmaAllocation allocation,
    VkImage       image );

/** \brief Binds image to allocation with additional parameters.

@param allocationLocalOffset Additional offset to be added while binding, relative to the beginnig of the `allocation`. Normally it should be 0.
@param pNext A chain of structures to be attached to `VkBindImageMemoryInfoKHR` structure used internally. Normally it should be null.

This function is similar to vmaBindImageMemory(), but it provides additional parameters.

If `pNext` is not null, #VmaAllocator object must have been created with #VMA_ALLOCATOR_CREATE_KHR_BIND_MEMORY2_BIT flag
or with VmaAllocatorCreateInfo::vulkanApiVersion `== VK_API_VERSION_1_1`. Otherwise the call fails.
*/
VMA_CALL_PRE VkResult VMA_CALL_POST vmaBindImageMemory2(
    VmaAllocator  allocator,
    VmaAllocation allocation,
    VkDeviceSize  allocationLocalOffset,
    VkImage       image,
    const void *  pNext );

/**
@param[out] pBuffer Buffer that was created.
@param[out] pAllocation Allocation that was created.
@param[out] pAllocationInfo Optional. Information about allocated memory. It can be later fetched using function vmaGetAllocationInfo().

This function automatically:

-# Creates buffer.
-# Allocates appropriate memory for it.
-# Binds the buffer with the memory.

If any of these operations fail, buffer and allocation are not created,
returned value is negative error code, *pBuffer and *pAllocation are null.

If the function succeeded, you must destroy both buffer and allocation when you
no longer need them using either convenience function vmaDestroyBuffer() or
separately, using `vkDestroyBuffer()` and vmaFreeMemory().

If VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT flag was used,
VK_KHR_dedicated_allocation extension is used internally to query driver whether
it requires or prefers the new buffer to have dedicated allocation. If yes,
and if dedicated allocation is possible (VmaAllocationCreateInfo::pool is null
and VMA_ALLOCATION_CREATE_NEVER_ALLOCATE_BIT is not used), it creates dedicated
allocation for this buffer, just like when using
VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT.
*/
VMA_CALL_PRE VkResult VMA_CALL_POST vmaCreateBuffer(
    VmaAllocator                   allocator,
    const VkBufferCreateInfo *     pBufferCreateInfo,
    const VmaAllocationCreateInfo *pAllocationCreateInfo,
    VkBuffer *                     pBuffer,
    VmaAllocation *                pAllocation,
    VmaAllocationInfo *            pAllocationInfo );

/** \brief Destroys Vulkan buffer and frees allocated memory.

This is just a convenience function equivalent to:

\code
vkDestroyBuffer(device, buffer, allocationCallbacks);
vmaFreeMemory(allocator, allocation);
\endcode

It it safe to pass null as buffer and/or allocation.
*/
VMA_CALL_PRE void VMA_CALL_POST vmaDestroyBuffer(
    VmaAllocator  allocator,
    VkBuffer      buffer,
    VmaAllocation allocation );

/// Function similar to vmaCreateBuffer().
VMA_CALL_PRE VkResult VMA_CALL_POST vmaCreateImage(
    VmaAllocator                   allocator,
    const VkImageCreateInfo *      pImageCreateInfo,
    const VmaAllocationCreateInfo *pAllocationCreateInfo,
    VkImage *                      pImage,
    VmaAllocation *                pAllocation,
    VmaAllocationInfo *            pAllocationInfo );

/** \brief Destroys Vulkan image and frees allocated memory.

This is just a convenience function equivalent to:

\code
vkDestroyImage(device, image, allocationCallbacks);
vmaFreeMemory(allocator, allocation);
\endcode

It it safe to pass null as image and/or allocation.
*/
VMA_CALL_PRE void VMA_CALL_POST vmaDestroyImage(
    VmaAllocator  allocator,
    VkImage       image,
    VmaAllocation allocation );

#ifdef __cplusplus
}
#endif

#endif // AMD_VULKAN_MEMORY_ALLOCATOR_H

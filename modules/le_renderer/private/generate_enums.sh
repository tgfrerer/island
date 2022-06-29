#!/bin/bash -e

../../../scripts/codegen/gen_le_enums.py <<- "EOF" | clang-format > le_vk_enums.inl
# Vulkan enum name,         generate to_string()
VkAccessFlagBits
VkAttachmentLoadOp, yes
VkAttachmentStoreOp,        yes
VkBlendFactor, yes
VkBlendOp, yes
VkBorderColor, yes
VkBufferUsageFlagBits, yes
VkBuildAccelerationStructureFlagBitsKHR, yes
VkColorComponentFlagBits
VkCompareOp, yes
VkCullModeFlagBits
VkDescriptorType,
VkFilter, yes
VkFormat, yes
VkFrontFace, yes
VkImageCreateFlagBits
VkImageLayout, yes
VkImageTiling, yes
VkImageType, yes
VkImageUsageFlagBits, yes
VkImageViewType, yes
VkIndexType, yes
VkPipelineStageFlagBits2,
VkPolygonMode, yes
VkPrimitiveTopology, yes
VkQueueFlagBits, yes
VkSampleCountFlagBits
VkSamplerAddressMode, yes
VkSamplerMipmapMode, yes
VkShaderStageFlagBits, yes
VkStencilOp, yes
EOF

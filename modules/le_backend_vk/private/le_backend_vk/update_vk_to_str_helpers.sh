#!/bin/bash -e

../../../../scripts/codegen/gen_vk_enum_to_str.py <<- "EOF" | clang-format > vk_to_str_helpers.inl
# Vulkan enum name
VkAccessFlagBits2
VkBufferUsageFlagBits
VkFormat
VkImageLayout
VkImageUsageFlagBits
VkPipelineStageFlagBits2
VkQueueFlagBits
VkResult
EOF

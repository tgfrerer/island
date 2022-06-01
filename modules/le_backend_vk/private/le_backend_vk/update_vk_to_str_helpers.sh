#!/bin/bash -e

../../../../scripts/codegen/gen_vk_enum_to_str.py <<- "EOF" | clang-format > vk_to_str_helpers.inl
# Vulkan enum name
VkBufferUsageFlagBits
VkFormat
VkImageUsageFlagBits
VkResult
VkAccessFlagBits2
VkPipelineStageFlagBits2
VkImageLayout
EOF

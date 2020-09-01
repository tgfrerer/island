set (TARGET le_backend_vk)

add_compile_definitions(VK_ENABLE_BETA_EXTENSIONS=1) # we need this for raytracing features
add_compile_definitions(VULKAN_HPP_NO_SMART_HANDLE)
add_compile_definitions(VULKAN_HPP_DISABLE_IMPLICIT_RESULT_VALUE_CAST)

#add_compile_definitions(VMA_USE_STL_CONTAINERS=1)

set (SOURCES "le_backend_vk.cpp")
set (SOURCES ${SOURCES} "le_backend_vk.h")
set (SOURCES ${SOURCES} "le_backend_types_internal.h")
set (SOURCES ${SOURCES} "le_instance_vk.cpp")
set (SOURCES ${SOURCES} "le_pipeline.cpp")
set (SOURCES ${SOURCES} "le_device_vk.cpp")
set (SOURCES ${SOURCES} "le_allocator.cpp")

set (SOURCES ${SOURCES} "util/vk_mem_alloc/vk_mem_alloc.h")
set (SOURCES ${SOURCES} "util/vk_mem_alloc/vk_mem_alloc.cpp")

set (SOURCES ${SOURCES} "util/spirv-cross/GLSL.std.450.h")
set (SOURCES ${SOURCES} "util/spirv-cross/spirv_common.hpp")
set (SOURCES ${SOURCES} "util/spirv-cross/spirv.hpp")
set (SOURCES ${SOURCES} "util/spirv-cross/spirv_cross.hpp")
set (SOURCES ${SOURCES} "util/spirv-cross/spirv_cross.cpp")
set (SOURCES ${SOURCES} "util/spirv-cross/spirv_cfg.hpp")
set (SOURCES ${SOURCES} "util/spirv-cross/spirv_cfg.cpp")
set (SOURCES ${SOURCES} "util/spirv-cross/spirv_cross_parsed_ir.cpp")
set (SOURCES ${SOURCES} "util/spirv-cross/spirv_cross_parsed_ir.hpp")
set (SOURCES ${SOURCES} "util/spirv-cross/spirv_parser.hpp")
set (SOURCES ${SOURCES} "util/spirv-cross/spirv_parser.cpp")

set (SOURCES ${SOURCES} "${ISLAND_BASE_DIR}/3rdparty/src/spooky/SpookyV2.cpp")
set (SOURCES ${SOURCES} "${ISLAND_BASE_DIR}/3rdparty/src/spooky/SpookyV2.h")

if (${PLUGINS_DYNAMIC})

    add_library(${TARGET} SHARED ${SOURCES})

    
    add_dynamic_linker_flags()

    target_compile_definitions(${TARGET} PUBLIC "PLUGINS_DYNAMIC=1")

else()

    # We also set a compiler flag so that our main source file can use the correct
    # static code path to statically load the library function pointers.

    # Adding a static library means to also add a linker dependency for our target
    # to the library.
    add_static_lib( ${TARGET} )

    add_library(${TARGET} STATIC ${SOURCES})

endif()

IF (WIN32)
    set (LINKER_FLAGS vulkan-1)
ELSE()
    set (LINKER_FLAGS ${LINKER_FLAGS} -Wl,--whole-archive vulkan -Wl,--no-whole-archive )
    set (LINKER_FLAGS ${LINKER_FLAGS} stdc++fs)
ENDIF()    
    
target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})

source_group(${TARGET} FILES ${SOURCES})
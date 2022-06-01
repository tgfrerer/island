set (TARGET le_backend_vk)


# list modules this module depends on
depends_on_island_module(le_core)
depends_on_island_module(le_log)
depends_on_island_module(le_shader_compiler)
depends_on_island_module(le_file_watcher)
depends_on_island_module(le_window)
depends_on_island_module(le_swapchain_vk)
depends_on_island_module(le_renderer)

# add_compile_definitions(VMA_USE_STL_CONTAINERS=1)

add_compile_definitions(VULKAN_HPP_NO_SMART_HANDLE)
add_compile_definitions(VULKAN_HPP_DISABLE_IMPLICIT_RESULT_VALUE_CAST)


set (SOURCES "le_backend_vk.cpp")
set (SOURCES ${SOURCES} "le_backend_vk.h")
set (SOURCES ${SOURCES} "le_backend_vk_settings.inl")
set (SOURCES ${SOURCES} "le_backend_types_internal.h")
set (SOURCES ${SOURCES} "private/le_backend_vk/le_backend_types_pipeline.inl")
set (SOURCES ${SOURCES} "private/le_backend_vk/vk_to_str_helpers.inl")
set (SOURCES ${SOURCES} "le_instance_vk.cpp")
set (SOURCES ${SOURCES} "le_pipeline.cpp")
set (SOURCES ${SOURCES} "le_device_vk.cpp")
set (SOURCES ${SOURCES} "le_allocator.cpp")

set (SOURCES ${SOURCES} "util/vk_mem_alloc/vk_mem_alloc.h")
set (SOURCES ${SOURCES} "util/vk_mem_alloc/vk_mem_alloc.cpp")

set (SOURCES ${SOURCES} "util/spirv_reflect/spirv_reflect.h")
set (SOURCES ${SOURCES} "util/spirv_reflect/spirv_reflect.c")

set (SOURCES ${SOURCES} "${ISLAND_BASE_DIR}/3rdparty/src/spooky/SpookyV2.cpp")
set (SOURCES ${SOURCES} "${ISLAND_BASE_DIR}/3rdparty/src/spooky/SpookyV2.h")

if (${PLUGINS_DYNAMIC})
    add_library(${TARGET} SHARED ${SOURCES})
    add_dynamic_linker_flags()
    target_compile_definitions(${TARGET} PUBLIC "PLUGINS_DYNAMIC=1")
else()
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
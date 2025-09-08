set (TARGET le_backend_vk)


# list modules this module depends on
depends_on_island_module(le_core)
depends_on_island_module(le_log)
depends_on_island_module(le_shader_compiler)
depends_on_island_module(le_file_watcher)
depends_on_island_module(le_window)
depends_on_island_module(le_swapchain_vk)
depends_on_island_module(le_renderer)
depends_on_island_module(le_tracy)

add_compile_definitions(SPIRV_REFLECT_USE_SYSTEM_SPIRV_H)
add_compile_definitions(VK_NO_PROTOTYPES)


set (SOURCES "le_backend_vk.cpp")
set (SOURCES ${SOURCES} "le_backend_vk.h")
set (SOURCES ${SOURCES} "le_backend_vk_settings.inl")
set (SOURCES ${SOURCES} "le_backend_types_internal.h")
set (SOURCES ${SOURCES} "private/le_backend_vk/le_backend_types_pipeline.inl")
set (SOURCES ${SOURCES} "private/le_backend_vk/vk_to_str_helpers.inl")
set (SOURCES ${SOURCES} "private/le_backend_vk/le_command_stream_t.h")
set (SOURCES ${SOURCES} "le_instance_vk.cpp")
set (SOURCES ${SOURCES} "le_pipeline.cpp")
set (SOURCES ${SOURCES} "le_device_vk.cpp")
set (SOURCES ${SOURCES} "le_allocator.cpp")

set (SOURCES ${SOURCES} "3rdparty/le_backend_vk/spirv_reflect/spirv_reflect.h")
set (SOURCES ${SOURCES} "3rdparty/le_backend_vk/spirv_reflect/spirv_reflect.c")

set (SOURCES ${SOURCES} "3rdparty/le_backend_vk/volk/volk.h")
set (SOURCES ${SOURCES} "3rdparty/le_backend_vk/volk/volk.c")

set (SOURCES ${SOURCES} "${ISLAND_BASE_DIR}/3rdparty/src/spooky/SpookyV2.cpp")
set (SOURCES ${SOURCES} "${ISLAND_BASE_DIR}/3rdparty/src/spooky/SpookyV2.h")

set (VK_MEM_ALLOC_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/le_backend_vk//vma/vk_mem_alloc.h")
set (VK_MEM_ALLOC_SOURCES ${VK_MEM_ALLOC_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/le_backend_vk/vma/vk_mem_alloc.cpp")

if (${PLUGINS_DYNAMIC})
    add_library(${TARGET} SHARED ${SOURCES})
    add_dynamic_linker_flags()
    target_compile_definitions(${TARGET} PUBLIC "PLUGINS_DYNAMIC")
else()
    # If we're creating a static library, we want to compile vma into the main static
    # library - there is no point in creating a separate library for vma.
    set (SOURCES ${VK_MEM_ALLOC_SOURCES} ${SOURCES})
    add_static_lib( ${TARGET} )
    add_library(${TARGET} STATIC ${SOURCES})
endif()

IF (WIN32)
ELSE()
    set (LINKER_FLAGS ${LINKER_FLAGS} stdc++fs)
ENDIF()    
    
target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})

if (${PLUGINS_DYNAMIC})
    # If we're hot-reloading we want to compile vma as a separate library. This is so that we can
    # keep it loaded persistently so that it does not interfere with hot-reloading the backend module.
    # It could interfere otherwise because it seems to use virtual inheritance in c++ objects, and the
    # v-tables of these objects can not be automatically updated on hot-reload.
    add_library(vma SHARED ${VK_MEM_ALLOC_SOURCES})
    target_link_libraries(${TARGET} PRIVATE vma )
endif()


source_group(${TARGET} FILES ${SOURCES})

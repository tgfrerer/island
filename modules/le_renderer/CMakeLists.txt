set (TARGET le_renderer)

# list modules this module depends on
depends_on_island_module(le_backend_vk)
depends_on_island_module(le_swapchain_vk)
depends_on_island_module(le_jobs)
depends_on_island_module(le_log)
depends_on_island_module(le_tracy)
depends_on_island_module(le_debug_print_text)


set (SOURCES "le_renderer.cpp")
set (SOURCES ${SOURCES} "le_renderer.h")
set (SOURCES ${SOURCES} "le_renderer.hpp")
set (SOURCES ${SOURCES} "private/le_renderer/le_renderer_types.h")
set (SOURCES ${SOURCES} "private/le_renderer/le_vk_enums.inl")
set (SOURCES ${SOURCES} "private/le_renderer/le_resource_handle_t.inl")
set (SOURCES ${SOURCES} "private/le_renderer/le_rendergraph.h")
set (SOURCES ${SOURCES} "le_rendergraph.cpp")
set (SOURCES ${SOURCES} "le_command_buffer_encoder.cpp")

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

target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})

source_group(${TARGET} FILES ${SOURCES})

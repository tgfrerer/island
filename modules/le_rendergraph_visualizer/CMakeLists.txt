set (TARGET le_rendergraph_visualizer)

# list modules this module depends on
depends_on_island_module(le_log)
depends_on_island_module(le_debug_print_text)
depends_on_island_module(le_renderer)
depends_on_island_module(le_2d)
depends_on_island_module(le_path)
depends_on_island_module(le_pipeline_builder)
depends_on_island_module(le_shader_compiler)
depends_on_island_module(le_font)
depends_on_island_module(le_ui_event)
depends_on_island_module(le_tracy)

set (SOURCES "le_rendergraph_visualizer.cpp")
set (SOURCES ${SOURCES} "le_rendergraph_visualizer.h")
set (SOURCES ${SOURCES} "private/le_rendergraph_visualizer/views.h" )
set (SOURCES ${SOURCES} "private/le_rendergraph_visualizer/views.cpp")

if (${PLUGINS_DYNAMIC})
    add_library(${TARGET} SHARED ${SOURCES})
    add_dynamic_linker_flags()
    target_compile_definitions(${TARGET}  PUBLIC "PLUGINS_DYNAMIC")
else()
    add_library(${TARGET} STATIC ${SOURCES})
    add_static_lib( ${TARGET} )
endif()

target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})

# allow the rendergraph visualizer to bring his own local resources
link_resources("${CMAKE_CURRENT_SOURCE_DIR}/resources" "${PROJECT_SOURCE_DIR}/resources/rendergraph_visualizer")

source_group(${TARGET} FILES ${SOURCES})

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

option(LE_RENDERGRAPH_VISUALIZER_SHADER_DEVELOPMENT off "Copy shaders into local resource directory for development.")
if (${LE_RENDERGRAPH_VISUALIZER_SHADER_DEVELOPMENT})
    target_compile_definitions(${TARGET} PUBLIC "LE_RENDERGRAPH_VISUALIZER_SHADERS_FROM_SOURCE=1")
    # link resources from module resource directory into app resource directory
    # this is mainly useful working on the shaders
    link_resources("${CMAKE_CURRENT_SOURCE_DIR}/private/le_rendergraph_visualizer/resources/glsl" "${PROJECT_SOURCE_DIR}/resources/rendergraph_visualizer_dev")
endif()

source_group(${TARGET} FILES ${SOURCES})

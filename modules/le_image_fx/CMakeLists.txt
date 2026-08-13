set (TARGET le_image_fx)

# list modules this module depends on
depends_on_island_module(le_renderer)
depends_on_island_module(le_pipeline_builder)
depends_on_island_module(le_log)

set (SOURCES "le_image_fx.cpp")
set (SOURCES ${SOURCES} "le_image_fx.h")

if (${PLUGINS_DYNAMIC})
    add_library(${TARGET} SHARED ${SOURCES})
    add_dynamic_linker_flags()
    target_compile_definitions(${TARGET}  PUBLIC "PLUGINS_DYNAMIC")
else()
    add_library(${TARGET} STATIC ${SOURCES})
    add_static_lib( ${TARGET} )
endif()


# allow the rendergraph visualizer to bring his own local resources
link_resources("${CMAKE_CURRENT_SOURCE_DIR}/private/le_image_fx/resources" "${PROJECT_SOURCE_DIR}/resources/le_image_fx_dev")

target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})
source_group(${TARGET} FILES ${SOURCES})

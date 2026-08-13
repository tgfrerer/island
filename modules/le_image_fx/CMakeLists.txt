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

option(LE_IMAGE_FX_SHADER_DEVELOPMENT off "Copy shaders into local resource directory for development.")

if (${LE_IMAGE_FX_SHADER_DEVELOPMENT})
    target_compile_definitions(${TARGET} PUBLIC "LE_IMAGE_FX_SHADERS_FROM_SOURCE=1")
    # link resources from module resource directory into app resource directory
    # this is mainly useful working on the shaders
    link_resources("${CMAKE_CURRENT_SOURCE_DIR}/private/le_image_fx/resources" "${PROJECT_SOURCE_DIR}/resources/le_image_fx")
endif()

target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})
source_group(${TARGET} FILES ${SOURCES})

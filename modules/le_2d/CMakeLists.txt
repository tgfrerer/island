set (TARGET le_2d)

# list modules this module depends on
depends_on_island_module(le_renderer)
depends_on_island_module(le_log)
depends_on_island_module(le_pipeline_builder)

set (SOURCES "le_2d.h")
set (SOURCES ${SOURCES} "le_2d.cpp")
set (SOURCES ${SOURCES} "le_2d_encoder.cpp")
set (SOURCES ${SOURCES} "./private/le_2d/SpookyV2.cpp")
set (SOURCES ${SOURCES} "./private/le_2d/SpookyV2.h")
set (SOURCES ${SOURCES} "./public/le_2d/le_transform_2d.hpp")

if (${PLUGINS_DYNAMIC})
    add_library(${TARGET} SHARED ${SOURCES})
    add_dynamic_linker_flags()
    target_compile_definitions(${TARGET}  PUBLIC "PLUGINS_DYNAMIC")
else()
    add_library(${TARGET} STATIC ${SOURCES})
    add_static_lib( ${TARGET} )
endif()

target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})

source_group(${TARGET} FILES ${SOURCES})

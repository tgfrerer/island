set (TARGET le_debug_print_text)

# list modules this module depends on
depends_on_island_module(le_renderer)
depends_on_island_module(le_pipeline_builder)
depends_on_island_module(le_log)

set (SOURCES "le_debug_print_text.cpp")
set (SOURCES ${SOURCES} "le_debug_print_text.h")

if (${PLUGINS_DYNAMIC})
    add_library(${TARGET} SHARED ${SOURCES})
    add_dynamic_linker_flags()
    target_compile_definitions(${TARGET}  PUBLIC "PLUGINS_DYNAMIC")
else()
    add_library(${TARGET} STATIC ${SOURCES})
    add_static_lib( ${TARGET} )
endif()

# set (LINKER_FLAGS ${LINKER_FLAGS} stdc++fs)

target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})

source_group(${TARGET} FILES ${SOURCES})

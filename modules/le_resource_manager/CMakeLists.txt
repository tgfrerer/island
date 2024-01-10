# list modules this module depends on
depends_on_island_module(le_renderer)
depends_on_island_module(le_pixels)
depends_on_island_module(le_file_watcher)
depends_on_island_module(le_log)

set (TARGET le_resource_manager)

set (SOURCES "le_resource_manager.cpp")
set (SOURCES ${SOURCES} "le_resource_manager.h")

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

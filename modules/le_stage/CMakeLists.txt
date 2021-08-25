set (TARGET le_stage)

# list modules this module depends on
depends_on_island_module(le_log)
depends_on_island_module(le_renderer)
depends_on_island_module(le_camera)
depends_on_island_module(le_timebase)
depends_on_island_module(le_pixels)

set (SOURCES "le_stage.cpp")
set (SOURCES ${SOURCES} "le_stage.h")
set (SOURCES ${SOURCES} "le_stage_types.h")

if (${PLUGINS_DYNAMIC})

    add_library(${TARGET} SHARED ${SOURCES})

    
    add_dynamic_linker_flags()
    
    target_compile_definitions(${TARGET}  PUBLIC "PLUGINS_DYNAMIC")

else()

    add_library(${TARGET} STATIC ${SOURCES})

    set (STATIC_LIBS ${STATIC_LIBS} ${TARGET} PARENT_SCOPE)

endif()

# set (LINKER_FLAGS ${LINKER_FLAGS} stdc++fs)

target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})
source_group(${TARGET} FILES ${SOURCES})

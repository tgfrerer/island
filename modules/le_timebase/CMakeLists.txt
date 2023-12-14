set (TARGET le_timebase)

# list modules this module depends on
# depends_on_island_module(le_path)

set (SOURCES "le_timebase.cpp")
set (SOURCES ${SOURCES} "le_timebase.h")
set (SOURCES ${SOURCES} "private/le_timebase/le_timebase_ticks_type.h")

if (${PLUGINS_DYNAMIC})

    add_library(${TARGET} SHARED ${SOURCES})

    
    add_dynamic_linker_flags()
    
    target_compile_definitions(${TARGET}  PUBLIC "PLUGINS_DYNAMIC")

else()

    add_library(${TARGET} STATIC ${SOURCES})

    add_static_lib(${TARGET})

endif()

# set (LINKER_FLAGS ${LINKER_FLAGS} stdc++fs)

target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})
source_group(${TARGET} FILES ${SOURCES})

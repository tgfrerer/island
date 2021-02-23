set ( TARGET le_file_watcher )

set ( SOURCES "le_file_watcher.h" )
set ( SOURCES ${SOURCES} "le_file_watcher_win32.cpp" )
set ( SOURCES ${SOURCES} "le_file_watcher_linux.cpp" )

if (${PLUGINS_DYNAMIC})
    set ( SOURCES ${SOURCES} "le_tweakable.h" )
    add_library(${TARGET} SHARED ${SOURCES})
    add_dynamic_linker_flags()
    target_compile_definitions(${TARGET} PUBLIC "PLUGINS_DYNAMIC=1")
else()
    add_static_lib( ${TARGET} )
    add_library(${TARGET} STATIC ${SOURCES})
endif()

if ( WIN32 )
else()
    target_link_libraries(${TARGET} stdc++fs)
endif()

target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})

source_group(${TARGET} FILES ${SOURCES})
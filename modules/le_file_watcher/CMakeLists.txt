set ( TARGET le_file_watcher )

set ( SOURCES "le_file_watcher.h" )
set ( SOURCES ${SOURCES} "le_file_watcher_win32.cpp" )
set ( SOURCES ${SOURCES} "le_file_watcher_linux.cpp" )
set ( SOURCES ${SOURCES} "le_tweakable.h" )

add_static_lib( ${TARGET} )
add_library(${TARGET} STATIC ${SOURCES})

if ( WIN32 )
else()
    target_link_libraries(${TARGET} stdc++fs)
endif()

target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})

source_group(${TARGET} FILES ${SOURCES})
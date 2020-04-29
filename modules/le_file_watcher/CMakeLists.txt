set ( TARGET le_file_watcher )

set ( SOURCES le_file_watcher.cpp )
set ( SOURCES ${SOURCES} le_file_watcher.h )

if (${PLUGINS_DYNAMIC})
    set ( SOURCES ${SOURCES} le_tweakable.h )
    set (PLUGIN_LIBS_DEPENDENCIES ${PLUGIN_LIBS_DEPENDENCIES} ${TARGET} PARENT_SCOPE)
    add_library(${TARGET} SHARED ${SOURCES} )
else()
    set (STATIC_LIBS ${STATIC_LIBS} ${TARGET} PARENT_SCOPE)
    add_library(${TARGET} STATIC ${SOURCES})
endif()

target_link_libraries(${TARGET} stdc++fs)

source_group(${TARGET} FILES ${SOURCES})
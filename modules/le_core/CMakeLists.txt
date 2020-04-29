set ( TARGET le_core )

set ( SOURCES le_api_loader.cpp )
set ( SOURCES ${SOURCES} le_api_loader.h )
set ( SOURCES ${SOURCES} le_core.cpp )
set ( SOURCES ${SOURCES} le_core.h )
set ( SOURCES ${SOURCES} hash_util.h )
set (SOURCES ${SOURCES} "${ISLAND_BASE_DIR}/3rdparty/src/spooky/SpookyV2.cpp")
set (SOURCES ${SOURCES} "${ISLAND_BASE_DIR}/3rdparty/src/spooky/SpookyV2.h")

if (${PLUGINS_DYNAMIC})

    add_library(${TARGET} SHARED ${SOURCES})

    # IMPORTANT: --no-gnu-unique
    #            This compiler flag is neccesary as otherwise the library may get compiled
    #            with some symbols exported as UNIQUE, which implicitly makes this library
    #            un-closeable. This means, any calls to `dlclose` on this library, once
    #            loaded, will have no effect, and autoreload for this library will not work
    #            as the first version of the library will remain resident.
    target_compile_options (${TARGET} PUBLIC --no-gnu-unique)

    target_compile_definitions(${TARGET} PUBLIC "PLUGINS_DYNAMIC=1")

else()

    add_library(${TARGET} STATIC ${SOURCES})
    add_static_lib( ${TARGET} )

endif()

target_link_libraries(${TARGET} PRIVATE le_file_watcher)
target_link_libraries(${TARGET} PUBLIC ${CMAKE_DL_LIBS})

set (PLUGIN_LIBS_DEPENDENCIES ${PLUGIN_LIBS_DEPENDENCIES} le_file_watcher PARENT_SCOPE)
set (PLUGIN_LIBS_DEPENDENCIES ${PLUGIN_LIBS_DEPENDENCIES} le_core PARENT_SCOPE)

source_group(${TARGET} FILES ${SOURCES})
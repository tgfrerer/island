
set ( SOURCES ApiLoader.cpp )
set ( SOURCES ${SOURCES} ApiRegistry.cpp )

if (${PLUGINS_DYNAMIC})

    add_library(pal_api_loader SHARED ${SOURCES})
    target_link_libraries(pal_api_loader PUBLIC dl)

else()

    add_library(pal_api_loader STATIC ${SOURCES})
    set (STATIC_LIBS ${STATIC_LIBS} pal_api_loader PARENT_SCOPE)
    target_compile_definitions(pal_api_loader PUBLIC "PLUGIN_PAL_WINDOW_STATIC")


endif()

target_link_libraries(pal_api_loader PRIVATE pal_file_watcher)

set (PLUGIN_LIBS_DEPENDENCIES ${PLUGIN_LIBS_DEPENDENCIES} pal_file_watcher PARENT_SCOPE)
set (PLUGIN_LIBS_DEPENDENCIES ${PLUGIN_LIBS_DEPENDENCIES} pal_api_loader PARENT_SCOPE)




# Adding a static library means to also add a linker dependency for our target
# to the library.


if (${PLUGINS_DYNAMIC})
    set (PLUGIN_LIBS_DEPENDENCIES ${PLUGIN_LIBS_DEPENDENCIES} file_watcher PARENT_SCOPE)
    add_library(file_watcher SHARED file_watcher.cpp)
else()
    set (STATIC_LIBS ${STATIC_LIBS} file_watcher PARENT_SCOPE)
    add_library(file_watcher STATIC file_watcher.cpp)
endif()

target_link_libraries(file_watcher stdc++fs)

if (${PLUGINS_DYNAMIC})
    set (PLUGIN_LIBS_DEPENDENCIES ${PLUGIN_LIBS_DEPENDENCIES} pal_file_watcher PARENT_SCOPE)
    add_library(pal_file_watcher SHARED pal_file_watcher.cpp pal_file_watcher.h)
else()
    set (STATIC_LIBS ${STATIC_LIBS} pal_file_watcher PARENT_SCOPE)
    add_library(pal_file_watcher STATIC pal_file_watcher.cpp pal_file_watcher.h)
endif()

target_link_libraries(pal_file_watcher stdc++fs)

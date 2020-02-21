if (${PLUGINS_DYNAMIC})
    set (PLUGIN_LIBS_DEPENDENCIES ${PLUGIN_LIBS_DEPENDENCIES} le_file_watcher PARENT_SCOPE)
    add_library(le_file_watcher SHARED le_file_watcher.cpp le_file_watcher.h le_tweakable.h)
else()
    set (STATIC_LIBS ${STATIC_LIBS} le_file_watcher PARENT_SCOPE)
    add_library(le_file_watcher STATIC le_file_watcher.cpp le_file_watcher.h)
endif()

target_link_libraries(le_file_watcher stdc++fs)

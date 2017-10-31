
# Adding a static library means to also add a linker dependency for our target
# to the library.
set (STATIC_LIBS ${STATIC_LIBS} file_watcher PARENT_SCOPE)

add_library(file_watcher STATIC file_watcher.cpp)
target_link_libraries(file_watcher stdc++fs)

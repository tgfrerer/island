cmake_minimum_required(VERSION 3.7.2) 
set (CMAKE_CXX_STANDARD 17)

project (Experiment)

set(PLUGINS_DYNAMIC ON CACHE BOOL "Use dynamic linking for plugins" FORCE)

set ( STATIC_LIBS "")
set ( PLUGIN_DEFINITIONS "")
set ( PLUGIN_LIBS_DEPENDENCIES "")

include_directories ("${PROJECT_SOURCE_DIR}")

add_subdirectory (pal_api_loader)
add_subdirectory (pal_file_watcher)
add_subdirectory (pal_window)
add_subdirectory (pal_backend_vk)

set (SOURCES main.cpp)

add_executable (Experiment ${SOURCES})

# add -D definitions so that source code will know whether plugins are linked
# statically or dynamically
target_compile_definitions(Experiment PUBLIC ${PLUGIN_DEFINITIONS})

# we need to link in the dynamic linking library so we can use dynamic linking
target_link_libraries(Experiment PUBLIC dl)
target_link_libraries(Experiment PRIVATE ${PLUGIN_LIBS_DEPENDENCIES})
target_link_libraries(Experiment PRIVATE ${STATIC_LIBS})


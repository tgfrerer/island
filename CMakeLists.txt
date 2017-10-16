cmake_minimum_required(VERSION 3.7.2) 
set (CMAKE_CXX_STANDARD 14)

project (Experiment)

set(PLUGINS_DYNAMIC ON CACHE BOOL "Use dynamic linking for plugins" FORCE)

set ( STATIC_LIBS "")
set ( PLUGIN_DEFINITIONS "")

include_directories ("${PROJECT_SOURCE_DIR}")

add_subdirectory (state_machine)
add_subdirectory (file_watcher)

# set (EXTRA_LIBS ${EXTRA_LIBS} Accumulator)

set (SOURCES main.cpp)
set (SOURCES ${SOURCES} "loader/ApiLoader.cpp")

add_executable (Experiment ${SOURCES})

# add -D definitions so that source code will know whether plugins are linked
# statically or dynamically
target_compile_definitions(Experiment PUBLIC ${PLUGIN_DEFINITIONS})

# we need to link in the dynamic linking library so we can use dynamic linking
target_link_libraries(Experiment dl)
target_link_libraries(Experiment ${STATIC_LIBS})


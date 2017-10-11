cmake_minimum_required(VERSION 3.7.2) 
set (CMAKE_CXX_STANDARD 14)

project (Experiment)


include_directories ("${PROJECT_SOURCE_DIR}")

add_subdirectory (accum)
add_subdirectory (state_machine)
add_subdirectory (file_watcher)
# /add_subdirectory (loader) 
# set (EXTRA_LIBS ${EXTRA_LIBS} Accumulator)

set (SOURCES main.cpp)
set (SOURCES ${SOURCES} "loader/ApiLoader.cpp")

add_executable (Experiment ${SOURCES})

# we need to link in the dynamic linking library so we can use dynamic linking
target_link_libraries(Experiment dl)

# The following is needed for gcc filesystem
# target_link_libraries(Experiment stdc++fs)

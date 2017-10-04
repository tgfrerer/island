cmake_minimum_required(VERSION 3.7.2) 
set (CMAKE_CXX_STANDARD 14)

project (Experiment)


include_directories ("${PROJECT_SOURCE_DIR}")

add_subdirectory (accum)
add_subdirectory (state_machine)
# /add_subdirectory (loader) 
# set (EXTRA_LIBS ${EXTRA_LIBS} Accumulator)

set (SOURCES main.cpp)
set (SOURCES ${SOURCES} "loader/ApiLoader.cpp")

add_executable (Experiment ${SOURCES})

# target_link_libraries (Experiment accumulator )
target_link_libraries(Experiment dl)

# the following is needed for gcc filesystem
target_link_libraries(Experiment stdc++fs)

cmake_minimum_required(VERSION 3.7.2) 

project (Experiment)

include_directories ("${PROJECT_SOURCE_DIR}")

add_subdirectory (accum) 
# set (EXTRA_LIBS ${EXTRA_LIBS} Accumulator)

add_executable (Experiment main.cpp)
target_link_libraries (Experiment accumulator )
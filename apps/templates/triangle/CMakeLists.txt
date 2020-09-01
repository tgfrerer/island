cmake_minimum_required(VERSION 3.7.2)
set (CMAKE_CXX_STANDARD 17)

set (PROJECT_NAME "Island-Triangle")

# Set global property (all targets are impacted)
set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CMAKE_COMMAND} -E time")
set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK "${CMAKE_COMMAND} -E time")

project (${PROJECT_NAME})

# set to number of worker threads if you wish to use multi-threaded rendering
# add_compile_definitions( LE_MT=4 )

# Point this to the base directory of your Island installation
set (ISLAND_BASE_DIR "${PROJECT_SOURCE_DIR}/../../../")

# Select which standard Island modules to use
set(REQUIRES_ISLAND_LOADER ON )
set(REQUIRES_ISLAND_CORE ON )

# Loads Island framework, based on selected Island modules from above
include ("${ISLAND_BASE_DIR}CMakeLists.txt.island_prolog.in")

# Add application module, and (optional) any other private
# island modules which should not be part of the shared framework.
add_subdirectory (triangle_app)

# Specify any optional modules from the standard framework here
add_island_module(le_camera)

# Main application c++ file. Not much to see there,
set (SOURCES main.cpp)

# Sets up Island framework linkage and housekeeping, based on user selections
include ("${ISLAND_BASE_DIR}CMakeLists.txt.island_epilog.in")

# (optional) create a link to local resources
link_resources(${PROJECT_SOURCE_DIR}/resources ${CMAKE_BINARY_DIR}/local_resources)
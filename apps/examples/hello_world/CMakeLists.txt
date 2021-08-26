cmake_minimum_required(VERSION 3.7.2)
set (CMAKE_CXX_STANDARD 17)

set (PROJECT_NAME "Island-HelloWorld")

# Set global property (all targets are impacted)
# set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CMAKE_COMMAND} -E time")
# set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK "${CMAKE_COMMAND} -E time")

project (${PROJECT_NAME})

# set to number of worker threads if you wish to use multi-threaded rendering
# add_compile_definitions( LE_MT=4 )

# Vulkan Validation layers are enabled by default for Debug builds.
# Uncomment the next line to disable loading Vulkan Validation Layers.
# add_compile_definitions( SHOULD_USE_VALIDATION_LAYERS=false )

# Point this to the base directory of your Island installation
set (ISLAND_BASE_DIR "${PROJECT_SOURCE_DIR}/../../../")

# Select which standard Island modules to use
set(REQUIRES_ISLAND_LOADER ON )
set(REQUIRES_ISLAND_CORE ON )

# Loads Island framework, based on selected Island modules from above
include ("${ISLAND_BASE_DIR}/CMakeLists.txt.island_prolog.in")

# Add custom module search paths
# add_island_module_location(${PROJECT_SOURCE_DIR}/../../modules)

# Main application c++ file. Not much to see there,
set (SOURCES main.cpp)

# Specify any used modules here - you may reference any module 
# found in the default Island modules/ directory, or found in any 
# directories you specified via `add_island_module_location` above.
#
depends_on_island_module(le_window)
depends_on_island_module(le_ui_event)
depends_on_island_module(le_renderer)
depends_on_island_module(le_pipeline_builder)
depends_on_island_module(le_camera)

depends_on_island_module(le_mesh)
depends_on_island_module(le_mesh_generator)
depends_on_island_module(le_resource_manager)
depends_on_island_module(le_pixels)

# Add application module, and (optional) any other private
# island modules which should not be part of the shared framework.
add_subdirectory (hello_world_app)

# Sets up Island framework linkage and housekeeping, based on user selections
include ("${ISLAND_BASE_DIR}/CMakeLists.txt.island_epilog.in")

# create a link to local resources
link_resources(${PROJECT_SOURCE_DIR}/resources ${CMAKE_BINARY_DIR}/local_resources)

set_target_properties(${PROJECT_NAME} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

source_group(${PROJECT_NAME} FILES ${SOURCES})
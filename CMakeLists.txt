cmake_minimum_required(VERSION 3.7.2)
set (CMAKE_CXX_STANDARD 17)

set (PROJECT_NAME "Island")

project (${PROJECT_NAME})

set(PLUGINS_DYNAMIC ON CACHE BOOL "Use dynamic linking for all plugins")

set ( STATIC_LIBS "")

set ( PLUGIN_DEFINITIONS "")
set ( PLUGIN_LIBS_DEPENDENCIES "")
set ( PLUGIN_LINK_DIRS "")

# Download and unpack glm at configure time ... there might be a time delay.
configure_file(CMakeLists.txt.in ${PROJECT_SOURCE_DIR}/libs/cmake/glm/download/CMakeLists.txt)
execute_process(COMMAND "${CMAKE_COMMAND}" -G "${CMAKE_GENERATOR}" .
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/libs/cmake/glm/download" )
execute_process(COMMAND "${CMAKE_COMMAND}" --build .
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/libs/cmake/glm/download" )

# create a link to shared shader repository so that we're always working with the same shaders.
execute_process(COMMAND ln -sfn ${PROJECT_SOURCE_DIR}/shaders ${CMAKE_BINARY_DIR}/shaders )


include_directories ("${PROJECT_SOURCE_DIR}")

add_subdirectory (test_app)

add_subdirectory (pal_api_loader)
add_subdirectory (pal_file_watcher)
add_subdirectory (pal_window)
add_subdirectory (le_backend_vk)
add_subdirectory (le_swapchain_vk)
add_subdirectory (le_renderer)
add_subdirectory (le_shader_compiler)

set (SOURCES main.cpp)

# NOTE: provide paths for libraries which are used by plugins, but not available via system search paths
# these were set via PARENT_SCOPE from the plugin CMake files.
link_directories(${PLUGIN_LINK_DIRS})

add_executable (${PROJECT_NAME} ${SOURCES})

# add -D definitions so that source code will know whether plugins are linked
# statically or dynamically
target_compile_definitions(${PROJECT_NAME} PUBLIC ${PLUGIN_DEFINITIONS})



# we need to link in the dynamic linking library so we can use dynamic linking
target_link_libraries(${PROJECT_NAME} PUBLIC dl)
target_link_libraries(${PROJECT_NAME} PRIVATE ${PLUGIN_LIBS_DEPENDENCIES})
target_link_libraries(${PROJECT_NAME} PRIVATE ${STATIC_LIBS})


cmake_minimum_required(VERSION 3.7.2) 
set (CMAKE_CXX_STANDARD 17)

set (PROJECT_NAME "Island")

project (${PROJECT_NAME})

set(PLUGINS_DYNAMIC OFF CACHE BOOL "Use dynamic linking for all plugins")

set ( STATIC_LIBS "")
set ( PLUGIN_DEFINITIONS "")
set ( PLUGIN_LIBS_DEPENDENCIES "")

include_directories ("${PROJECT_SOURCE_DIR}")

add_subdirectory (test_app)

add_subdirectory (pal_api_loader)
add_subdirectory (pal_file_watcher)
add_subdirectory (pal_window)
add_subdirectory (le_backend_vk)
add_subdirectory (le_swapchain_vk)
add_subdirectory (le_renderer)

set (SOURCES main.cpp)

add_executable (${PROJECT_NAME} ${SOURCES})

# add -D definitions so that source code will know whether plugins are linked
# statically or dynamically
target_compile_definitions(${PROJECT_NAME} PUBLIC ${PLUGIN_DEFINITIONS})

# we need to link in the dynamic linking library so we can use dynamic linking
target_link_libraries(${PROJECT_NAME} PUBLIC dl)
target_link_libraries(${PROJECT_NAME} PRIVATE ${PLUGIN_LIBS_DEPENDENCIES})
target_link_libraries(${PROJECT_NAME} PRIVATE ${STATIC_LIBS})


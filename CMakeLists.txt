cmake_minimum_required(VERSION 3.7.2)
set (CMAKE_CXX_STANDARD 17)

set (PROJECT_NAME "Island")

project (${PROJECT_NAME})

set(PLUGINS_DYNAMIC ON CACHE BOOL "Use dynamic linking for all plugins")

set ( STATIC_LIBS "")

set ( PLUGIN_DEFINITIONS "")
set ( PLUGIN_LIBS_DEPENDENCIES "")
set ( PLUGIN_LINK_DIRS "")

# specify output directory
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/modules)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/modules)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})


configure_file(CMakeLists.txt.imgui.build ${PROJECT_SOURCE_DIR}/3rdparty/src/imgui/CMakeLists.txt COPYONLY)

execute_process(
	COMMAND mkdir -p 3rdparty/src/imgui/build
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" 
    )

execute_process(
	COMMAND "${CMAKE_COMMAND}" -G "${CMAKE_GENERATOR}" -DCMAKE_INSTALL_PREFIX=${PROJECT_SOURCE_DIR}/3rdparty -DPLUGINS_DYNAMIC=${PLUGINS_DYNAMIC} ..
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/3rdparty/src/imgui/build" 
    )

execute_process(
	COMMAND "${CMAKE_COMMAND}" --build . --target install 
	WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/3rdparty/src/imgui/build"
	)

link_directories(${PROJECT_SOURCE_DIR}/3rdparty/libs/imgui)



# link glm source directory into includes directory
execute_process(COMMAND ln -sfn ${PROJECT_SOURCE_DIR}/3rdparty/src/glm/glm ${PROJECT_SOURCE_DIR}/3rdparty/include/glm )
# add glm include directory to includes search path - this is because glm's internals require glm's include directory to be globally accessible :(
include_directories(${PROJECT_SOURCE_DIR}/3rdparty/include/glm)

# create a link to shared shader repository so that we're always working with the same shaders.
execute_process(COMMAND ln -sfn ${PROJECT_SOURCE_DIR}/3rdparty/src/fx-gltf/include/fx ${PROJECT_SOURCE_DIR}/3rdparty/include/fx )


# include imgui directory
include_directories(${PROJECT_SOURCE_DIR}/3rdparty/include)

# create a link to 3rd party library binaries so that app can find them.
execute_process(COMMAND ln -sfn ${PROJECT_SOURCE_DIR}/3rdparty/src/json/single_include/nlohmann ${PROJECT_SOURCE_DIR}/3rdparty/include/nlohmann )

# create a link to shared resources
execute_process(COMMAND ln -sfn ${PROJECT_SOURCE_DIR}/resources ${CMAKE_BINARY_DIR}/resources )

# create a link to 3rd party library binaries so that app can find them.
execute_process(COMMAND ln -sfn ${PROJECT_SOURCE_DIR}/3rdparty/libs ${CMAKE_BINARY_DIR}/libs )




# Check if a local copy of the Vulkan SDK exists, and the environment VULKAN_SDK points to it.
# If yes, use this directory for header includes, and linking
set (VULKAN_SDK_ENV $ENV{VULKAN_SDK})
if (EXISTS ${VULKAN_SDK_ENV})

    #include vulkan from SDK installation directory
    include_directories(${VULKAN_SDK_ENV}/include/)

    # NOTE: We're linking Vulkan from this system's Vulkan SDK directory
    link_directories(${VULKAN_SDK_ENV}/lib)

endif(EXISTS ${VULKAN_SDK_ENV})

include_directories ("${PROJECT_SOURCE_DIR}")

add_subdirectory (test_app)

add_subdirectory (pal_api_loader)
add_subdirectory (pal_file_watcher)
add_subdirectory (pal_window)
add_subdirectory (le_backend_vk)
add_subdirectory (le_swapchain_vk)
add_subdirectory (le_renderer)
add_subdirectory (le_shader_compiler)
add_subdirectory (le_gltf_loader)


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


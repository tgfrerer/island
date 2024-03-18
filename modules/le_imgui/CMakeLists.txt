set (TARGET le_imgui)

depends_on_island_module(le_pipeline_builder)
depends_on_island_module(le_renderer)
depends_on_island_module(le_ui_event)


# n.b. configure_file will replace any cmake variables it finds in the file with their values.    
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/imgui/LICENSE.txt")
	configure_file("CMakeLists.txt.imgui.in" "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/imgui/CMakeLists.txt" @ONLY)
	configure_file("imconfig.h" "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/imgui/imconfig.h" COPYONLY)
else()
	message( FATAL_ERROR "\nImgui git submodule not found. \nHint: Run `git submodule init; git submodule update`; first...")
endif()

add_subdirectory( 3rdparty/imgui )

set (SOURCES "le_imgui.cpp")
set (SOURCES ${SOURCES} "le_imgui.h")

if (${PLUGINS_DYNAMIC})
    add_library(${TARGET} SHARED ${SOURCES})
    add_dynamic_linker_flags()
    target_compile_definitions(${TARGET} PUBLIC "PLUGINS_DYNAMIC")
else()
    add_library(${TARGET} STATIC ${SOURCES})
    # link against static version of imgui library
    target_link_libraries(${TARGET} PRIVATE imgui)
    add_static_lib( ${TARGET} )
endif()

target_include_directories(${TARGET} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/imgui/")

# Important: WE DO NOT WANT TO DO target_link_libraries
# As we want to link libimgui *dynamically* at run-time.
# if we don't do so, then this module becomes un-hot-reloadable in linux

source_group(${TARGET} FILES ${SOURCES})

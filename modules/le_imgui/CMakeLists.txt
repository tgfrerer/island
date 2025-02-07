set (TARGET le_imgui)

depends_on_island_module(le_pipeline_builder)
depends_on_island_module(le_renderer)
depends_on_island_module(le_ui_event)

# n.b. configure_file will replace any cmake variables it finds in the file with their values.    
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/imgui/LICENSE.txt")
	configure_file("imconfig.h" "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/imgui/imconfig.h" COPYONLY)
else()
	message( FATAL_ERROR "\nImgui git submodule not found. \nHint: Run `git submodule init; git submodule update`; first...")
endif()

set (SOURCES "le_imgui.cpp")
set (SOURCES ${SOURCES} "le_imgui.h")

set (IMGUI_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/imgui/imgui.cpp")
set (IMGUI_SOURCES ${IMGUI_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/imgui/imgui_demo.cpp")
set (IMGUI_SOURCES ${IMGUI_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/imgui/imgui_draw.cpp")
set (IMGUI_SOURCES ${IMGUI_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/imgui/imgui_widgets.cpp")
set (IMGUI_SOURCES ${IMGUI_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/imgui/imgui_tables.cpp")

if (${PLUGINS_DYNAMIC})
    add_library(${TARGET} SHARED ${SOURCES})
    add_dynamic_linker_flags()
    target_compile_definitions(${TARGET} PUBLIC "PLUGINS_DYNAMIC")
else()
	# In case we are creating a non-dynamic build, we want to
	# combine sources for imgui with le_imgui and compile all
	# into one static library, instead of creating two static
	# libraries.
    set (COMBINED_SOURCES ${SOURCES})
    set (COMBINED_SOURCES ${COMBINED_SOURCES} ${IMGUI_SOURCES})	
    add_library(${TARGET} STATIC ${COMBINED_SOURCES})
	add_static_lib( ${TARGET} )
	target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})
endif()

target_include_directories(${TARGET} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/imgui/")

if (${PLUGINS_DYNAMIC})
	# if we're hot-reloading, then we use imgui as an external dependency
	# this dependency is compiled as a dynamic library in case of a debug
	# build, but statically compiled (and linked) in a regular build.
	add_library(imgui SHARED ${IMGUI_SOURCES})
	target_link_libraries(${TARGET} PRIVATE imgui)
endif()

source_group(${TARGET} FILES ${SOURCES})
source_group(imgui FILES ${IMGUI_SOURCES})

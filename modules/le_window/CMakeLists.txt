set( GLFW_BUILD_EXAMPLES  OFF CACHE INTERNAL "" )
set( GLFW_BUILD_TESTS     OFF CACHE INTERNAL "" )
set( GLFW_BUILD_DOCS      OFF CACHE INTERNAL "" )
set( GLFW_INSTALL         OFF CACHE INTERNAL "" )
set( GLFW_VULKAN_STATIC   OFF CACHE INTERNAL "" )

if (${PLUGINS_DYNAMIC})
    set( BUILD_SHARED_LIBS ON)
endif()

add_compile_definitions(VK_NO_PROTOTYPES)

add_subdirectory( 3rdparty/glfw )

set (TARGET le_window )

depends_on_island_module(le_ui_event)
depends_on_island_module(le_log)
depends_on_island_module(le_backend_vk)


set (SOURCES "le_window.cpp")
set (SOURCES ${SOURCES} "le_window.h")

if (${PLUGINS_DYNAMIC})

    add_library( ${TARGET} SHARED ${SOURCES} )
    
    add_dynamic_linker_flags()

    target_compile_definitions(${TARGET} PUBLIC "PLUGINS_DYNAMIC=1")

else()

    # We also set a compiler flag so that our main source file can use the correct
    # static code path to statically load the library function pointers.

    # Adding a static library means to also add a linker dependency for our target
    # to the library.
    add_static_lib( ${TARGET} )

    add_library(${TARGET} STATIC ${SOURCES})

endif()

# This module depends on glfw - we must make sure that glfw will not be re-loaded
# when this module unloads, which means that we must somehow force the dynamic linker
# to include it in the main program already, which means even if our plugin unloads
# the library, the reference count in the linker for glfw will not be zero,
# and glfw will not be unloaded recursively
#
# This is done in le_api_loader.cpp - by explicitly loading glfw and specifying RTLD_NODELETE


if (WIN32)
     set (LINKER_FLAGS ${LINKER_FLAGS} glfw)
else()
    set (LINKER_FLAGS -Wl,--whole-archive glfw -Wl,--no-whole-archive)
endif()

target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})

source_group(${TARGET} FILES ${SOURCES})


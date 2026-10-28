
include(CMakeDependentOption)

cmake_dependent_option(GLFW_BUILD_WAYLAND "Build support for Wayland" ON "UNIX;NOT WIN32" OFF)
cmake_dependent_option(LE_BUILD_OPTION_WAYLAND_TABLET "Build support for Wayland Tablet" OFF "GLFW_BUILD_WAYLAND" OFF)

set( GLFW_BUILD_EXAMPLES  OFF CACHE INTERNAL "" )
set( GLFW_BUILD_TESTS     OFF CACHE INTERNAL "" )
set( GLFW_BUILD_DOCS      OFF CACHE INTERNAL "" )
set( GLFW_INSTALL         OFF CACHE INTERNAL "" )
set( GLFW_VULKAN_STATIC   OFF CACHE INTERNAL "" )


if (${LE_BUILD_OPTION_WAYLAND_TABLET})

    message(STATUS "Using Wayland as target for GLFW.")
    # since we're building wayland we may as well add tablet support
    find_package(PkgConfig REQUIRED)

    pkg_check_modules(WAYLAND REQUIRED wayland-client wayland-cursor wayland-egl xkbcommon)

    # Tablet (Wacom) support: generate the tablet-v2 protocol code with wayland-scanner.
    find_program(WAYLAND_SCANNER_EXECUTABLE NAMES wayland-scanner REQUIRED)
    execute_process(COMMAND pkg-config --variable=pkgdatadir wayland-protocols
                    OUTPUT_VARIABLE WAYLAND_PROTOCOLS_DIR OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(TABLET_XML "${WAYLAND_PROTOCOLS_DIR}/stable/tablet/tablet-v2.xml")
    if (NOT EXISTS ${TABLET_XML})
        message(FATAL_ERROR "tablet-v2.xml not found in ${WAYLAND_PROTOCOLS_DIR} (install wayland-protocols)")
    endif()

    message(STATUS "Generating wayland helper files via wayland-scanner")

    set (GEN_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated")
    file (MAKE_DIRECTORY ${GEN_SRC_DIR})

    add_custom_command(
        OUTPUT ${GEN_SRC_DIR}/tablet-protocol.h ${GEN_SRC_DIR}/tablet-protocol.c
        COMMAND ${WAYLAND_SCANNER_EXECUTABLE} client-header ${TABLET_XML} ${GEN_SRC_DIR}/tablet-protocol.h
        COMMAND ${WAYLAND_SCANNER_EXECUTABLE} private-code ${TABLET_XML} ${GEN_SRC_DIR}/tablet-protocol.c
        COMMENT "Generating tablet-v2 protocol code"
    )

    # Tablet sources are generated wl files plus the associated file
    set(TABLET_SRCS
        ${GEN_SRC_DIR}/tablet-protocol.h
        ${GEN_SRC_DIR}/tablet-protocol.c
        "le_window_tablet_wayland.cpp"
    )

    add_compile_definitions(
        LE_BUILD_OPTION_WAYLAND_TABLET
    )
endif()

if (${PLUGINS_DYNAMIC})
    set( BUILD_SHARED_LIBS ON)
endif()

add_compile_definitions(VK_NO_PROTOTYPES)

add_subdirectory( 3rdparty/glfw )

set (TARGET le_window )

depends_on_island_module(le_ui_event)
depends_on_island_module(le_log)
depends_on_island_module(le_backend_vk)

set (SOURCES
    "le_window.cpp"
    "le_window.h"
    ${TABLET_SRCS}
)

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
    set (LINKER_FLAGS -Wl,--whole-archive glfw ${WAYLAND_LIBRARIES} -Wl,--no-whole-archive)
endif()

target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})

source_group(${TARGET} FILES ${SOURCES})


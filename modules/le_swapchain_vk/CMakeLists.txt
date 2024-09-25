set (TARGET le_swapchain_vk)

# list modules this module depends on
depends_on_island_module(le_core)
depends_on_island_module(le_window)
depends_on_island_module(le_log)
depends_on_island_module(le_backend_vk)
depends_on_island_module(le_renderer)
depends_on_island_module(le_tracy)

add_compile_definitions(VK_NO_PROTOTYPES)

set (SOURCES "le_swapchain_vk.h")
set (SOURCES ${SOURCES} "le_swapchain_vk.cpp")
set (SOURCES ${SOURCES} "le_swapchain_khr.cpp")
set (SOURCES ${SOURCES} "le_swapchain_img.cpp")
set (SOURCES ${SOURCES} "le_swapchain_direct.cpp")
set (SOURCES ${SOURCES} "le_swapchain_khr.h")
set (SOURCES ${SOURCES} "le_swapchain_img.h")
set (SOURCES ${SOURCES} "le_swapchain_direct.h")
set (SOURCES ${SOURCES} "private/le_swapchain_vk/le_swapchain_vk_common.inl")
set (SOURCES ${SOURCES} "private/le_swapchain_vk/vk_to_string_helpers.inl")

include(CMakeDependentOption)

cmake_dependent_option(LE_SWAPCHAIN_BUILD_X11 "Build support for X11" ON "UNIX;NOT WIN32" OFF)
cmake_dependent_option(LE_SWAPCHAIN_BUILD_WAYLAND "Build support for Wayland" ON "UNIX;NOT WIN32" OFF)

if (WIN32)
else()
	if (LE_SWAPCHAIN_BUILD_WAYLAND )
		set (flags_string ${flags_string} wayland-client)
		set (flags_string ${flags_string} drm)

		add_compile_definitions(LE_SWAPCHAIN_BUILD_WAYLAND)

	endif()
	if (LE_SWAPCHAIN_BUILD_X11)
		set (flags_string ${flags_string} X11)
		add_compile_definitions(LE_SWAPCHAIN_BUILD_X11)
	endif()
	    set (LINKER_FLAGS -Wl,--whole-archive ${flags_string} -Wl,--no-whole-archive )
endif()
if (${PLUGINS_DYNAMIC})
    add_library(${TARGET} SHARED ${SOURCES})
    add_dynamic_linker_flags()
    target_compile_definitions(${TARGET} PUBLIC "PLUGINS_DYNAMIC=1")
else()
    add_static_lib( ${TARGET} )
    add_library(${TARGET} STATIC ${SOURCES})
endif()


target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})

source_group(${TARGET} FILES ${SOURCES})

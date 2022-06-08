set (TARGET le_swapchain_vk)

# list modules this module depends on
depends_on_island_module(le_core)
depends_on_island_module(le_window)
depends_on_island_module(le_log)

add_compile_definitions(VK_NO_PROTOTYPES)

set (SOURCES "le_swapchain_vk.h")
set (SOURCES ${SOURCES} "le_swapchain_vk.cpp")
set (SOURCES ${SOURCES} "le_swapchain_khr.cpp")
set (SOURCES ${SOURCES} "le_swapchain_img.cpp")
set (SOURCES ${SOURCES} "le_swapchain_direct.cpp")
set (SOURCES ${SOURCES} "private/le_swapchain_vk/le_swapchain_vk_common.inl")
set (SOURCES ${SOURCES} "private/le_swapchain_vk/vk_to_string_helpers.inl")

if (${PLUGINS_DYNAMIC})
    add_library(${TARGET} SHARED ${SOURCES})
    add_dynamic_linker_flags()
    target_compile_definitions(${TARGET} PUBLIC "PLUGINS_DYNAMIC=1")
else()
    add_static_lib( ${TARGET} )
    add_library(${TARGET} STATIC ${SOURCES})
endif()

if (WIN32)
else()
    set (LINKER_FLAGS -Wl,--whole-archive X11 -Wl,--no-whole-archive )
endif()

target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})

source_group(${TARGET} FILES ${SOURCES})
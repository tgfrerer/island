set (TARGET le_swapchain_vk)

set (SOURCES "le_swapchain_vk.h")
set (SOURCES ${SOURCES} "le_swapchain_vk.cpp")
set (SOURCES ${SOURCES} "le_swapchain_khr.cpp")
set (SOURCES ${SOURCES} "le_swapchain_img.cpp")
set (SOURCES ${SOURCES} "le_swapchain_direct.cpp")
set (SOURCES ${SOURCES} "include/internal/le_swapchain_vk_common.h")

if (${PLUGINS_DYNAMIC})

    add_library(${TARGET} SHARED ${SOURCES})

    
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

set (LINKER_FLAGS -Wl,--whole-archive vulkan X11 -Wl,--no-whole-archive )
target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})

source_group(${TARGET} FILES ${SOURCES})
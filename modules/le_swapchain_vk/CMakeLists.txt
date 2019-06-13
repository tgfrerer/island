set (TARGET le_swapchain_vk)

set (SOURCES "le_swapchain_vk.h")
set (SOURCES ${SOURCES} "le_swapchain_vk.cpp")
set (SOURCES ${SOURCES} "le_swapchain_khr.cpp")
set (SOURCES ${SOURCES} "le_swapchain_img.cpp")
set (SOURCES ${SOURCES} "include/internal/le_swapchain_vk_common.h")

if (${PLUGINS_DYNAMIC})

    add_library(${TARGET} SHARED ${SOURCES})

    # IMPORTANT: --no-gnu-unique
    #            This compiler flag is neccesary as otherwise the library may get compiled
    #            with some symbols exported as UNIQUE, which implicitly makes this library
    #            un-closeable. This means, any calls to `dlclose` on this library, once
    #            loaded, will have no effect, and autoreload for this library will not work
    #            as the first version of the library will remain resident.
    target_compile_options (${TARGET} PUBLIC --no-gnu-unique)

    target_compile_definitions(${TARGET} PUBLIC "PLUGINS_DYNAMIC=1")

else()

    # We also set a compiler flag so that our main source file can use the correct
    # static code path to statically load the library function pointers.

    # Adding a static library means to also add a linker dependency for our target
    # to the library.
    set (STATIC_LIBS ${STATIC_LIBS} ${TARGET} PARENT_SCOPE)

    add_library(${TARGET} STATIC ${SOURCES})

endif()

set (LINKER_FLAGS -Wl,--whole-archive vulkan -Wl,--no-whole-archive )
target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})

#include_directories("/usr/local/include")
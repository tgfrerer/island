
set (SOURCES "pal_backend_vk.cpp")
set (SOURCES ${SOURCES} "instance_vk.cpp")

if (${PLUGINS_DYNAMIC})
    add_library(pal_backend_vk SHARED ${SOURCES})  
#    target_link_libraries(pal_backend_vk PUBLIC pal_api_loader)
else()

    # We also set a compiler flag so that our main source file can use the correct
    # static code path to statically load the library function pointers.

    # Adding a static library means to also add a linker dependency for our target
    # to the library.
    set (STATIC_LIBS ${STATIC_LIBS} pal_backend_vk PARENT_SCOPE)

    add_library(pal_backend_vk STATIC ${SOURCES})

    target_compile_definitions(pal_backend_vk PUBLIC "PLUGIN_PAL_BACKEND_VK_STATIC")

endif()

set (VULKAN -Wl,--whole-archive vulkan -Wl,--no-whole-archive)
target_link_libraries(pal_backend_vk PRIVATE ${VULKAN})

#set (PLUGIN_LIBS_DEPENDENCIES ${PLUGIN_LIBS_DEPENDENCIES} ${VULKAN} PARENT_SCOPE)

include_directories("/usr/local/include")

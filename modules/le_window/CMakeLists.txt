set (TARGET le_window )

add_compile_definitions(VULKAN_HPP_NO_SMART_HANDLE)

set (SOURCES "le_window.cpp")
set (SOURCES ${SOURCES} "le_window.h")

if (${PLUGINS_DYNAMIC})

    add_library( ${TARGET} SHARED ${SOURCES} )

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
    add_static_lib( ${TARGET} )

    add_library(${TARGET} STATIC ${SOURCES})

endif()

# This plugin depends on glfw - we must make sure that glfw will not be re-loaded
# when this plugin unloads, which means that we must somehow force the dynamic linker
# to include it in the main program already, which means even if our plugin unloads
# the library, the reference count in the linker for glfw will not be zero,
# and glfw will not be unloaded recursively
#
# This is done in the ApiLoader.cpp - by explicitly loading glfw and specifying RTLD_NODELETE

set (LINKER_FLAGS -Wl,--whole-archive glfw -Wl,--no-whole-archive)
set (LINKER_FLAGS ${LINKER_FLAGS} vulkan)
target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})


if (${PLUGINS_DYNAMIC})

    add_library(pal_window SHARED pal_window.cpp)
#    target_link_libraries(pal_window PUBLIC pal_api_loader)

else()

    # We also set a compiler flag so that our main source file can use the correct
    # static code path to statically load the library function pointers.
    # set (PLUGIN_DEFINITIONS ${PLUGIN_DEFINITIONS} "PLUGIN_PAL_WINDOW_STATIC" PARENT_SCOPE)


    # Adding a static library means to also add a linker dependency for our target
    # to the library.
    set (STATIC_LIBS ${STATIC_LIBS} pal_window PARENT_SCOPE)

    add_library(pal_window STATIC pal_window.cpp)

    target_compile_definitions(pal_window PUBLIC "PLUGIN_PAL_WINDOW_STATIC")

endif()

# This plugin depends on glfw - we must make sure that glfw will not be re-loaded
# when this plugin unloads, which means that we must somehow force the dynamic linker
# to include it in the main program already, which means even if our plugin unloads
# the library, the reference count in the linker for glfw will not be zero,
# and glfw will not be unloaded recursively
#
# This is done in the ApiLoader.cpp - by explicitly loading glfw and specifying RTLD_NODELETE

set (GLFW -Wl,--whole-archive glfw -Wl,--no-whole-archive)
#set (PLUGIN_LIBS_DEPENDENCIES ${PLUGIN_LIBS_DEPENDENCIES} ${GLFW} PARENT_SCOPE)

target_link_libraries(pal_window PUBLIC ${GLFW})

include_directories("/usr/local/include")

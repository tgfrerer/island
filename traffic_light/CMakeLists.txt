if (${PLUGINS_DYNAMIC})
    add_library(traffic_light SHARED traffic_light.cpp)

else()

    # We also set a compiler flag so that our main source file can use the correct
    # static code path to statically load the library function pointers.
    set (PLUGIN_DEFINITIONS ${PLUGIN_DEFINITIONS} "PLUGIN_TRAFFIC_LIGHT_STATIC" PARENT_SCOPE)

    # Adding a static library means to also add a linker dependency for our target
    # to the library.
    set (STATIC_LIBS ${STATIC_LIBS} traffic_light PARENT_SCOPE)

    add_library(traffic_light STATIC traffic_light.cpp)

endif()

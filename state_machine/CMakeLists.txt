if (${PLUGINS_DYNAMIC})
    add_library(state_machine SHARED state_machine.cpp)

else()

    # We also set a compiler flag so that our main source file can use the correct
    # static code path to statically load the library function pointers.
    set (PLUGIN_DEFINITIONS ${PLUGIN_DEFINITIONS} "PLUGIN_STATE_MACHINE_STATIC" PARENT_SCOPE)

    # Adding a static library means to also add a linker dependency for our target
    # to the library.
    set (STATIC_LIBS ${STATIC_LIBS} state_machine PARENT_SCOPE)

    add_library(file_watcher STATIC state_machine.cpp)

endif()

set (TARGET test_app)

set (SOURCES "test_app.cpp")
set (SOURCES ${SOURCES} "test_app.h")
set (SOURCES ${SOURCES} "horse_image.h")

if (${PLUGINS_DYNAMIC})

    add_library(${TARGET} SHARED ${SOURCES})

    # IMPORTANT: --no-gnu-unique
    #            This compiler flag is neccesary as otherwise the library may get compiled
    #            with some symbols exported as UNIQUE, which implicitly makes this library
    #            un-closeable. This means, any calls to `dlclose` on this library, once
    #            loaded, will have no effect, and autoreload for this library will not work
    #            as the first version of the library will remain resident.
    target_compile_options (${TARGET} PUBLIC --no-gnu-unique)

    target_compile_definitions(${TARGET}  PUBLIC "PLUGINS_DYNAMIC")

else()

    # We also set a compiler flag so that our main source file can use the correct
    # static code path to statically load the library function pointers.

    # Adding a static library means to also add a linker dependency for our target
    # to the library.
    set (STATIC_LIBS ${STATIC_LIBS} ${TARGET} PARENT_SCOPE)

    add_library(${TARGET} STATIC ${SOURCES})

    # link against static version of imgui library
    target_link_libraries(${TARGET} PRIVATE imgui)

endif()

set (LINKER_FLAGS -Wl,--whole-archive vulkan -Wl,--no-whole-archive )

target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})

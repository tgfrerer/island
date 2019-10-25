set (TARGET le_font)

depends_on_island_module(le_path)

set (SOURCES "le_font.cpp")
set (SOURCES ${SOURCES} "le_font.h")
set (SOURCES ${SOURCES} "3rdparty/stb_truetype.h")
set (SOURCES ${SOURCES} "3rdparty/stb_truetype.cpp")
set (SOURCES ${SOURCES} "3rdparty/stb_rect_pack.h")

if (${PLUGINS_DYNAMIC})

    add_library(${TARGET} SHARED ${SOURCES})

    # IMPORTANT: --no-gnu-unique
    #            This compiler flag is neccesary as otherwise the library may get compiled
    #            with some symbols exported as UNIQUE, which implicitly makes this library
    #            un-closeable. This means, any calls to `dlclose` on this library, once
    #            loaded, will have no effect, and autoreload for this library will not work
    #            as the first version of the library will remain resident.
    target_compile_options (${TARGET} PUBLIC --no-gnu-unique)
    
    target_compile_definitions(${TARGET} PUBLIC "PLUGINS_DYNAMIC")

else()

    add_library(${TARGET} STATIC ${SOURCES})

    add_static_lib( ${TARGET} )

endif()

# set (LINKER_FLAGS ${LINKER_FLAGS} stdc++fs)

target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})


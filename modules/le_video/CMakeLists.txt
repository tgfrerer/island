set(TARGET le_video)

# list modules this module depends on
depends_on_island_module(le_resource_manager)
depends_on_island_module(le_pixels)
depends_on_island_module(le_log)

list(APPEND CMAKE_MODULE_PATH  ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty)

FIND_PACKAGE(LIBVLC REQUIRED)

set(SOURCES "le_video.cpp")
set(SOURCES ${SOURCES} "le_video.h")

if (${PLUGINS_DYNAMIC})

    add_library(${TARGET} SHARED ${SOURCES})

    # IMPORTANT: --no-gnu-unique
    #            This compiler flag is neccesary as otherwise the library may get compiled
    #            with some symbols exported as UNIQUE, which implicitly makes this library
    #            un-closeable. This means, any calls to `dlclose` on this library, once
    #            loaded, will have no effect, and autoreload for this library will not work
    #            as the first version of the library will remain resident.
    target_compile_options(${TARGET} PUBLIC --no-gnu-unique)

    target_compile_definitions(${TARGET} PUBLIC "PLUGINS_DYNAMIC")

else ()

    add_library(${TARGET} STATIC ${SOURCES})

    set(STATIC_LIBS ${STATIC_LIBS} ${TARGET} PARENT_SCOPE)

endif ()

# set (LINKER_FLAGS ${LINKER_FLAGS} stdc++fs)

target_include_directories(${TARGET} PRIVATE ${LIBVLC_INCLUDE_DIRS})

# TODO: use the actual filepath from findVLC for maximum platform compatibility, ${LIBVLCCORE_LIBRARY}
target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS} vlc)

source_group(${TARGET} FILES ${SOURCES})
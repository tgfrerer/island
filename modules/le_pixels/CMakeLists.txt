depends_on_island_module(le_log)
depends_on_island_module(le_renderer)

set (TARGET le_pixels)

set (SOURCES "le_pixels.cpp")
set (SOURCES ${SOURCES} "le_pixels.h")
set (SOURCES ${SOURCES} "3rdparty/stb_image.h")
set (SOURCES ${SOURCES} "3rdparty/stb_image_implementation.cpp")

if (${PLUGINS_DYNAMIC})

    add_library(${TARGET} SHARED ${SOURCES})
    
    add_dynamic_linker_flags()
    
    target_compile_definitions(${TARGET}  PUBLIC "PLUGINS_DYNAMIC")

else()

    add_library(${TARGET} STATIC ${SOURCES})

    add_static_lib( ${TARGET} )

endif()

# set (LINKER_FLAGS ${LINKER_FLAGS} stdc++fs)

target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})

source_group(${TARGET} FILES ${SOURCES})

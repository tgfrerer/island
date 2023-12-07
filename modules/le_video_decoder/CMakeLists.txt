set (TARGET le_video_decoder)

# List of modules that this module depends on:
depends_on_island_module(le_backend_vk)
depends_on_island_module(le_renderer)
depends_on_island_module(le_log)
depends_on_island_module(le_timebase)

add_compile_definitions(VK_NO_PROTOTYPES)
add_compile_definitions( VK_ENABLE_BETA_EXTENSIONS=true)

set (SOURCES "le_video_decoder.cpp")
set (SOURCES ${SOURCES} "le_video_decoder.h")
set (SOURCES ${SOURCES} "3rdparty/minimp4/minimp4.h")
set (SOURCES ${SOURCES} "3rdparty/h264/h264.h")

if (${PLUGINS_DYNAMIC})
    add_library(${TARGET} SHARED ${SOURCES})
    add_dynamic_linker_flags()
    target_compile_definitions(${TARGET}  PUBLIC "PLUGINS_DYNAMIC")
else()
    add_library(${TARGET} STATIC ${SOURCES})
    add_static_lib( ${TARGET} )
endif()

target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})

source_group(${TARGET} FILES ${SOURCES})

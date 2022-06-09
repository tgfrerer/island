set (TARGET le_gltf)

# list modules this module depends on
depends_on_island_module(le_stage)
depends_on_island_module(le_renderer)

set (SOURCES "le_gltf.cpp")
set (SOURCES ${SOURCES} "le_gltf.h")
set (SOURCES ${SOURCES} "3rdparty/cgltf/cgltf.h")
set (SOURCES ${SOURCES} "cgltf.c")
set (SOURCES ${SOURCES} "${ISLAND_BASE_DIR}/3rdparty/src/spooky/SpookyV2.cpp")
set (SOURCES ${SOURCES} "${ISLAND_BASE_DIR}/3rdparty/src/spooky/SpookyV2.h")


if (${PLUGINS_DYNAMIC})

    add_library(${TARGET} SHARED ${SOURCES})

    
    add_dynamic_linker_flags()
    
    target_compile_definitions(${TARGET}  PUBLIC "PLUGINS_DYNAMIC")

else()

    add_library(${TARGET} STATIC ${SOURCES})

    add_static_lib(${TARGET})

endif()

# set (LINKER_FLAGS ${LINKER_FLAGS} stdc++fs)

target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})
source_group(${TARGET} FILES ${SOURCES})

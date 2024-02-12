set (TARGET le_mesh)

set (SOURCES "le_mesh.h" )
set (SOURCES ${SOURCES} "le_mesh.cpp" )
set (SOURCES ${SOURCES} "le_mesh_types.h" )
set (SOURCES ${SOURCES} "le_mesh_ply.cpp" )

if (${PLUGINS_DYNAMIC})
    add_library(${TARGET} SHARED ${SOURCES})
    add_dynamic_linker_flags()
    target_compile_definitions(${TARGET}  PUBLIC "PLUGINS_DYNAMIC")
else()
    add_library(${TARGET} STATIC ${SOURCES})
    add_static_lib( ${TARGET} )
endif()

if (WIN32)
else()
    set (LINKER_FLAGS ${LINKER_FLAGS} stdc++fs)
endif()

target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})

source_group(${TARGET} FILES ${SOURCES})

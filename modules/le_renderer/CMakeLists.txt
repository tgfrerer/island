set (TARGET le_renderer)

set (SOURCES "le_renderer.cpp")
set (SOURCES ${SOURCES} "le_renderer.h")
set (SOURCES ${SOURCES} "private/le_renderer_types.h")
set (SOURCES ${SOURCES} "le_rendergraph.cpp")
set (SOURCES ${SOURCES} "le_command_buffer_encoder.cpp")

set (SOURCES ${SOURCES} "${ISLAND_BASE_DIR}/3rdparty/src/spooky/SpookyV2.cpp")
set (SOURCES ${SOURCES} "${ISLAND_BASE_DIR}/3rdparty/src/spooky/SpookyV2.h")

if (${PLUGINS_DYNAMIC})

    add_library(${TARGET} MODULE ${SOURCES})

    
    add_dynamic_linker_flags()

    target_compile_definitions(${TARGET} PUBLIC "PLUGINS_DYNAMIC=1")

else()

    # We also set a compiler flag so that our main source file can use the correct
    # static code path to statically load the library function pointers.

    # Adding a static library means to also add a linker dependency for our target
    # to the library.
    add_static_lib( ${TARGET} )

    add_library(${TARGET} STATIC ${SOURCES})

endif()

target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})

source_group(${TARGET} FILES ${SOURCES})
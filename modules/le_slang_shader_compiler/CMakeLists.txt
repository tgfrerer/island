set (TARGET le_slang_shader_compiler)

# list modules this module depends on
depends_on_island_module(le_log)
depends_on_island_module(le_renderer)
depends_on_island_module(le_shader_compiler)

set (SOURCES "le_slang_shader_compiler.cpp")
set (SOURCES ${SOURCES} "le_slang_shader_compiler.h")

if (${PLUGINS_DYNAMIC})

    add_library(${TARGET} SHARED ${SOURCES})
    add_dynamic_linker_flags()
    target_compile_definitions(${TARGET} PUBLIC "PLUGINS_DYNAMIC=1")

    if (WIN32)
        set (LINKER_FLAGS  ${LINKER_FLAGS} slang)
    else()    
        set (LINKER_FLAGS  ${LINKER_FLAGS} -Wl,--whole-archive slang.so -Wl,--no-whole-archive)
    endif()

else()

    # Set additional search directory for static linker
    # set (PLUGIN_LINK_DIRS ${PLUGIN_LINK_DIRS} ${LIB_SHADERC_DIR} PARENT_SCOPE)

    add_static_lib( ${TARGET} )

    add_library(${TARGET} STATIC ${SOURCES})

    if (WIN32)
        target_link_libraries(${TARGET} PRIVATE slang)
    else()
        target_link_libraries(${TARGET} PRIVATE slang)
    endif()

endif()

target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})

source_group(${TARGET} FILES ${SOURCES})

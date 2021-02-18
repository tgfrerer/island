set (TARGET le_shader_compiler)

set (SOURCES "le_shader_compiler.cpp")
set (SOURCES ${SOURCES} "le_shader_compiler.h")

if (${PLUGINS_DYNAMIC})

    add_library(${TARGET} SHARED ${SOURCES})
    add_dynamic_linker_flags()
    target_compile_definitions(${TARGET} PUBLIC "PLUGINS_DYNAMIC=1")

    if (WIN32)
        set (LINKER_FLAGS  ${LINKER_FLAGS} shaderc_shared )
    else()    
        set (LINKER_FLAGS  ${LINKER_FLAGS} -Wl,--whole-archive shaderc_shared.so -Wl,--no-whole-archive)
    endif()

else()

    # Set additional search directory for static linker - where it may find
    # libshaderc_shared.so
    set (PLUGIN_LINK_DIRS ${PLUGIN_LINK_DIRS} ${LIB_SHADERC_DIR} PARENT_SCOPE)

    add_static_lib( ${TARGET} )

    add_library(${TARGET} STATIC ${SOURCES})

    if (WIN32)
        set (LINKER_FLAGS  ${LINKER_FLAGS} shaderc_shared )
    else()
        set (LINKER_FLAGS ${LINKER_FLAGS} pthread)
        include(FindPkgConfig)
        pkg_check_modules(shaderc REQUIRED IMPORTED_TARGET shaderc)
        set (LINKER_FLAGS  ${LINKER_FLAGS} PkgConfig::shaderc )
        # message(STATUS ${LINKER_FLAGS}) 
    endif()

endif()

if (WIN32)
else()
    set (LINKER_FLAGS ${LINKER_FLAGS} stdc++fs)
endif()

target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})

source_group(${TARGET} FILES ${SOURCES})
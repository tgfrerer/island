set (TARGET le_shader_compiler)

set (SOURCES "le_shader_compiler.cpp")
set (SOURCES ${SOURCES} "le_shader_compiler.h")

#set (LIB_SHADERC_DIR $ENV{VULKAN_SDK}/lib/libshaderc/)

# NOTE: We're linking shaderc from this system's Vulkan SDK directory
#link_directories(${LIB_SHADERC_DIR})

# NOTE: We're using the shaderc headers from this system's Vulkan SDK directory
#include_directories($ENV{VULKAN_SDK}/include/)

if (${PLUGINS_DYNAMIC})

    add_library(${TARGET} SHARED ${SOURCES})

    
    add_dynamic_linker_flags()

    target_compile_definitions(${TARGET} PUBLIC "PLUGINS_DYNAMIC=1")

    set (LINKER_FLAGS  ${LINKER_FLAGS} -Wl,--whole-archive shaderc_shared.so -Wl,--no-whole-archive)
else()

    # Set additional search directory for static linker - where it may find
    # libshaderc_shared.so
    set (PLUGIN_LINK_DIRS ${PLUGIN_LINK_DIRS} ${LIB_SHADERC_DIR} PARENT_SCOPE)

    # Adding a static library means to also add a linker dependency for our target
    # to the library.
    add_static_lib( ${TARGET} )

    add_library(${TARGET} STATIC ${SOURCES})

    set (LINKER_FLAGS ${LINKER_FLAGS} pthread)

    set (LINKER_FLAGS  ${LINKER_FLAGS} -Wl,--whole-archive shaderc_combined -Wl,--no-whole-archive)

endif()


set (LINKER_FLAGS ${LINKER_FLAGS} stdc++fs)

target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})

source_group(${TARGET} FILES ${SOURCES})
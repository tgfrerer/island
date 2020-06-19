set (TARGET le_imgui)

set (SOURCES "le_imgui.cpp")
set (SOURCES ${SOURCES} "le_imgui.h")

if (${PLUGINS_DYNAMIC})

    add_library(${TARGET} SHARED ${SOURCES})

    
    add_dynamic_linker_flags()
    
    target_compile_definitions(${TARGET}  PUBLIC "PLUGINS_DYNAMIC")
    
else()

    add_library(${TARGET} STATIC ${SOURCES})

    # link against static version of imgui library
    target_link_libraries(${TARGET} PRIVATE imgui)

    add_static_lib( ${TARGET} )

endif()

target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})
source_group(${TARGET} FILES ${SOURCES})

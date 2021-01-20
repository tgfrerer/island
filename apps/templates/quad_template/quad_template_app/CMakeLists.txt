set (TARGET quad_template_app)

set (SOURCES "quad_template_app.cpp")
set (SOURCES ${SOURCES} "quad_template_app.h")

if (${PLUGINS_DYNAMIC})

    add_library(${TARGET} SHARED ${SOURCES})

    
    add_dynamic_linker_flags()

    target_compile_definitions(${TARGET}  PUBLIC "PLUGINS_DYNAMIC")

else()

    # Adding a static library means to also add a linker dependency for our target
    # to the library.
    set (STATIC_LIBS ${STATIC_LIBS} ${TARGET} PARENT_SCOPE)

    add_library(${TARGET} STATIC ${SOURCES})

endif()

target_include_directories(${TARGET} PUBLIC ${MODULE_LOCATIONS_LIST})
target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})

source_group(${TARGET} FILES ${SOURCES})
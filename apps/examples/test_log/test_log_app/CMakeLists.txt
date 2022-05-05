set (TARGET test_log_app)

set (SOURCES "test_log_app.cpp")
set (SOURCES ${SOURCES} "test_log_app.h")

if (${PLUGINS_DYNAMIC})

    add_library(${TARGET} SHARED ${SOURCES})

    
    add_dynamic_linker_flags()

    target_compile_definitions(${TARGET}  PUBLIC "PLUGINS_DYNAMIC")

else()

    # Adding a static library means to also add a linker dependency for our target
    # to the library.
    add_static_lib( ${TARGET} )

    add_library(${TARGET} STATIC ${SOURCES})

endif()

target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})

source_group(${TARGET} FILES ${SOURCES})
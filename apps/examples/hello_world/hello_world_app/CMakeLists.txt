set (TARGET hello_world_app)

set (SOURCES "hello_world_app.cpp")
set (SOURCES ${SOURCES} "hello_world_app.h")

if (${PLUGINS_DYNAMIC})
    add_library(${TARGET} SHARED ${SOURCES})
    add_dynamic_linker_flags()
    target_compile_definitions(${TARGET}  PUBLIC "PLUGINS_DYNAMIC")
else()
    set (STATIC_LIBS ${STATIC_LIBS} ${TARGET} PARENT_SCOPE)
    add_library(${TARGET} STATIC ${SOURCES})
endif()

target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})

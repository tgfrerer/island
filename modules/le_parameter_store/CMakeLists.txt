set (TARGET le_parameter_store)

# list modules this module depends on
# depends_on_island_module(le_some_example_module)

set (SOURCES "le_parameter_store.cpp")
set (SOURCES ${SOURCES} "le_parameter_store.h")

if (${PLUGINS_DYNAMIC})
    add_library(${TARGET} SHARED ${SOURCES})
    add_dynamic_linker_flags()
    target_compile_definitions(${TARGET}  PUBLIC "PLUGINS_DYNAMIC")
else()
    add_library(${TARGET} STATIC ${SOURCES})
    set (STATIC_LIBS ${STATIC_LIBS} ${TARGET} PARENT_SCOPE)
	add_static_lib( ${TARGET} )
endif()

# set (LINKER_FLAGS ${LINKER_FLAGS} stdc++fs)

target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})

source_group(${TARGET} FILES ${SOURCES})
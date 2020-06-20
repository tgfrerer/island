set (TARGET le_jobs)

set (SOURCES "le_jobs.cpp")
set (SOURCES ${SOURCES} "le_jobs.h")
set (SOURCES ${SOURCES} "private/lockfree_ring_buffer.h")
set (SOURCES ${SOURCES} "private/lockfree_ring_buffer.cpp")

if (${PLUGINS_DYNAMIC})

    add_library(${TARGET} SHARED ${SOURCES})

    
    add_dynamic_linker_flags()
    
    target_compile_definitions(${TARGET}  PUBLIC "PLUGINS_DYNAMIC")

    set (LINKER_FLAGS ${LINKER_FLAGS} -Wl,--whole-archive pthread -Wl,--no-whole-archive )

else()

    add_library(${TARGET} STATIC ${SOURCES})

    add_static_lib( ${TARGET} )

     target_link_libraries(${TARGET} PRIVATE pthread)
endif()

target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})

source_group(${TARGET} FILES ${SOURCES})
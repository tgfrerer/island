set (TARGET le_jobs)

set (SOURCES "le_jobs.cpp")
set (SOURCES ${SOURCES} "le_jobs.h")
set (SOURCES ${SOURCES} "private/lockfree_ring_buffer.h")
set (SOURCES ${SOURCES} "private/lockfree_ring_buffer.cpp")

if (${PLUGINS_DYNAMIC})

    add_library(${TARGET} SHARED ${SOURCES})

    # IMPORTANT: --no-gnu-unique
    #            This compiler flag is neccesary as otherwise the library may get compiled
    #            with some symbols exported as UNIQUE, which implicitly makes this library
    #            un-closeable. This means, any calls to `dlclose` on this library, once
    #            loaded, will have no effect, and autoreload for this library will not work
    #            as the first version of the library will remain resident.
    target_compile_options (${TARGET} PUBLIC --no-gnu-unique)
    
    target_compile_definitions(${TARGET}  PUBLIC "PLUGINS_DYNAMIC")

    set (LINKER_FLAGS ${LINKER_FLAGS} -Wl,--whole-archive pthread -Wl,--no-whole-archive )

else()

    add_library(${TARGET} STATIC ${SOURCES})

    add_static_lib( ${TARGET} )

     target_link_libraries(${TARGET} PRIVATE pthread)
endif()

target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})

source_group(${TARGET} FILES ${SOURCES})
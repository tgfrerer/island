set (TARGET le_tessellator)

set (SOURCES "le_tessellator.cpp")
set (SOURCES ${SOURCES} "le_tessellator.h")

# source files for libtess2
set (SOURCES ${SOURCES} "./3rdparty/libtess2/Source/tess.c")
set (SOURCES ${SOURCES} "./3rdparty/libtess2/Source/bucketalloc.c")
set (SOURCES ${SOURCES} "./3rdparty/libtess2/Source/dict.c")
set (SOURCES ${SOURCES} "./3rdparty/libtess2/Source/geom.c")
set (SOURCES ${SOURCES} "./3rdparty/libtess2/Source/mesh.c")
set (SOURCES ${SOURCES} "./3rdparty/libtess2/Source/priorityq.c")
set (SOURCES ${SOURCES} "./3rdparty/libtess2/Source/sweep.c")
set (SOURCES ${SOURCES} "./3rdparty/libtess2/Source/tess.c")


if (${PLUGINS_DYNAMIC})

    add_library(${TARGET} SHARED ${SOURCES})

    
    add_dynamic_linker_flags()
    
    target_compile_definitions(${TARGET}  PUBLIC "PLUGINS_DYNAMIC")

else()

    add_library(${TARGET} STATIC ${SOURCES})

    add_static_lib( ${TARGET} )

endif()

# set include directory for private 3rdparty includes to libtess2
target_include_directories(${TARGET} PRIVATE "./3rdparty/libtess2/Include" )

target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})
source_group(${TARGET} FILES ${SOURCES})

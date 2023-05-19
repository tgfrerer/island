set (TARGET le_tracy)

if (${PLUGINS_DYNAMIC})
    set( BUILD_SHARED_LIBS ON)
endif()

get_directory_property(COMPILE_DEFS COMPILE_DEFINITIONS)
MESSAGE( STATUS "Compile defs contain: " "${COMPILE_DEFS}" )

# We detect whether Tracy has been enabled via the 
# global compile_definition - you must set this in the 
# topmost CMakeLists.txt file
if ("TRACY_ENABLE" IN_LIST COMPILE_DEFS)
	set( TRACY_ENABLE ON CACHE INTERNAL "" )
	add_subdirectory( 3rdparty/tracy)
else()
	set( TRACY_ENABLE OFF CACHE INTERNAL "" )
endif()

# list modules this module depends on
# depends_on_island_module(le_path)

set (SOURCES ${SOURCES} "le_tracy.h")

if (${PLUGINS_DYNAMIC})

    add_library(${TARGET} SHARED ${SOURCES})

    add_dynamic_linker_flags()
  
    target_compile_definitions(${TARGET}  PUBLIC "PLUGINS_DYNAMIC")

else()

    add_library(${TARGET} STATIC ${SOURCES})

    add_static_lib( ${TARGET} )

endif()

target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})

# If tracing is enabled, and in case we are running a static build, 
# we must make the tracy lib available to the app by linking it against
# LibTracyClient
if (${TRACY_ENABLE} AND NOT ${PLUGINS_DYNAMIC})
	MESSAGE( STATUS "TracyClient enabled.")
	set (PLUGIN_LIBS_DEPENDENCIES ${PLUGIN_LIBS_DEPENDENCIES} TracyClient CACHE INTERNAL "plugins_libs_dependencies")
endif()

source_group(${TARGET} FILES ${SOURCES})

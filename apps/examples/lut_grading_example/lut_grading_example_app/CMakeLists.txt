set (TARGET lut_grading_example_app)

# Specify any used modules here - you may reference any module 
# found in the default Island modules/ directory, or found in any 
# directories you specified via `add_island_module_location` aboe.
#
depends_on_island_module(le_window)
depends_on_island_module(le_ui_event)
depends_on_island_module(le_renderer)
depends_on_island_module(le_pipeline_builder)
depends_on_island_module(le_camera)

depends_on_island_module(le_pixels)
depends_on_island_module(le_resource_manager)

set (SOURCES "lut_grading_example_app.cpp")
set (SOURCES ${SOURCES} "lut_grading_example_app.h")

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

target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})

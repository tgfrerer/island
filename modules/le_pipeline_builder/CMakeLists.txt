set (TARGET le_pipeline_builder)

add_compile_definitions(LE_FEATURE_RTX)
add_compile_definitions(VK_ENABLE_BETA_EXTENSIONS=1) # we need this for raytracing features
add_compile_definitions(VULKAN_HPP_NO_SMART_HANDLE)

set (SOURCES "le_pipeline_builder.cpp")
set (SOURCES ${SOURCES} "le_pipeline_builder.h")

if (${PLUGINS_DYNAMIC})

    add_library(${TARGET} SHARED ${SOURCES})

    
    add_dynamic_linker_flags()
    
    target_compile_definitions(${TARGET}  PUBLIC "PLUGINS_DYNAMIC")

else()

    add_library(${TARGET} STATIC ${SOURCES})

    add_static_lib( ${TARGET} )

endif()

# set (LINKER_FLAGS ${LINKER_FLAGS} stdc++fs)

target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})

source_group(${TARGET} FILES ${SOURCES})
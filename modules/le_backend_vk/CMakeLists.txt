set (TARGET le_backend_vk)


set (SOURCES "le_backend_vk.cpp")
set (SOURCES ${SOURCES} "le_backend_vk.h")
set (SOURCES ${SOURCES} "le_backend_types_internal.h")
set (SOURCES ${SOURCES} "le_instance_vk.cpp")
set (SOURCES ${SOURCES} "le_pipeline.cpp")
set (SOURCES ${SOURCES} "le_device_vk.cpp")
set (SOURCES ${SOURCES} "le_allocator.cpp")
set (SOURCES ${SOURCES} "util/spooky/SpookyV2.cpp")

set (SOURCES ${SOURCES} "util/vk_mem_alloc/vk_mem_alloc.h")
set (SOURCES ${SOURCES} "util/vk_mem_alloc/vk_mem_alloc.cpp")

set (SOURCES ${SOURCES} "util/spirv-cross/GLSL.std.450.h")
set (SOURCES ${SOURCES} "util/spirv-cross/spirv_common.hpp")
set (SOURCES ${SOURCES} "util/spirv-cross/spirv.hpp")
set (SOURCES ${SOURCES} "util/spirv-cross/spirv_cross.hpp")
set (SOURCES ${SOURCES} "util/spirv-cross/spirv_cross.cpp")
set (SOURCES ${SOURCES} "util/spirv-cross/spirv_cfg.hpp")
set (SOURCES ${SOURCES} "util/spirv-cross/spirv_cfg.cpp")


if (${PLUGINS_DYNAMIC})

    add_library(${TARGET} SHARED ${SOURCES})

    # IMPORTANT: --no-gnu-unique
    #            This compiler flag is neccesary as otherwise the library may get compiled
    #            with some symbols exported as UNIQUE, which implicitly makes this library
    #            un-closeable. This means, any calls to `dlclose` on this library, once
    #            loaded, will have no effect, and autoreload for this library will not work
    #            as the first version of the library will remain resident.
    target_compile_options (${TARGET} PUBLIC --no-gnu-unique)

    target_compile_definitions(${TARGET} PUBLIC "PLUGINS_DYNAMIC=1")

else()

    # We also set a compiler flag so that our main source file can use the correct
    # static code path to statically load the library function pointers.

    # Adding a static library means to also add a linker dependency for our target
    # to the library.
    add_static_lib( ${TARGET} )

    add_library(${TARGET} STATIC ${SOURCES})

endif()

set (LINKER_FLAGS ${LINKER_FLAGS} -Wl,--whole-archive vulkan -Wl,--no-whole-archive )
set (LINKER_FLAGS ${LINKER_FLAGS} stdc++fs)

target_link_libraries(${TARGET} PUBLIC ${LINKER_FLAGS})

# Low-Overhead Profiling via Tracy

Profiling is only enabled (and Tracy only compiled) if you add the
following compiler definition into your topmost CMakeLists.txt file:

	add_compile_definitions( TRACY_ENABLE )

To enable passing log messages to tracy, add
 	LE_TRACY_ENABLE_LOG(-1)
to where you initialize your main app.

Every module which uses tracy must load the tracy library, you
must do this via an explicit library load, by adding

	#ifdef LE_LOAD_TRACING_LIBRARY
 	LE_LOAD_TRACING_LIBRARY
	#endif

To where you initialize this module's api pointers in its cpp file.

---------------------------------------------------------------------- 
# TRACY PROFILER
---------------------------------------------------------------------- 

To view Tracy captures, you need to run the Tracy Profiler application, which 
you first must compile from source. You find the source for the Profiler 
application under `le_tracy/3rdparty/tracy/profiler/build`

Note that if you build the profiler on Windows, it is recommended that you 
first execute `le_tracy/3rdparty/tracy/vcpkg/install_vcpkg_dependencies.bat`
so that you have all necessary profiler dependencies installed before 
compiling.

For more information on Tracy, and on how to use the Tracy Profiler, see
the [Tracy repository on github](https://github.com/wolfpld/tracy)



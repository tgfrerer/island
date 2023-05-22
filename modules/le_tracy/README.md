# Low-Overhead Profiling via Tracy

Profiling is only enabled (and Tracy only compiled) if you add the
following compiler definition into your topmost CMakeLists.txt file:

	add_compile_definitions( TRACY_ENABLE )

To enable passing log messages to Tracy, add the single statement:
`LE_TRACY_ENABLE_LOG(-1)` to where you initialize your main app 
(this is most likely your `app::initialize()` method). 

Every module which uses Tracy must load the tracy library; you
must do this via an explicit library load, by adding

 	LE_LOAD_TRACING_LIBRARY

to where you initialize the module's api pointers in its cpp file. In case 
Tracy is not used this statement melts away to a no-op.

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



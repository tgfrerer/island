# Windows port progress tracker

Island is currently able to compile and run natively on Windows 10 (64bit) systems. 

I've tested using Visual Studio 2019, with Visual Studio's CMake build tools installed. 

## Visual Studio 2019 hints 

* Open the Cmake file by selecting "Open Directory"
* After generating the CMake cache (which should happen automatically once you open), you can configure build targets. Choose "Debug x64", or "Release x64". 
* Regenerate CMake cache
* In the project view, select "CMake Targets" to see all cmake targets which are associated with the current project - this allows you to inspect the Island framework source code at the same time as your own source
* Hit Build All (ctrl+shift+b)
* Hit Run (F5)

## Installation instructions

* Install Vulkan SDK for Windows via LunarG https://vulkan.lunarg.com/
* Set the environment variable `VULKAN_SDK` to the path where you installed the Vulkan SDK if this has not been set automatically.

# Progress

- [x] All Examples are working now
- [x] Release x64 Target works
- [x] Debug x64 Target works 
- [x] Loading modules as plugins (.dlls) works
- [x] File watcher ported to Windows
- [x] Shader-hot-reloading works
- [ ] le_jobs needs porting - implementation should use Windows' own [Fiber API](https://nullprogram.com/blog/2019/03/28/) 
- [ ] swapchain direct - needs porting for windows, provided driver allows direct rendering
- [ ] image swapchain needs porting - we can't pipe to ffmpeg like we did on linux - or can we?
- [ ] Hot-reloading for native code needs some thought

## Challenges

Because Windows file handles are fundamentally different from file handles on linux, it is impossible to overwrite a file on windows, which is currently in use. This means we must find a windows-specific way of unloading and reloading libraries for hot-reloading, as we cannot just watch the .dll for having been overwritten. 

We probably also must update the build system so that it produces different versions of the .dlls for us, and associated versions of .pdb (debug symbol) files. Perhaps something along the line of what Stefan Reinalter discusses in [this blog post](https://blog.molecular-matters.com/2017/05/09/deleting-pdb-files-locked-by-visual-studio/). 

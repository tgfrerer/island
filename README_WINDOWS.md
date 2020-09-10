# Windows port progress tracker

Island is currently able to compile and run natively on Windows 10 (64bit)
systems. Hot-reloading works, too. It is enabled by default for Debug targets.

I've tested using Visual Studio 2019, with Visual Studio's CMake build tools installed. 

## Visual Studio 2019 hints 

* Open the Cmake file by selecting "Open Directory"
* After generating the CMake cache (which should happen automatically once you
  open), you can configure build targets. Choose "Debug x64", or "Release x64". 
* Regenerate CMake cache
* In the project view, select "CMake Targets" to see all cmake targets which
  are associated with the current project - this allows you to inspect the
  Island framework source code at the same time as your own source
* Hit Build All (ctrl+shift+b)
* Hit Run (F5)

## Visual Studio 2019 hot-reloading hints

Once you've hit the 'run' button on visual studio and the debugger is attached,
the visual studio interface disables the shortcuts for building, and compiling.
This does, however, not mean that you can't compile and hot-reload your project
anymore. 

Here are a few steps to set you up for hot-reloading on Windows

* Build your app using Visual Studio
* Start your app the normal way from Visual Studio, so that the Debugger is attached 
* Start a "x64 Native Tools Command Prompt for VS 2019" → you'll find this under 'Windows Start Button → Visual Studio 2019 Folder → "x64 Native Tools Command Prompt for VS 2019"
* Navigate to the directory in which your app gets built (that's the folder in which you find the app's .exe file)
* Enter 'ninja' in the command prompt - this will trigger a recompile and hot-reload, if any of your source files changed

 
## Installation instructions

* Install Vulkan SDK for Windows via LunarG https://vulkan.lunarg.com/
* Set the environment variable `VULKAN_SDK` to the path where you installed the
  Vulkan SDK if this has not been set automatically.

# Progress

- [x] All Examples are working now
- [x] Release x64 Target works
- [x] Debug x64 Target works 
- [x] Loading modules as plugins (.dlls) works
- [x] File watcher ported to Windows
- [x] Shader-hot-reloading works
- [x] Hot-reloading for native code needs some thought
- [ ] le_jobs needs porting - implementation should use Windows' own [Fiber API](https://nullprogram.com/blog/2019/03/28/) 
- [ ] swapchain direct - needs porting for windows, provided driver allows direct rendering
- [ ] image swapchain needs porting - we can't pipe to ffmpeg like we did on linux - or can we?



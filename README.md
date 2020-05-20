# Project Island 🌋🐎 

Project Island is an experimental Vulkan renderer/engine for Linux, written in C/C++.

Island is written for rapid protoyping and tweaking. It allows hot-reloading wherever possible: for GLSL shaders and for all native C/CPP code, including its own core modules. The codebase aims to be strictly separated into independent modules, to make it very fast to compile in parallel.

**Note** The API is under active development, expect lots of change. As such, there are no promises that it might be ready or fit for any purpose, and the code here is released in the hope that you might find it entertaining or instructive.

## Main Features:

* **Native code hot-reloading**: An Island project is made from isolated c/cpp modules, each of which can be tweaked, re-compiled at runtime, and automatically hot-reloaded. The same happens with changes in shader source files. Because of Island's modular architecture, recompilation & reload often takes less than 1 second, while the project keeps running.

* **Fast compile times**: Because of Island's highly modular architecture, everything compiles swiftly, especially on multi-core systems. Typically, compilation from scratch for the whole codebase takes less than 6 seconds, and (re)compilation of an app module takes less than a second.

* **Static release binaries**: While Island is highly modular and dynamic when run in debug, it can compile into a single, optimised static binary for release. 

* **Shader code debugging**: Shader GLSL code may be hot-reloaded too. Any change in shader files triggers a recompile, and (Vulkan) pipelines are automatically rebuilt if needed. Shaders may include other shaders via `#include` directives. Error messages will point at file and line number, and include a brief listing with problematic lines highlighted in context.

* **Vulkan backend**: Island uses a Vulkan backend, which, on Linux, allows you to experiment with GPU features as soon as they are released. The renderer takes care of most of the bureaucracy which comes with modern APIs: Vulkan resources are automatically synchronised, and only allocated on demand. Pipelines are compiled and recompiled when needed. When compiled in Debug mode, Vulkan validation layers are loaded by default.

* **Framegraph**: Resources are allocated on-demand and synchronised automatically using a framegraph system. Most resource properties are *inferred* automatically, reducing the bureaucracy of dealing with modern graphics APIs. The framegraph generates `.dot` files in debug mode, which can be visualised with graphviz.

* **GPU ray tracing** Island supports RTX via the *Khronos Vulkan raytracing extensions*. Creating acceleration structures and shader binding tables is automated and simplified as much as possible. Ray tracing shaders can be hot-reloaded.

* **GPU mesh shaders** gives you - optional - access to Nvidia-specific extensions for mesh and task shaders. These can be used in regular graphics pipelines.

* **Code tweaks**: Near-instant in-code paramter tweaks 

* **Straight to video**: Island can render straight to screen using the direct rendering backend, or use any number of available options for a window-based vulkan swapchain. It's also easy to render straight to an mp4 file, or an image sequence without showing a window, by selecting the appropriate `le_swapchain` specialisation.

* **Multisampling**: minimal effort to enable multisampling, import images, fonts

* **2d drawing context**: Draw thick lines and curves using `le_paths`, which specialises in 2d meshes. This module implements a useful subset of the SVG command palette, and has some extras like the option to apply to hobby algorithm to open and closed bezier curves for automatic smoothing.

* **glTF** Island wraps cgltf for gltf file import, and the `le_stage` module can display and render most features found in gltf files: pbrt materials, vertex animations, morph target animations, and skinning animations.

* **Job-system**: Parallel workloads can be implemented using the `le_jobs` module, which implements a job system using co-routines. Both backend and render modules are designed to minimise resource contention.

## Tools

+ Project generator: Generates scaffolding for new apps, based on templates
+ Module scaffold generator: Generates scaffolding for new modules.

## Examples 

* TODO:
- show screenshot for each example, and short description
- show basic usage example

## Island includes the following helper modules: 

| Module | wraps | Description | 
| --- | :---: | --- | 
| `le_camera` | - | interactive, mouse controlled camera |
| `le_path` | - | draw svg-style paths, can parse simplified SVG strings | 
| `le_tessellator` | earcut/libtess | dynamic choice of tessellation lib |
| `le_imgui` | `imgui` | graphical user interface |
| `le_pixels` | `stb_image` | load image files |
| `le_font` | `stb_font` | truetype glyph sdf, geometry and texture atlas based typesetting |
| `le_pipeline_builder` | - | build vulkan pipelines | 


Similarly, debug builds will automatically load Vulkan debug layers,
while release builds won't.

# Installation instructions

Island should run out of the box on a modern Linux system with
a current Vulkan SDK installed. 

## Depencencies

Island depends on a baseline of common development tools: CMake, gcc, git 

## Install Vulkan SDK 

### Vulkan SDK >= `1.1.92.0`

I recommend to install the Vulkan SDK via the Ubuntu package manager.
Follow the installation instructions via:
<https://vulkan.lunarg.com/sdk/home#linux>.

    wget -qO - http://packages.lunarg.com/lunarg-signing-key-pub.asc | sudo apt-key add -
    sudo wget -qO /etc/apt/sources.list.d/lunarg-vulkan-1.1.92-bionic.list http://packages.lunarg.com/vulkan/1.1.92/lunarg-vulkan-1.1.92-bionic.list
    sudo apt update

    sudo apt-get install vulkan-lunarg-sdk

### Manual Vulkan SDK install and legacy Vulkan SDK installation

Older Vulkan SDKs did not come with a Ubuntu package, and required
some extra steps to set up. These steps are documented in a [separate
readme](legacy_sdk_installation_instructions.md). 

## Island compilation

*Remember to update submodules before building*

    git submodule init
    git submodule update

The CMAKE parameter `PLUGINS_DYNAMIC` lets you choose whether to compile Island as a static binary, or as a thin module with dynamic plugins. I recommend dynamic plugins for debug, and deactivating the option for release builds.


# Project Island

Project Island is a Vulkan r&d renderer/engine for Linux, written in C/C++.

Island is written for rapid protoyping and tweaking. It allows code hot-reloading for GLSL shaders and for native code, including its own core modules. The codebase aims to be strictly separated into independent modules, to make it very fast to compile in parallel.

The API is under active development, expect lots of change. As such, there are no promises that it might be ready or fit for any purpose, and the code here is released in the hope that you might find it entertaining or instructive.

## Main Features:

+ C/C++ code hot-reloading
+ GLSL hot-reloading and debugging 
+ GLSL code allows `#include` directive, debug messages, and hot-reloading are `#include` aware
+ Near-instant in-code parameter tweaks
+ Automatic resource allocation/resolution via Framegraph system
+ Minimal effort to add multisampling/depth passes to renderpasses
+ Vulkan backend
	+ Dynamic GPU resource management/allocation
	+ Dynamic pipeline generation and caching
	+ Dynamic descriptor management and caching
	+ Automatic synchronisation

+ Straight-to-video, image sequence or screen rendering
+ Compiles to a single static binary with Release target
+ Vulkan validation layers loaded by default for Debug target

## Things that make life easier
+ Project generator
+ Module scaffold generator

## Examples 

* TODO:
- show screenshot for each example, and short description
- show basic usage example

## Island includes the following helper modules: 

| Module | wraps | Description | 
| --- | :---: | --- | 
| `le_camera` | - | interactive, mouse controlled camera |
| `le_path` | - | draw svg style paths, can parse simplified SVG strings | 
| `le_tessellator` | earcut/libtess | dynamic choice of tessellation lib |
| `le_imgui` | `imgui` | graphical user interface |
| `le_pixels` | `stb_image` | load image files |
| `le_font` | `stb_font` | truetype glyph sdf, geometry and texture atlas based typesetting |
| `le_pipeline_builder` | - | build vulkan pipelines | 

Island is highly modular and dynamic when run in debug, but compiles
into a single, optimised static binary for release. 

Similarly, debug builds will automatically load Vulkan debug layers,
while release builds won't.

# Installation instructions

Island should run out of the box on a modern Linux system with
a current Vulkan SDK installed. 

## Depencencies

Island depends on a baseline of common development tools: CMake, gcc, git 

## Install Vulkan SDK 

### Current Vulkan SDK >= `1.1.92.0`

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


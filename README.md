# Project Island

Project Island is a Vulkan r&d renderer/engine for Linux based
systems.

Island is made for rapid protoyping and tweaking. It combines native
performance with fast interactive iteration via fast compilation
speeds and code hot-reloading. 

Island is under active development, expect lots of change. As such,
there are no promises that it might be fit for any purpose, and the
code here is released in the hope that you might find it entertaining
or instructive. 

## Island's main features are:

+ C++ source-code hot-reloading
+ GLSL hot-reloading and debugging 
+ Parameter tweaks within source code
+ Dynamic GPU resource management via Framegraph
+ Straight-to-video, image sequence or screen rendering
+ compile to a single static binary with Release target
+ Vulkan validation layers loaded by default for Debug target

## Island includes the following helper modules: 

|-|-| 
|camera | interactive mouse controlled camera |
|path | draw svg style paths, can parse simplified SVG strings |
|tessellator | choice between earcut/libtess for backend |

Island is highly modular and dynamic when run in debug, but compiles
into a single, optimised static binary for release. 

Similarly, debug builds will automatically load Vulkan debug layers,
while release builds won't.


# Installation instructions

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

The CMAKE parameter `PLUGINS_DYNAMIC` lets you choose whether to compile Island
as a static binary, or as a thin module with dynamic plugins. I recommend
dynamic plugins for debug, and deactivating the option for release builds.

## Legacy Vulkan SDK installation instructions


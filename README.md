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


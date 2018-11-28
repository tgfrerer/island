# Installation instructions

## Install Vulkan SDK 

### Current Vulkan SDK >= 1.1.92.0

I recommend installing the Vulkan SDK via the ubuntu package manager. Follow
the installation instructions via <https://vulkan.lunarg.com/sdk/home#linux>.

    wget -qO - http://packages.lunarg.com/lunarg-signing-key-pub.asc | sudo apt-key add -
    sudo wget -qO /etc/apt/sources.list.d/lunarg-vulkan-1.1.92-bionic.list http://packages.lunarg.com/vulkan/1.1.92/lunarg-vulkan-1.1.92-bionic.list
    sudo apt update

    sudo apt-get install vulkan-lunarg-sdk

### Previous Vulkan SDK < 1.1.92.0

Previous SDK did not come with a ubuntu package, and was more cumbersome
to set up. Follow instructions at the end of this document under section
`Legacy Vulkan SDK installation instructions`.


## Island compilation

*Remember to update submodules before building*

    git submodule init
    git submodule update

The CMAKE parameter `PLUGINS_DYNAMIC` lets you choose whether to compile Island
as a static binary, or as a thin module with dynamic plugins. I recommend
dynamic plugins for debug, and deactivating the option for release builds.

## Legacy Vulkan SDK installation instructions

Previous Vulkan SDK installation was slighly more cumbersome, and required
to manually build shaderc so that we could use it as a shared library.

Note that the shaderc header include path in island the source using
shaderc.h might need to be changed for the legacy method to work again.

Download Vulkan SDK from the LunarG [web
site](https://vulkan.lunarg.com/sdk/home#linux)

### Recommended SDK local folder structure

We recommend to extract this and future Vulkan SDK archives into a shared
top-level folder so that you can recreate the following structure:

    VulkanSDK/
        1.1.73.0/
        1.1.77.0/
        @current -> 1.1.77.0

Note that the folder has a symlink, `current`, which points at the
very latest version of the Vulkan SDK. This way, upgrading the SDK
becomes trivial.

### Make Vulkan SDK environment variables visible

Current linux distributions of the Vulkan SDK include a file named
`setup-env.sh`. This file needs to be sourced into your shell on
startup. 

#### Ubuntu 18.0

To source `setup-env.sh`, add the following line to your `~/.profile`:

    `source ~/Documents/VulkanSDK/current/setup-env.sh`

This assumes you have created the VulkanSDK top level folder in
`~/Documents`. If not, update the path to `setup-env.sh` accordingly.
Note that we're using the symlink `current` mentioned above, so that
we're always sourcing the current SDK.

Then add a library search path entry for SDK libs: 

    sudo echo echo "$VULKAN_SDK/lib" > /etc/ld.so.conf.d/vk.conf

Rebuild the library search cache

    ldconfig 

You should then see the correct library paths for the SDK validation
layers, if you issue:

    ldconfig -p | grep libVk

Unfortunately ubuntu won't let `setup-env.sh` update the system-wide
library search paths, `LD_LIBRARY_PATH`, which is why we have to jump
through this hoop.

### Build Vulkan SDK Tools 

Move to the current Vulkan SDK directory, and edit `build_tools.sh`

In method `buildShaderc()` change the build type so that it says: 

    `-DCMAKE_BUILD_TYPE=Release ..`

This is so that the build does not create a 400MB leviathan of a debug
symbol laden library, but a lean, 12MB release version. Note that
building SDK tools creates both static and dynamic version of the
shaderC library, but only adds a link to the static version of the
library to the artifacts folder. Let this build create a symlink to
the dynamic version of the shaderc library. For this, below the line:
    
    ln -sf "$PWD"/libshaderc/libshaderc_combined.a "${LIBDIR}"/libshaderc

add the line:

    ln -sf "$PWD"/libshaderc/libshaderc_shared.so "${LIBDIR}"/libshaderc

Save & close `build_tools.sh`, then build the SDK tools:

    ./build_tools.sh



# Windows port progress tracker

# Installation instructions

* Install Vulkan SDK for Windows via LunarG https://vulkan.lunarg.com/
* Set the environment variable `VULKAN_SDK` to the path where you installed the Vulkan SDK

## Windows changes and caveats

* All Examples are working now
* *imgui* has been moved as a part of the le_imgui module  
* *glm* is not symbolically linked. src/glm is added as a include folder
* *resources* - are now symbolically linked 
* *lib* folder is not no longer needed

* le_device_vk i disabled 
```cpp
featuresChain.get<vk::PhysicalDeviceVulkan12Features>()
	    //    .setShaderInt8( true )
	    //    .setShaderFloat( true )
```
I guess we need to interrogate the driver if they are supported?

As it was failing on my nvidia 1070

* le_instance_vk - i commented out the enabledValidationFeature array as it's empty and vs doesn't want to compile it
* le_pipeline - vs complains about path to string conversions and missing sstream
* le_api_load - was just made to compile for now but it won't do much 
* le_file_watcher - put under define all linux stuff. pending on adding windows stuff
* le_jobs - added empty functions as vs x64 doesn't compile inline asm
* le_rendergraph - fixed executable path functionality for windows
* le_swapchain_direct - removed x11 display from windows
* le_swapchain_img - disabled pipe functionality

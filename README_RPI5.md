
# Island on Raspberry Pi 5

Island is currently able to compile and run natively on Raspberry Pi 5. 

Hot-reloading of shaders, of assets, and of application code works, too. It is enabled by default for Debug targets.

> [!IMPORTANT]
> Island depends on Vulkan 1.3 (for better synchronisation primitives). As the default Raspbian OS on Raspberry Pi 5 (bookworm, as of 2024-11-19) comes only with support for Vulkan 1.2 pre-installed, we must manually compile the latest version of the Mesa Graphics driver. Fret not, the Raspberry Pi 5 is a fairly burly machine, and this will only take about the time needed to brew a decent pot of coffee, and then consume it. You'll be more awake at the end of this process.

## Installation instructions

Getting RPi 5 ready for Island means following these steps:

0. Preparing the OS
1. Compile & install the latest Mesa Graphics driver
2. Compile & install the latest Vulkan SDK

## Prepare the OS

Before doing anything, it might be a good idea to refresh the system, to make sure we're at the latest verison of Raspbian.
```bash
apt-get upgrade
apt-get update
```
## Compile & Install latest Mesa Graphics Driver

Now we should be ready to compile & install a fresh version of the Mesa Graphics Driver.

```bash
# Get latest version of Mesa driver from source. Known-Good: eac8f1d4602cb1e44793b959c5680c92c9854be7
git clone --depth=1 https://gitlab.freedesktop.org/mesa/mesa.git mesa_vulkan

cd mesa_vulkan 


# install mesa build dependencies -- os
apt-get install -y libxcb-randr0-dev libxrandr-dev libxcb-xinerama0-dev libxinerama-dev libxcursor-dev libxcb-cursor-dev libxkbcommon-dev xutils-dev xutils-dev libpthread-stubs0-dev libpciaccess-dev libffi-dev x11proto-xext-dev libxcb1-dev libxcb-*dev bison flex libssl-dev libgnutls28-dev x11proto-dri2-dev libx11-dev libxcb-glx0-dev libx11-xcb-dev libxext-dev libxdamage-dev libxfixes-dev libva-dev x11proto-randr-dev x11proto-present-dev libclc-19-dev libelf-dev git build-essential mesa-utils libvulkan-dev ninja-build libvulkan1 libdrm-dev libxshmfence-dev libxxf86vm-dev libwayland-dev wayland-protocols libwayland-egl-backend-dev cmake libassimp-dev python3-full

# before we can install python components via pip, we must create a python venv
python venv .

# install mesa python build dependencies
./bin/pip install meson PyYAMl mako
```

Then create a `build.sh` file in the mesa directory with the following contents:

```bash
#!/bin/bash -e

CFLAGS="-mcpu=cortex-a72"
CXXFLAGS="-mcpu=cortex-a72"
THIS_DIR=$(pwd)
PATH="$THIS_DIR/bin:$PATH"

echo $PATH

./bin/meson setup --reconfigure --prefix /usr -Dgles1=disabled -Dgles2=enabled -Dplatforms=x11,wayland -Dvulkan-drivers=broadcom -Dgallium-drivers=v3d,vc4,zink,virgl -Dbuildtype=release  build
ninja -C build -j4
sudo ninja -C build install
```

Then, execute `build.sh` -- With a little bit of luck, this should compile the mesa drivers and install them onto your Raspberry Pi 5.

> [!IMPORTANT]
> You want to reboot at this point

Once rebooted, you should see the following when calling `vulkaninfo`

```txt
(...)
Device Properties and Extensions:
=================================
GPU0:
VkPhysicalDeviceProperties:
---------------------------
        apiVersion        = 1.3.303 (4206895)
        driverVersion     = 24.99.99 (101068899)
        vendorID          = 0x14e4
        deviceID          = 0x55701c33
        deviceType        = PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU
        deviceName        = V3D 7.1.7.0
        pipelineCacheUUID = 76b0da65-6c5c-0fec-bf7d-0c8ac906fc0c
(...)
```

## Compile & Install Vulkan SDK

Follow the steps outlined here to setup the Vulkan SDK. If in doubt, you can also consult the [instructions provided by lunarg](https://vulkan.lunarg.com/doc/view/latest/linux/getting_started.html) for compiling the SDK.

```bash
cd "$HOME"

VULKAN_SDK_DIR="$HOME/vulkan_sdk"

mkdir -p "$VULKAN_SDK_DIR"
cd "$VULKAN_SDK_DIR"

curl -o vulkan-sdk.tar.gz https://sdk.lunarg.com/sdk/download/latest/linux/vulkan-sdk.tar.gz

#unpack the sdk files
tar xfv vulkansdk.tar.gz

# create a symlink so that we can later upgrade the sdk more easily
ln -s `find . -maxdepth 1 -type d -name '1.*' -print -quit` current

# move into the vulkan sdk directory
cd current

# remove pre-built x86_64 artifacts
rm -r x86_64

# build vulkan sdk components as needed (this will take some time)
./vulkansdk --maxjobs vulkan-headers vulkan-loader spirv-headers shaderc layers

# source vulkan sdk into your bash environment 
echo "$VULKAN_SDK_DIR/current/setup-env.sh" >> "$HOME/.profile"
```

Once the Vulkan SDK is installed, it might be a good idea to restart the Raspberry Pi. Once rebooted, verify that the install was successful by calling: `echo $VULKAN_SDK`. If successful, this should print out the correct path to the local Vulkan SDK installation directory.


## Download & Compile Island

As for compiling Island, and its example applications, follow the [Setup Instructions in the main Readme](README.md#setup-instructions)

> [!NOTE]
> Most examples should work, with a few having quirks. 

* The Hello World Example will not work out of the box, since the Raspberry Pi 5 does not support texture images at 8K resolution. Rescale the images first. 
* Similarly the Screenshot Example will not work out of the box. Rescale the image to `2040x1024` and all will be fine...

* The Bitonic Merge Sort Example will not work out of the box, since the Raspberry Pi 5 does not support workgroup sizes of `1024`. Set this to `256` and it will work.

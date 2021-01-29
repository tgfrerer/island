# GPU Bitonic Merge Sort Example (Vulkan Compute)

|  Before | After  |
:---: | :---
![screenshot](resources/images/jonatan-pie-GQIGk5L1Ppk-unsplash.jpg) | ![screenshot](screenshot.jpg)
| Initial image | Image after 55 of 190 steps |

Applies a parallel sorting algorithm to a given buffer of pixel data. 

The algorithm runs on the GPU via compute shaders. Pixels are sorted by perceptual brightness, via the [Oklab colorspace](https://bottosson.github.io/posts/oklab/). 

The bitonic merge sort algorithm and its implementation are described in much more detail (diagrams! formulas! mini-visualisations!) in the [companion blog post](https://poniesandlight.co.uk/reflect/bitonic_merge_sort/) to this example.

Note that by default, the algorithm is applied in extreme slow-motion (if let run freely, it could easily sort 1M pixels under 4ms) - this is for visual effect. Accelerate your sort by pressing the `CURSOR_DOWN` key (for more Keyboard Controls see below).

You can drag and drop image files onto the application window to see their pixels sorted. Image files with a width of 1024px will look best.

## Keyboard Controls:

* `SPACEBAR`: reset pixel buffer to random noise
* `CURSOR_UP`: increase slow motion delay
* `CURSOR_DOWN`: decrease slow motion delay (0 delay switches off slow motion and runs optimised algorithm at full speed)
* `F11` : toggle app fullscreen

## Techniques used: 

* Vulkan compute
* visualising raw buffer data via fragment shader
* explicit cpu-side synchronisation
* compute shader: workgroup local memory 
* image loading
* implements bitonic merge sort (based on the [alternative representation](https://en.m.wikipedia.org/wiki/Bitonic_sorter#Alternative_representation) shown on wikipedia

## Build instructions

Configure build environment using CMake: 

    mkdir build 
    cd build
    cmake -G Ninja ..

Note that if you are using Qt Creator you may skip manually setting up the build environment, and simply open the project CMakeLists.txt using Qt Creator.

Build using Ninja:

    ninja

Run application: 

    ./Island-BitonicMergeSortExample



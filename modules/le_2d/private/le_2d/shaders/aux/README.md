# Vello shaders 

The original shaders from vello were written in wgsl - which we need to convert into vulkan-flavoured spirv for Island. 

Unfortunately, at the time of writing, Naga, the rust-based WGSL shader compiler, produces sub-par spir-v output; this causes validation layers when loading the shaders.

There is another WGSL shader compiler available, which is part of Google Dawn, the reference implementation for webgpu. It is quite cumbersome to compile this but it's possible.

Here is what we had to do:

```bash
git clone https://dawn.googlesource.com/dawn
cd dawn
```

Then you need to pull the dependencies for dawn -- don't use git for this, but use the python script that is part of dawn for your own sanity: 

```bash
python tools/fetch_dawn_dependencies.py
```

This should shallow-clone all the dependencies that are needed for building dawn/tint.

```bash
cmake -S . -B ./out/Release                 \
-G Ninja                                    \
-D CMAKE_BUILD_TYPE=Release                 \
-D CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded \
-D ABSL_MSVC_STATIC_RUNTIME=ON              \
-D DAWN_BUILD_SAMPLES=OFF                   \
-D DAWN_BUILD_TESTS=OFF                     \
-D DAWN_ENABLE_D3D12=OFF                    \
-D DAWN_ENABLE_D3D11=OFF                    \
-D DAWN_ENABLE_NULL=OFF                     \
-D DAWN_ENABLE_DESKTOP_GL=OFF               \
-D DAWN_ENABLE_OPENGLES=OFF                 \
-D DAWN_ENABLE_VULKAN=ON                    \
-D DAWN_USE_GLFW=OFF                        \
-D DAWN_ENABLE_SPIRV_VALIDATION=OFF         \
-D DAWN_DXC_ENABLE_ASSERTS_IN_NDEBUG=OFF    \
-D DAWN_FETCH_DEPENDENCIES=OFF              \
-D DAWN_BUILD_MONOLITHIC_LIBRARY=SHARED     \
-D TINT_BUILD_TESTS=OFF                     \
-D TINT_BUILD_SPV_READER=ON                 \
-D TINT_BUILD_SPV_WRITER=ON                 \
-D TINT_BUILD_CMD_TOOLS=ON                  \
-D DAWN_FORCE_SYSTEM_COMPONENT_LOAD=ON
```

Then build `tint`:

```bash
cmake --build ./out/Release --target tint --parallel
```

you should now have a binary named ```tint``` in the `./out/Release` folder. Make it available to your system path.

Then, you can download the vello shaders from the [vello shaders repository](https://github.com/linebender/vello/tree/8f2f2564127812362d2c57ded20cad369e3a00fe/vello_shaders).

Place the script `tint_to_spirv.sh` in the vello shaders directory. Create a subfolder named `spv`. If you then execute `tint_to_spirv.sh` on the vello shaders directory, it should generate spirv for all the shaderfiles, and place the spirv artifacts into the `spv` directory. 

You might have to rename the shader files because tint does not respect the out file name parameter, and adds `.main` to any file that it generates.

Replace the `.spv` files in the folder `../vello/` with the files that you just generated. Then run the script in `../vello/compress_shaders.sh` to generate `.inl` files that can be included as blobs into the cpp source code. Voila...

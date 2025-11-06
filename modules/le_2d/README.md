# Le2D

<img width="1012" height="550" alt="le_2d illustration" src="https://github.com/user-attachments/assets/b76eae09-ee5a-481b-aee6-e58fdda27268" />

Le2D is a fully GPU-accelerated 2D drawing context. It supports drawing thick lines, fills, quadratic and cubic bezier curves, rounded rectangles (optionally with soft shadows). 2d commands are recorded into specialized command lists, which can be cached, and concatenated, and finally drawn via the 2d context.

It's super performant.

Le2D is used to draw the user interface of [le_rendergraph_visualizer](/modules/le_rendergraph_visualizer).


# How to use le_2d 

`Le2D` works as a 2d drawing context with its own command encoder, `Encoder2D`. 2d command encoders are temporary, freestanding objects.

You issue drawing commands into a 2d command encoder. Once finished recording commands, you pass the encoder to the 2d drawing context for drawing via its `update` method.

Add a 2d drawing context to your app object:

```cpp
struct app_o {
 // ... 
 Le2D ctx_2d;
 // ...
}
```

To render the drawing context to an image, add this in your app's `update()` method

```cpp

Encoder2D encoder_2d {};

encoder_2d
	.transform()                           // record commands
	.path_begin(le_2d::FillStyle::NonZero) // ..
	.path_end()
	// record more commands
	;


self->ctx_2d.update( rg, encoder_2d, self->img_output, &self->img_output_info );
```


Where `rg` is the current rendergraph, `encoder_2d` the encoder that you want to draw, and `self->img_output` is the image into which you would like to draw the 2d context. `self->img_output_info` is the image info for the output image. 

# How to use `Encoder2D`

> [!Caution] 
> Encoder2D comes only with limited guardrails, it's possible to cause a context loss via an incorrect sequence of encoded 2d commands. The Encoder2D c++ façade helps with most, but can't protect from all issues.

## Rules for Encoder2D:

* Every path must begin with a `transform` command -- even if the transform is empty. 
* A `colour` command **must** happen after `transform` and before `path_begin`, for any path that is **not** a clipping path
* Every `path_begin` must have a matching `path_end`.

### Special Rules for Clipping Paths:

Clipping paths define a sub-drawing context. Only what is contained within the area of the clipping path is affected by the clipping path

* Must begin with `transform`
* Must not contain `color` command.
* Must describe a valid **filled** (not stroke) path: 
	* `path_begin` .. path commands ..  `path_end`
* Must be followed by `begin_clip` -- this opens the clip scope
	* Can contain other drawing elements in here 
* Ended by `end_clip` -- this closes the clip scope

## Concatenating Encoders:

2d command encoders do not have to be temporary; you can store command encoders and concatenate commands from command encoders. You can apply a transform over commands in a command encoder when concatenating. This allows you to cache and re-use commands.

## Working with le::Transform2D

Transforms apply right-to-left (just as matrices are multiplied in GLSL). Transforms have an `inverse` method which calculates and returns their inverse.

---


# LICENSE

Much of the 2D GPU rendering routines of this module were adapted from [vello](https://github.com/linebender/vello) source code.

We are grateful to the vello authors for releasing Vello under a permissive license.

Vello is released by the Vello Authors under [Apache License, Version 2.0](https://github.com/linebender/vello/blob/main/vello_shaders/LICENSE-APACHE), or the [MIT License](https://github.com/linebender/vello/blob/main/vello_shaders/LICENSE-MIT).

You can find the original vello renderer implementation here:
<https://github.com/linebender/vello>


GPU rasterizer shaders are taken from vello_shaders <https://github.com/linebender/vello/tree/main/vello_shaders> and translated to SPIR-V using Tint, the shader compiler which is part of Dawn, the WebGPU reference implementation. You can find the compilation instructions and scripts under: [private/le_2d/shaders/supplementary_materials](/modules/le_2d/private/le_2d/shaders/supplementary_materials).


Vello shaders are licensed under [Apache License, Version 2.0](https://github.com/linebender/vello/blob/main/vello_shaders/shader/LICENSE-APACHE), [MIT](https://github.com/linebender/vello/blob/main/vello_shaders/shader/LICENSE-MIT), or [Unilicense](/island/modules/le_2d/private/le_2d/shaders/supplementary_materials).



# Thoughts

## Renderer Architecture: 

	1. We should not store state with graphbuilder - instead use graph builder
	   to manipulate frame state - memory which belongs to that particular
	   frameData.
  
	2. We need a way to record commands in an abstraction of command buffers, so
	   that the backend can translate these commands back to an api specific
	   version of command buffers.
  
	3. We might need some commands to operate on resources - these need syncing
	   inserted at the correct time.

Generally, we want to separate command encoding (which happens in Engineland)
from command buffer generation (which happens in the backend). 

# Inside of a Renderpass, to draw something, we need to:

	1. Set up Resources (Images)

	2. Set up Pipeline (Shader)

	3. Set up Vertex Data (Attributes) and Indices

	4. Set up Uniform Data

--------------------------------------------------------------------------------

# Next steps

	* Create Renderpass from FrameData (programmatically create Renderpass)
	  [Needs ResourceManager for Images?]
	* Add ExecutePass callback to renderpass

## Rendergraph

	1. Merge passes with same order into *one* Renderpass, as subpasses
	
## How do we want the `render` callback to look like?

    * set PSO parameters: we want to set textures, uniforms
    


	```cpp
	
	void (* render) (void * user_data){
	
	// we use user_data to have access to the objects we actually want to
	render

	encoder
		.setPSO("passthrough")
		.setTexture("src_texture_0", resources.getTexture("textureName", eRO))
		.setVertices(resources.getBuffer("vbuf_1"));

	}
	```

## What is the minimum subset of commands we need to implement for intermediate command buffers?

	* bindPipeline
	* bindIndexBuffer
	* bindDescriptorSets
	* draw
	* bindVertexBuffers

## How can we intercept any resource-modifying calls?

    We need to specify the API for resource-acquisition (buffers, images)

## How do we store command parameters?
	--> we can use an offset into a parameter buffer, cast it to command
	parameter type
## How do we store command data?
	--> offset into buffer, header tells us size and type of data

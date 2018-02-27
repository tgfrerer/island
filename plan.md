
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

    1. TODO: Merge passes with same order into *one* Renderpass, as subpasses
	
## How do we want the `render` callback to look like?

    * set PSO parameters: we want to set textures, uniforms
    
We call back providing an encoder (encoder for command list) and `void *
user_data` - the encoder is used to create an intermediate representation of a
renderpass. 

	```cpp
	
	void (* render) (encoder, void * user_data){
	
	// we use user_data to have access to the objects we actually want to render

    // this basically mirrors the drawcommand syntax
    
	encoder->bindRenderPipeline("passthrough")
	encoder->setTexture(set_number, binding_position, binding_index, texture_id) // pipeline paramters
    encoder->bindDynamicDescriptorData([buffers],[dynamic_offsets]); // pipeline parameters
    encoder->bindVertexBuffers([buffers],[dynamic_offsets]);
    encoder->bindIndexBuffer(buffer,offset); // optional
    encoder->setViewport(/*{viewportData}*/);
    encoder->setLineWidth(1.f);
    encoder->drawPrimitiveIndexed(...);
	}
	```

### How do we specify pipeline parameters? 

For now we want to be able to specify two things: 

    * buffers (for UBO structs) and 
    * image resources.

We want to be able to set parameters directly - these parameters then map to
descriptorSets. Ideally, we should be able to map them by index, so that this
maps directly to a DescriptorSet, which can then be bound:

```cpp
    mVkCmd.bindDescriptorSets(
		pipelineBindPoint(graphics)	  // use graphics, not compute pipeline
		PIPELINE_LAYOUT               // VkPipelineLayout object used to program the bindings.
		0,                            // firstset: first set index (of the above) to bind to - mDescriptorSet[0] will be bound to pipeline layout [firstset]
		boundVkDescriptorSets.size(), // setCount: how many sets to bind
-->     boundVkDescriptorSets.data(), // the descriptorSets to bind
        dynamicBindingOffsets.size(), // dynamic offsets count how many dynamic offsets
		dynamicBindingOffsets.data()  // dynamic offsets for each descriptor
	);
```

Perhaps we can introduce something like a descriptor table and descriptors can
be generated upfront in vulkan from all requested descriptors for the frame.

We could use this descriptor table with VkDescriptorUpdateTemplateKHR, so that
we don't have to create separate write descriptors for everything.

### In Vulkan, how is a resource bound to a pipeline?

    1. pipeline defines pipelineLayout
    2. pipelineLayout is an array of DescriptorSetLayouts
    3. DescriptorSetLayout is an array of DescritptorSets
    4. DescriptorSet is an array of Bindings
    5. Bindings has an array of Descriptors

In Vulkan, you bind resources to a pipeline by binding descriptorSets. If you
want to associate a particular texture with a pipeline you do so by creating a
descriptorSet which references the texture. 

Descriptors must be allocated from designated pools - and they must be written
to, so that they can be used. 

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

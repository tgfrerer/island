# PLAN

# TODO

 * We're quite gung-ho about `le_buffer` in `le_backend_vk` when we create a
   buffer - ideally, a buffer is reference-tracked. we're currently not
   tracking the lifetime of a buffer, and we're also allowing other frames to
   alias parts of it. this means, it becomes complicated to track the lifetime
   of a `le_buffer` : CLEAN THIS UP.

## (A)

    * draw a triangle:

        + allocate a scratch buffer per frame.

        + map the scratch buffer per frame.

        + use scratch buffer memory when you write vertex data

        + uses of the scratch buffer means that the scratch buffer is bound at
          the binding address specified. 

        + later introduce allocator which will give you access to scratch data.
    
        + we must somehow keep track of the state of bound attributes, and
          store these with draw commands?! 
      
          That, or we bind vertex buffers explicitly. It appears that APIs like
          Metal keep track of the binding state for you and will automatically
          bind when you issue `setVertexData(...,bindingIndex)`. 
          
          We could automatically add a bind command in here, but then, binding
          is not done piece-meal, it's better to bind the full set. But we can
          do it.

        + If we wanted to have a stateless solution it would be better to bind
          buffers in bulk: we can achieve this by adding a method which allows
          us to bind buffers in bulk. A stateless solution needs to happen on
          the next-higher abstraction layer, i think, as commandbuffers
          themselves are by definition stateful, and our encoder is a wrapper
          around the command buffer. 

## (B)

    * find out: what happens to method-static variables upon api reload - do
      these get re-initialized?

    * think: can we use macros to generate encoder methods?

## (C)

    * find a better way to store window surface- it should probably live inside
      the backend, tagged with window name, or perhaps it should be owned by
      the swapchain which uses it, so that it can be deleted at the correct
      time. 

    * make sure `reset_swapchain` is clean - at the moment it complains about
      deleting an object which is currently in use by a command buffer.

----------------------------------------------------------------------

# LEARNED SO FAR:


    * we can pass around LE objects (buffers, attachments, pipelines, etc.) as
      opaque handles, effectively using a pointer-to-object-type which
      represents an object:

      ```c
      typedef struct our_object_t* our_object;
      ```
    
    * instead of passing the api via parameter- you can retrieve it via the
      registry inside the function which uses it, and store it as a
      function-level static variable.



----------------------------------------------------------------------
# RESOURCE HANDLING

    * when we encode commands which mention resources, resources are referenced
      by opaque ids retrieved via the engine. the engine then patches the
      command stream and substitutes any engine-specific resource ids by
      api-specific ids. this happens between command recording and command
      processing. 

----------------------------------------------------------------------
+ find a way to minimize calls to the api registry - once the api has been
  registered, the pointer address will not change, only the contents of the
  struct the pointer points to - looking up the api from the registry creates
  some unnecessary overhead, especially when running the app statically
  compiled, where reloading is impossible.

+ create renderpass programmatically

* minimal methods for encoder to draw into a frame: 
    * we need a buffer for vertex data
    * a simple pass-through pipleine 
        * pass-through shader
        * descriptorset
     

----------------------------------------------------------------------

* Create a templating script to generate class scaffold so you don't have to
  type that much boilerplate.


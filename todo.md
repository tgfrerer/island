# Todo


## (A)

    * implement setVertexBytes
        vertex data must first be stored inside encoder- in process_frame this is transferred to GPU coherent memory. Don't use updateBuffer for this. You can just write to gpu-mapped memory, if you have an allocator for this.

    * add a method to store an attribute for a specific binding location
    * add scratch buffer for vertex data
    * draw a triangle
  

    + allocate a scratch buffer per frame.
    + map the scratch buffer per frame.
    + use scratch buffer memory when you write vertex data
    + later introduce allocator which will give you access to scratch data.


## (B)

    * find out: what happens to method-static variables upon api reload - do
      these get re-initialized?

    * instead of passing the api via parameter- you can retrieve it via the
      registry inside the function which uses it, and store it as a
      function-level static variable.

    * think: can we use macros to generate encoder methods?

## (C)

    * find a better way to store window surface- it should probably
      live inside the backend, tagged with window name, or perhaps it
      should be owned by the swapchain which uses it, so that it can
      be deleted at the correct time. 

    * make sure `reset_swapchain` is clean - at the moment it
      complains about deleting an object which is currently in use by
      a command buffer.

----------------------------------------------------------------------

+ find a way to minimize calls to the api registry - once the api has
  been registered, the pointer address will not change, only the
  contents of the struct the pointer points to - looking up the api
  from the registry creates some unnecessary overhead, especially when
  running the app statically compiled, where reloading is impossible.

+ create renderpass programmatically

* minimal methods for encoder to draw into a frame: 
    * we need a buffer for vertex data
    * a simple pass-through pipleine 
        * pass-through shader
        * descriptorset
     

----------------------------------------------------------------------

* Create a templating script to generate class scaffold so you don't
  have to type that much boilerplate.


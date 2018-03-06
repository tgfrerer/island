# Todo

----------------------------------------------------------------------

## (A)

    * create command buffer using encoder data    
    * find out: what happens to method-static variables upon api reload - do
      these get re-initialized?
  
## (B)

    * store encoder data with frame
    * instead of passing the api via parameter- you can retrieve it via the
      registry inside the function which uses it, and store it as a
      function-level static variable.

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

+ use encoder to draw basic triangle (this will allow us to test
  backend)
+ create renderpass programmatically
+ use backend to record command buffers, to submit command buffers, to
  track gpu object state  

* have one encoder per renderpass
    * make sure encoder stores into correct (local) frame
    * add minimal encoder methods
    * add storage to encoder - somewhere to store scissors, viewports,
      buffer data - so that it can be pieced back together later. 
    
* minimal methods for encoder to draw into a frame: 
    * we need a buffer for vertex data
    * a simple pass-through pipleine 
        * pass-through shader
        * descriptorset
     

----------------------------------------------------------------------

* Create a templating script to generate class scaffold so you don't
  have to type that much boilerplate.


#Todo

+ use encoder to draw basic triangle (this will allow us to test backend)
+ create renderpass programmatically
+ use backend to record command buffers, to submit command buffers, to track
  gpu object state  

------------------------------

* have one encoder per renderpass
    * make sure encoder stores into correct (local) frame
    * add minimal encoder methods
    * add storage to encoder - somewhere to store scissors, viewports, buffer
      data - so that it can be pieced back together later. 
    
* minimal methods for encoder to draw into a frame: 
    * we need a buffer for vertex data
    * a simple pass-through pipleine 
        * pass-through shader
        * descriptorset
     

------------------------------

* Create a templating script to generate class scaffold so you don't have to type that much boilerplate.


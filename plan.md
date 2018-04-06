# PLAN

## Data Flow

    * if two objects communicate by sharing an object, communication should be
      strictly pipeline-style, i.e. the consumer is not allowed to write back
      to the object borrowed from the provider. This makes it easier to reason
      about data-flow. we should also enforce that borrowing is an atomic
      operation, i.e. the provider *must not* be able to write to an object
      once it has been handed out for reading.

## Structs as Data

Think of *structs as data* - your main interaction should be with data,
sequences of functions acting collaboratively on data. 

## Objects as abstract *state* machines

You use objects and object methods if you want objects to update their internal
state. The internal state of an object may be hidden, and this is how you
implement encapsulation, and abstraction (an object may decide for itself how
it implements a certain method).

* backend should not need to know about graphbuilder - just pass passes in the
  correct order when calling backend methods

    * for this to work, encoder needs to come out of the graphbuilder, and
      needs to live with framedata. this is a better place for encoder anyway.

Let's refer to all resources in the renderer using opaque `uint64_t` ids. These
ids should be based on hashing the name - but we could decide later how we want
to handle id generation and retrieval if we put the id generator into its own
method. 

Hashing the name has the benefit that this can be done independently and that
no locking has to occur, and everyone who hashes a name should get the same
result. Also, hashing is potentially executable as a constexpr, so the id will
not have to be calculated at runtime at all.

Resources are introduced in passes during their setup stage - if a resource is
permanent, the backend will remember the reource id, this also means that if a
permanent resource gets dropped during a resource pass, the backend will mark
that physical resource for recycling once no frame which is in-flight still
uses it.  

    > does this mean we could also introduce shader resources in setup stage?

Where should we *declare* resources? 

    * if we declare resources at the point of first use, this means that
      resources always need to be declared using their full resource
      descriptor, which means all callbacks need to have access to the same
      version of the resource descriptor. 

    * if we declare resources upfront, this means we have one central point
      where resources are defined. 

# ROADMAP

    * all resources should be the of type `resource_descriptor`, so that their
      dependencies can be tracked. The interface for tracking resource
      dependencies is the same for textures and for buffers.

    * we should use descriptors so that resources can be instantiated when we
      first need them.

    * add sort-key to encoder, so that we can decouple calling the callbacks
      from generating the command buffers.

# TODO

 * combine `resource` and `buffer`- a buffer is-a resource, as an image is-a
   resource. We define a resource as something which has memory backing on the
   GPU and needs synchronisation.
 
 * we want three different types of passes: render, transfer, compute. Each
   pass has a list of inputs, and a list of outputs.

   Rendergraph is calculated based on module, which contains a list of
   pre-sorted passes. 


 * backendFrameData also has a resource table - and a type ResourceInfo - we
   should consolidate this with our resource type.


## (A)

    * simplify linear allocator: 

        Currently, one leBuffer can have more than one allcator - this can turn
        into a nightmare. allocator must own buffer exclusively. memory may be
        handed out in chunks per buffer, but buffer must be owned exclusively
        by allocator. 

## (B)

    * think: can we use macros to generate encoder methods?

    * get Renderdoc to actually work on both of your test systems.

## (C)

    * find a better way to store window surface- it should probably live inside
      the backend, tagged with window name, or perhaps it should be owned by
      the swapchain which uses it, so that it can be deleted at the correct
      time. 

    * make sure `reset_swapchain` is clean - at the moment it complains about
      deleting an object which is currently in use by a command buffer : the
      view, which is owned by the swapchain.

----------------------------------------------------------------------

# LEARNED SO FAR:


    * we can pass around LE objects (buffers, attachments, pipelines, etc.) as
      opaque handles, effectively using a pointer-to-object-type which
      represents an object:

      ```c
      typedef struct our_object_t* our_object;
      ```
    
      This is also how APIs like Vulkan present their objects.

    * instead of passing the api via parameter- you can retrieve it via the
      registry inside the function which uses it, and store it as a
      function-level static variable.



----------------------------------------------------------------------

# RESOURCE HANDLING

    * when we encode commands which mention resources, resources are referenced
      by opaque ids retrieved via the engine. the engine then patches the
      command stream and substitutes any engine-specific resource ids by
      api-specific ids. this happens between command recording and command
      processing, in a method called `renderer_acquire_backend_resources`. 

----------------------------------------------------------------------

# WHO OWNS RESOURCES

    * The BACKEND owns all resources. It knows all about resources. The
      RENDERER, being the front-end, acts as an interface, it deals only with
      opaque handles of resources.

    * The BACKEND does everything which is API specific.


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


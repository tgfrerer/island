# PLAN

# ROADMAP

* all resources should be the of type `AbstractResource`, so that their
  dependencies can be tracked. The interface for tracking resource
  dependencies is the same for textures and for buffers.

* we should use descriptors so that resources can be instantiated when we
  first need them.

* add sort-key to encoder, so that we can decouple calling the callbacks
  from generating the command buffers.

# TODO

* Remove namespaces in header files - these are not compatible with other
  programming languages.
 
* we want three different types of passes: render, transfer, compute. Each pass
  has a list of inputs, and a list of outputs.

  Rendergraph is calculated based on module, which contains a list of
  pre-sorted passes. 

* Better distinguish between renderpass types when creating vulkan command
  buffers

* Rename internal structure to `Batch` instead of `Renderpass` in backend.

## (A)

## (B)

## (C)

* find a better way to store window surface- it should probably live inside the
  backend, tagged with window name, or perhaps it should be owned by the
  swapchain which uses it, so that it can be deleted at the correct time. 

----------------------------------------------------------------------

# STRATEGY

## Data Flow

* if two objects communicate by sharing an object, communication should be
  strictly pipeline-style, i.e. the consumer is not allowed to write back
  to the object borrowed from the provider. This makes it easier to reason
  about data-flow. we should also enforce that borrowing is an atomic
  operation, i.e. the provider *must not* be able to write to an object
  once it has been handed out for reading.

## Structs as Data

Think of *structs as data* - your main interaction should be with data,
sequences of functions acting collaboratively on data. There is **no
inheritance** whatsoever. 

## Objects as abstract *state* machines

You use objects and object methods if you want objects to update their
internal state. The internal state of an object may be hidden, and this is
how you implement encapsulation, and abstraction (an object may decide for
itself how it implements a certain method).

Let's refer to all resources in the renderer using opaque `uint64_t` ids.
These ids should be based on hashing the name - but we could decide later
how we want to handle id generation and retrieval if we put the id
generator into its own method. 

Hashing the name has the benefit that this can be done independently and
that no locking has to occur, and everyone who hashes a name should get
the same result. Also, hashing is potentially executable as a constexpr,
so the id will not have to be calculated at runtime at all.

Resources are introduced in passes during their setup stage - if
a resource is permanent, the backend will remember the reource id, this
also means that if a permanent resource gets dropped during a resource
pass, the backend will mark that physical resource for recycling once no
frame which is in-flight still uses it.  

    > does this mean we could also introduce shader resources in setup
    > stage?

Where should we *declare* resources? 

* if we declare resources at the point of first use, this means that
  resources always need to be declared using their full resource
  descriptor, which means all callbacks need to have access to the same
  version of the resource descriptor. 

* if we declare resources upfront, this means we have one central point
  where resources are defined.

# Data + Algorithms
+ Data and algorithms ideally are orthogonal, which means that we can
  organise our code to follow a "pipeline" or "production line" approach,
  where data is "owned" by the algorithm that works on it, and only one
  algorithm at a time interacts with a strictly defined subset of the
  data. 
  
+ I believe rust for example enforces this through the borrow-checker.
  
+ For performance reasons, however, we don't necessarily want the
  granularity of rust, we only want to make sure that each execution
  context owns their memory resources (because that's the only reason for
  contention/race conditions)

  This means that algorithms can run, as long as their data is available,
  in parallel, without risk of race conditions or contention.

# Normal Accidents
+ Happen if systems are: 

    1. Highly Complex 
    2. Interactive 
    3. potentially catastrophic

+ We define "catastrophic" in this context as a fatal error
+ The book of the same name is worth reading

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
  registry inside the function which uses it, and store it as
  a function-level static variable.

----------------------------------------------------------------------

# RESOURCE HANDLING

* when we encode commands which mention resources, resources are
  referenced by opaque ids retrieved via the engine. the engine then
  patches the command stream and substitutes any engine-specific resource
  ids by api-specific ids. this happens between command recording and
  command processing, in a method called
  `renderer_acquire_backend_resources`. 

* we could abstract resources further, resources are stored internally in
  one large array, and resource handles effectively consist of a 32 bit
  offset into this array, plus a 32 bit header (msb) which indicates the
  type of the object, and some access information.

----------------------------------------------------------------------

# WHO OWNS RESOURCES

* The BACKEND owns all resources. It knows all about resources. The
  RENDERER, being the front-end, acts as an interface, it deals only with
  opaque handles of resources.

* The BACKEND does everything which is API specific.

---------------------------------------------------------------------- 

# Island-framework

* Create a templating script to generate class scaffold so you don't have
  to type that much boilerplate.

* structure framework so that you may have multiple test applications
  using it

----------------------------------------------------------------------

# Applications

* A video post-processing pipeline

# Features

- resource lookup in `command_buffer_encoder_interface`

when we setup command buffers, we declare all resources - this means all
resource handles can be put into a vector, where each resource is only
referenced once. -> This becomes the vector of frame-available resources

when we acquire resources, we create a matching vector which has the
vulkan object id for each resource - indices match frame available
resources vector.

when we record command buffers, we store index into the frame resource
list - that way we can be much faster at assigning resources

- Something like an allocator which comes with the frame context - an
  arena allocator which provides you with storage for any temporary
  objects that different libraries want to create, and which have
  a frame-bound lifespan; this will need to be thread-safe but should be
  quite trivial to attain, if the `used_mem` index is an atomic `uint32_t`
  value, for example.

# Next Step Features
- create a project generator
- entity-component system for nodes
- add materials for renderer
- add a geometry generator module - something for us to experiment with
- implement a post processing effects pipeline
- implement pbrt materials based on gltf reference implementation
- investigate intel performance primitives for multithreading
- image writer swapchain (for post processing)
- implement compute pipeline - and compute passes
- refactor shader related methods/pipeline state object elements to their
  own translation unit within backend
- implement pipeline generation as a channeled op - per encoder first,
  then consolidate those elements generated within a frame
- use opaque pointer for pso object handle

# Long-Term aspirations
- reduce compile times with glm: template specialisations
- improve ergonomics, reduce lines to type
- project generator for simple apps
- project generator for apps

# Todo
- use opaque handle to reference pso instead of bare `uint64_t`
- create a minimal test app which draws a triangle


# What I'm unhappy with
- the way a lower sytem calls into a higher level system when executing
  renderpass callbacks is too complicated - it also appears to be a case
  of the tail wagging the dog.
- i can see no benefit in having renderpass setup being a callback
- the api for declaring resources is too convoluted, and too noisy, I feel
  there is a lot of double bookkeeping going on.
- c++ template compile times (yikes)

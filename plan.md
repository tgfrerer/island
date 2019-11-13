# PLAN

# ROADMAP

# TODO

- move this file along with anything that's not the main readme to
  a folder labelled "meta"
- add instructions for how to create an empty app or module using the
  generators
- add summary of what Island is to readme
- add screenshot to readme
- add code example to readme which demonstrates something island does
  well

## (A)
- **relative size for renderpass attachments instead of absolute
  pixels dimensions** - the idea is that sometimes (like, with bloom)
  we don't know the size for the final target buffer, but we know that
  we don't need an effect etc to render at the full resolution, but
  that a fraction of the resolution will be enough.

## (B)

- find a way to deal with deactivated bindings (assert in #1501, `le_pipeline.cpp`)
- embed shader code as spir-v
- Architect a usability layer on top of base framework, which makes common
  operations easy, and DRY. Images are a good point to start.
- implement automatic mip chain generation 
- add a coordinate axes geometry generator - so that we can debug
  cameras and handedness.

## (C)
- job-queue: implement a way to not busy-wait, if there is nothing
  more on the queue, if possible. 
- It should not be possible accidentally to provide a texture handle
  for a `resource_handle` where we expect an image or buffer handle.
- Compute Passes
    - combine setArgumentData and bindArgumentBuffer in encoder so that
      we're using a single path in the backend for both these methods (we
      can express them fully as setArgumentData).
- integrate cgltf
- integrate jsmn json parser
- check if there is an elegant way to keep as much as possible from
  a bound pipeline when binding a compatible pipeline - basically
  re-use descriptorsets if possible (check this holds true: if
  descriptorsetlayout does not change, descriptors are compatible).
- what should we do with "orphaned" resources? that's resources which were
  not provided by previous passes, but are used by following passes...
  currently, these get re-allocated to default values - which means that
  images eventually get the backbuffer extent in memory, even if they
  are not used. this is nice, because it doesn't crash, but i can imagine
  that we might want to have a better way for dealing with this.

- implement a way to store paramters- parameters are values which we
  want to tweak during program runtime, and save before we quit the
  program, then reaload. parameters are closely related to
  serialisation as a more general problem.

## (Unsorted)

- usability: make rendertarget specification more discoverable - it
  should be trivial for example to change the clear color.

- add sort-key to encoder, so that we can decouple calling the `execute`
  callbacks from generating the command buffers.

- Better distinguish between renderpass types when creating vulkan command
  buffers

- Rename internal structure to `Batch` instead of `Renderpass` in backend
  because batch may fit better for *resource transfer*, *compute*, or
  *draw*.

### Find a better way to deal with external dependencies

With external dependencies we mean libraries which help us deal with glm,
json, memory allocation, gltf, midi, etc. 

- ideally, external dependencies are treated the same as internal modules;
  everything is a plug-in.

- it should be possible to express module dependencies (does this mean we
  need something like a package manager?)

----------------------------------------------------------------------

# STRATEGY

## Data Flow

* We strive for the simplest method of implementation - which is the
  pipeline.

* if two objects communicate by sharing an object, communication should be
  strictly pipeline-style, i.e. the consumer is not allowed to write back
  to the object borrowed from the provider. This makes it easier to reason
  about data-flow. We should also enforce that borrowing is an atomic
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

## Resource addressing

Let's refer to all resources in the renderer using opaque handles.
These handles should be based on hashing the name to 32 bits, which
allows us to store 32bits of metadata with each handle. Metadata
includes: resourcetype, resource id flags.

Hashing the name has the benefit that this can be done independently and
that no locking has to occur, and everyone who hashes a name should get
the same result. Also, hashing is executable as a constexpr, so the id
will not have to be calculated at runtime at all.

Resources are introduced by passes during their setup stage - if
a resource is permanent, the backend will remember the reource id, this
also means that if a permanent resource gets dropped during a resource
pass, the backend will mark that physical resource for recycling once no
frame which is in-flight still uses it.  

    > does this mean we could also introduce shader resources in setup
    > stage?

# Data + Algorithms

+ Data and algorithms ideally are orthogonal, which means that we can
  organise our code to follow a "pipeline" or "production line" approach,
  where data is "owned" by the algorithm that works on it, and only one
  algorithm at a time interacts with a strictly defined subset of the
  data. Access to data by algorithms is sequential, meaning only one
  algorithm at a time has access to the data. 
  
+ I believe rust for example enforces this through the borrow-checker.
  
+ For performance reasons, however, we don't necessarily want the
  granularity of rust, we only want to make sure that each execution
  context owns their memory resources (because that's the main reason for
  contention/race conditions)

  This means that algorithms can run, as long as their data is available,
  in parallel, without risk of race conditions or contention.

----------------------------------------------------------------------

# Caveat Emptor: Normal Accidents
+ Happen if systems are: 

    1. Highly Complex 
    2. Interactive (tightly coupled) 
    3. Incomprehensible (cause and effect are obscured)

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

* When we want to pass out an enum, but don't want to include a header
  file, we can hand out the enum wrapped in a struct (struct has only the
  enum as a field) for the enum. This means in a header file we can use
  the wrapper-type opaquely, which gives us type-safety for the enum.
  Implementations (.cpp files) which use the enum need a method to unwrap
  the original enum from the wrapper, which can be implemented trivially.

* instead of passing the api via parameter- you can retrieve it via the
  registry inside the function which uses it, and store it as
  a function-level static variable.

----------------------------------------------------------------------

# RESOURCE HANDLING

* when we encode commands which mention resources, resources are referenced by
  opaque handles retrieved via the engine. the engine then patches the command
  stream and substitutes any engine-specific resource ids by api-specific ids.
  this happens between command recording and command processing, in a method
  called `renderer_acquire_backend_resources`. 

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

# Applications

* A video post-processing pipeline

# Features

- resource lookup in `command_buffer_encoder_interface`

when we setup command buffers, we declare all resources - this means all
resource handles can be put into a vector, where each resource is only
referenced once. -> This becomes the vector of frame-available resources

> when we consolidate `resource_infos_t` for a frame, we should *OR* all
> their usage flags, so that the resource has the correct usage flags for
> the complete lifetime of the frame.

when we acquire resources, we create a matching vector which has the
vulkan object id for each resource - indices match frame available
resources vector.

# Next Step Features
- add materials for renderer
- implement a post processing effects pipeline
- implement pbrt materials based on gltf reference implementation
- implement pipeline generation as a channeled op - per encoder first,
  then consolidate those elements generated within a frame

## Plan on how to implement Multisampling

Multisampling means that draws don't go to image directly, but to
a *version* of the image which has a larger-than-one sample count. The
multisample version then gets resolved before the image is sampled.
Renderpasses have a mechanism which allows you to resolve multisample
attachments by specifying resolveAttachments. 

Note that the multisample versions of images must be allocated like
normal images, and that we must create imageviews for these
multisample images too. we must also track the sync state of the
single sample image and the multisample image. 

In case a renderpass has resolve attachments it needs to be created
with one resolve attachment per color attachment. It appears that we
also need one reolve attachment per depth attachment.

It would be nice to have a way to access the multisample image from
the corresponding single sample image and vice versa if such exists
for a renderpass. (We have tried to implement this by making the
sampleCount a property of the resourceID handle, but i'm not sure how
successful this approach is).

Multisampling is a property of the renderpass: 

+ if samplecount for a renderpass is greater than 1, all image
  attachments (apart from resolve attachments) must be images with the
  same chosen sample count. resolve attachments must have sample count
  of 1. 
+ Similarly, all pipelines used with such a multisampled renderpass
  must have chosen sample count for rasterizationSamples in its
  multisamplestate property.



# Long-Term aspirations
- reduce compile times with glm: template specialisations
- improve ergonomics, reduce lines to type (less "noisy" code)

# Todo

## What I'm happy with

- The "infer the obvious" principle - as a user of the api we want the
  renderer/engine to do the bookkeeping for us. 

- The way renderpasses are skipped/pruned from the graph if the renderer
  detects that they have no effect on swapchain. You can still force
  renderpasses to be executed by marking them as "root"

- Automatic mipmaps are auto-generated on image upload, if an image is
  declared with miplevels. If it is not declared with mip levels, no
  mipmap chain is generated.

- shader error reporting is pretty robust - you get file name, line
  number plus a context printout if a shader fails compilation.

- shader includes (and includes watching) is working great. If
  a shader or an included shader file is updated and saved, all
  dependent pieplines are recreated automatically.

- Automatic allocation of resources works - first all declarations of the
  resource are collected, then combined, then resources are allocated so
  that they fit all their uses. This means we can reason locally about
  what we need from a resource, and the engine will give us a resource
  that can do it.

- multisampling: it works almost magically: you specify the number of samples
  per renderpass, and that's it. This is a really good example for a simple api
  which protects you from great complexity.

- The way we can just add attachments to renderpasses, and enable/disable
  depth buffer by not declaring it in the setup stage of the renderpass.

## What I'm unhappy with

- the way a lower sytem calls into a higher level system when executing
  renderpass callbacks is too complicated - it also appears to be a case
  of the tail wagging the dog.


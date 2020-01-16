#include "le_gltf.h"
#include "pal_api_loader/ApiRegistry.hpp"

#include "3rdparty/cgltf/cgltf.h"

#include <vector>

// It could be nice if le_mesh_o could live outside of the stage - so that
// we could use it as a method to generate primitives for example, like spheres etc.

// the mesh would need a way to upload its geometry data.
// but in the most common cases that data will not be held inside the mesh.

// Stage is where we store our complete scene graph.
// The stage is the owner of the scene graph
//
struct le_stage_o {
	struct le_scene_o *scenes;
	size_t             scenes_sz;
	struct le_mesh_o * meshes;
	size_t             meshes_sz;

	// Buffers and images live inside the renderer (meaning, their memory is managed and owned by the
	// renderer) and may be accessed via their hash ids/handles.
	//
	// We must capture the hash id and keep it so that the bufferview will reference the
	// correct buffer.
	//
	// Perhaps the renderer should also allow us to store bufferviews, and imageviews, and to look
	// these up via hash id/handles - traditionally though, bufferviews and imageviews have been
	// objects with frame lifetime, and therefore we did allocate and deallocate them dynamically
	// with the frame.
	//
	// it's probably better we keep these internally.
	//
	// We must also provide a method to upload buffer and image data in a convenient way
	//
};

struct le_gltf_o {
	cgltf_options options = {};
	cgltf_data *  data    = nullptr;
	cgltf_result  result  = {};
};

// ----------------------------------------------------------------------

static void le_gltf_destroy( le_gltf_o *self ) {
	if ( self ) {
		if ( self->data )
			cgltf_free( self->data );
		delete self;
	}
}

// ----------------------------------------------------------------------

static le_gltf_o *le_gltf_create( char const *path ) {
	auto self    = new le_gltf_o();
	self->result = cgltf_parse_file( &self->options, path, &self->data );

	if ( self->result == cgltf_result_success ) {

		// This will load buffers from file, or data URIs,
		// and will allocate memory inside the cgltf module.
		//
		// Memory will be freed when calling `cgltf_free(self->data)`
		cgltf_result buffer_load_result = cgltf_load_buffers( &self->options, self->data, path );

		if ( buffer_load_result != cgltf_result_success ) {
			le_gltf_destroy( self );
			return nullptr;
		}

	} else {
		le_gltf_destroy( self );
		return nullptr;
	}

	return self;
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_gltf_api( void *api ) {
	auto &le_gltf_i = static_cast<le_gltf_api *>( api )->le_gltf_i;

	le_gltf_i.create  = le_gltf_create;
	le_gltf_i.destroy = le_gltf_destroy;
}

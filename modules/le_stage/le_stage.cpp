#include "le_stage.h"
#include "pal_api_loader/ApiRegistry.hpp"

#include "le_renderer/le_renderer.h"
#include "le_stage_types.h"

#include "3rdparty/src/spooky/SpookyV2.h"

#include <vector>
#include <string>
#include "string.h"

// It could be nice if le_mesh_o could live outside of the stage - so that
// we could use it as a method to generate primitives for example, like spheres etc.

// the mesh would need a way to upload its geometry data.
// but in the most common cases that data will not be held inside the mesh.

// Stage is where we store our complete scene graph.
// The stage is the owner of the scene graph
//

/* 
 * We need a method which allows us to upload resources. This method needs to be called
 * once before we do any rendering of scenes from a particular gltf instance.
 * 
*/

struct le_buffer_o {
	void *               mem;    // nullptr if not owning
	le_resource_handle_t handle; // renderer resource handle
	le_resource_info_t   resource_info;
	uint32_t             size;            // number of bytes
	bool                 was_transferred; // whether this buffer was transferred to gpu already
	bool                 owns_mem;        // true if sole owner of memory pointed to in mem
};

struct le_buffer_view_o {
	uint32_t            buffer_idx; // index of buffer in stage
	uint32_t            byte_offset;
	uint32_t            byte_length;
	uint32_t            byte_stride;
	le_buffer_view_type type; // vertex, or index type
};

struct le_accessor_o {
	le_num_type          component_type;
	le_compound_num_type type;
	uint32_t             byte_offset;
	uint32_t             count;
	uint32_t             buffer_view_idx; // index of buffer view in stage
	float                min[ 16 ];
	float                max[ 16 ];
	bool                 is_normalized;
	bool                 has_min;
	bool                 has_max;
	bool                 is_sparse;
};

struct le_attribute_o {
	le_primitive_attribute_info::Type type;
	std::string                       name;
	uint32_t                          index;
	uint32_t                          accessor_idx;
};

struct le_primitive_o {
	std::vector<le_attribute_o> attributes;
	bool                        has_indices;
	uint32_t                    indices_accessor_idx;
};

// has many primitives
struct le_mesh_o {
	std::vector<le_primitive_o> primitives;
};

// Owns all the data
struct le_stage_o {

	le_renderer_o *renderer; // non-owning.

	struct le_scene_o *scenes;
	size_t             scenes_sz;

	// Everything is kept as owned pointers -

	std::vector<le_mesh_o> meshes;

	std::vector<le_accessor_o>    accessors;
	std::vector<le_buffer_view_o> buffer_views;

	std::vector<le_buffer_o *>        buffers;
	std::vector<le_resource_handle_t> buffer_handles;
};

/// \brief Add a buffer to stage, return index to buffer within this stage.
///
static uint32_t le_stage_create_buffer( le_stage_o *stage, void *mem, uint32_t sz, char const *debug_name ) {

	assert( mem && "must point to memory" );
	assert( sz && "must have size > 0" );

	assert( stage->buffers.size() == stage->buffer_handles.size() );

	le_resource_handle_t res{};

#if LE_RESOURCE_LABEL_LENGTH > 0
	if ( debug_name ) {
		// Copy debug name if such was given, and handle has debug name field.
		strncpy( res.debug_name, debug_name, LE_RESOURCE_LABEL_LENGTH );
	}
#endif

	res.handle.as_handle.name_hash         = SpookyHash::Hash32( mem, sz, 0 );
	res.handle.as_handle.meta.as_meta.type = LeResourceType::eBuffer;

	uint32_t buffer_handle_idx = 0;
	for ( auto &h : stage->buffer_handles ) {
		if ( h == res ) {
			break;
		} else {
			buffer_handle_idx++;
		}
	}

	// ----------| Invariant: buffer_handle_idx == index for buffer handle inside stage

	if ( buffer_handle_idx == stage->buffer_handles.size() ) {

		// Buffer with this hash was not yet seen before
		// - we must allocate a new buffer.

		le_buffer_o *buffer = new le_buffer_o{};

		buffer->handle = res;
		buffer->mem    = malloc( sz );

		if ( buffer->mem ) {
			memcpy( buffer->mem, mem, sz );
			buffer->owns_mem = true;
			buffer->size     = sz;
		} else {
			// TODO: handle out-of-memory error.
			delete buffer;
			assert( false );
		}

		// TODO: check if we can narrow usage flags based on whether bufferview
		// which uses this buffer specifies index, or vertex for usage.

		buffer->resource_info = le::BufferInfoBuilder()
		                            .setSize( buffer->size )
		                            .addUsageFlags( {LE_BUFFER_USAGE_TRANSFER_DST_BIT |
		                                             LE_BUFFER_USAGE_INDEX_BUFFER_BIT |
		                                             LE_BUFFER_USAGE_VERTEX_BUFFER_BIT} )
		                            .build();

		stage->buffer_handles.push_back( res );
		stage->buffers.push_back( buffer );
	}

	return buffer_handle_idx;
}

/// \brief add buffer view to stage, return index of added buffer view inside of stage
static uint32_t le_stage_create_buffer_view( le_stage_o *self, le_buffer_view_info const *info ) {
	le_buffer_view_o view{};

	view.buffer_idx  = info->buffer_idx;
	view.byte_offset = info->byte_offset;
	view.byte_length = info->byte_length;
	view.byte_stride = info->byte_stride;
	view.type        = info->type;

	uint32_t idx = uint32_t( self->buffer_views.size() );
	self->buffer_views.emplace_back( view );
	return idx;
}

/// \brief add accessor to stage, return index of newly added accessor as it appears in stage.
static uint32_t le_stage_create_accessor( le_stage_o *self, le_accessor_info const *info ) {

	le_accessor_o accessor{};

	accessor.component_type  = info->component_type;
	accessor.type            = info->type;
	accessor.byte_offset     = info->byte_offset;
	accessor.count           = info->count;
	accessor.buffer_view_idx = info->buffer_view_idx;
	accessor.has_min         = info->has_min;
	accessor.has_max         = info->has_max;
	if ( info->has_min ) {
		memcpy( accessor.min, info->min, sizeof( float ) * 16 );
	}
	if ( info->has_max ) {
		memcpy( accessor.max, info->max, sizeof( float ) * 16 );
	}
	accessor.is_normalized = info->is_normalized;
	accessor.is_sparse     = info->is_sparse;

	uint32_t idx = uint32_t( self->accessors.size() );
	self->accessors.emplace_back( accessor );
	return idx;
}

/// \brief add mesh to stage, return index of newly added mesh as it appears in stage.
static uint32_t le_stage_create_mesh( le_stage_o *self, le_mesh_info const *info ) {

	le_mesh_o mesh;

	{
		le_primitive_info const *primitive_info_begin = info->primitives;
		auto                     primitive_infos_end  = primitive_info_begin + info->primitive_count;

		for ( auto p = primitive_info_begin; p != primitive_infos_end; p++ ) {

			le_primitive_o primitive{};

			le_primitive_attribute_info const *attr_info_begin = p->attributes;
			auto                               attr_info_end   = attr_info_begin + p->attribute_count;

			for ( auto attr = attr_info_begin; attr != attr_info_end; attr++ ) {
				le_attribute_o attribute{};
				//				attribute.name = attr->name; // TODO: copy name if available
				attribute.name         = "";
				attribute.index        = attr->index;
				attribute.accessor_idx = attr->accessor_idx;
				attribute.type         = attr->type;
				primitive.attributes.emplace_back( attribute );
			}

			if ( p->has_indices ) {
				primitive.has_indices          = true;
				primitive.indices_accessor_idx = p->indices_accessor_idx;
			}

			mesh.primitives.emplace_back( primitive );
		}
	}

	uint32_t idx = uint32_t( self->meshes.size() );
	self->meshes.emplace_back( mesh );
	return idx;
}

// ----------------------------------------------------------------------

/// \brief
static bool pass_setup_resources( le_renderpass_o *pRp, void *user_data ) {
	le::RenderPass rp{pRp};
	auto           stage = static_cast<le_stage_o *>( user_data );

	bool needsUpload = false;

	for ( auto &b : stage->buffers ) {
		needsUpload |= !b->was_transferred;
		if ( !b->was_transferred ) {
			rp.useBufferResource( b->handle, {LE_BUFFER_USAGE_TRANSFER_DST_BIT} );
		}
	}

	return needsUpload; // false means not to execute the execute callback.
}

// ----------------------------------------------------------------------

static void pass_transfer_resources( le_command_buffer_encoder_o *encoder_, void *user_data ) {
	auto stage   = static_cast<le_stage_o *>( user_data );
	auto encoder = le::Encoder{encoder_};

	for ( auto &b : stage->buffers ) {
		if ( !b->was_transferred ) {

			// upload buffer
			encoder.writeToBuffer( b->handle, 0, b->mem, b->size );

			// we could possibly free mem once that's done.
			free( b->mem );
			b->mem             = nullptr;
			b->owns_mem        = false;
			b->was_transferred = true;
		}
	}
}

// ----------------------------------------------------------------------

/// \brief add setup and execute callbacks to rendermodule so that rendermodule
/// knows which resources are needed to render the stage.
/// There are two resource types which potentially need uploading: buffers,
/// and images.
static void le_stage_update_render_module( le_stage_o *stage, le_render_module_o *module ) {

	using namespace le_renderer;

	auto rp = le::RenderPass( "STAGE_TRANSFER", LeRenderPassType::LE_RENDER_PASS_TYPE_TRANSFER )
	              .setSetupCallback( stage, pass_setup_resources )
	              .setExecuteCallback( stage, pass_transfer_resources )
	              .setIsRoot( true );

	// declare buffers

	for ( auto &b : stage->buffers ) {
		render_module_i.declare_resource( module, b->handle, b->resource_info );
	}

	render_module_i.add_renderpass( module, rp );
}

// ----------------------------------------------------------------------

static le_stage_o *le_stage_create( le_renderer_o *renderer ) {
	auto self      = new le_stage_o{};
	self->renderer = renderer;
	return self;
}

// ----------------------------------------------------------------------

static void le_stage_destroy( le_stage_o *self ) {

	for ( auto &b : self->buffers ) {
		if ( b->owns_mem && b->mem && b->size ) {
			free( b->mem );
		}
		delete b;
	}
	self->buffers.clear();
	self->buffer_handles.clear();

	delete self;
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_stage_api( void *api ) {
	auto &le_stage_i = static_cast<le_stage_api *>( api )->le_stage_i;

	le_stage_i.create  = le_stage_create;
	le_stage_i.destroy = le_stage_destroy;

	le_stage_i.update_rendermodule = le_stage_update_render_module;

	le_stage_i.create_buffer      = le_stage_create_buffer;
	le_stage_i.create_buffer_view = le_stage_create_buffer_view;
	le_stage_i.create_accessor    = le_stage_create_accessor;
	le_stage_i.create_mesh        = le_stage_create_mesh;
}

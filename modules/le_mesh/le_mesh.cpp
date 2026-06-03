#define LE_MODULE_UNREGISTER_EXPLICIT
#include "le_mesh.h"
#include "le_core.h"
#include "le_log.h"
#include "le_pipeline_builder.h"
#include "le_renderer.h"
#include "le_renderer.hpp"

#include <cstdlib>
#include <vector>
#include <cstring> // for memcopy
#include <map>
#include <set>
#include <cassert>
#include <unordered_map>
#include <string>

#include "shaders/default_vert.h"
#include "shaders/default_frag.h"

static auto logger = le::Log( "le_mesh" );

// ffdecl.
static void le_mesh_use_with_renderpass( le_mesh_o* self, le_renderpass_o* rp_, le_mesh_attribute_info_t const* attribute_infos, size_t attribute_infos_count );

struct le_mesh_debug_draw_data_t {
	le_mesh_o* mesh;        // the mesh object itself
	float      mvp[ 16 ];   // model view projection per mesh
	float      colour[ 4 ]; // vertex colour for this mesh
};

struct draw_data_t {
	std::vector<le_mesh_debug_draw_data_t> draw_data;
};

struct le_mesh_singleton_o {
	draw_data_t* current_draw_batch = nullptr;
};

struct buffer_data_descriptor {
	uint32_t data_idx          = 0;
	uint32_t interleave_offset = 0;
	uint32_t bytes_per_vertex  = 0;
};

struct buffer_data_t {
	std::vector<le_mesh_attribute_info_t> attribute_infos;
	std::vector<uint8_t>                  cpu_data;

	le_buffer_resource_handle buffer_resource      = nullptr;
	le_resource_info_t        buffer_resource_info = {};
	//
	uint32_t num_bytes_per_stride = 0;    // bytes per index or bytes per vertex on this buffer
	bool     is_tainted           = true; // whether the resource needs to be uploaded or not
};

struct le_mesh_o {
	const std::string debug_name;
	size_t            num_vertices = 0; // number of vertices - all attribute_data must have this count

	std::vector<buffer_data_t>                                      data;
	std::map<le_mesh_attribute_name, buffer_data_descriptor>        data_descriptors;

	le_mesh_attribute_name existing_attribute_names;

	buffer_data_t* indices_data = nullptr;
};

// ----------------------------------------------------------------------

static le_mesh_o* le_mesh_create( char const* debug_name = nullptr ) {
	auto self = new le_mesh_o{ .debug_name = std::string( debug_name != nullptr ? debug_name : "" ) };
	return self;
}

// ----------------------------------------------------------------------

static void le_mesh_destroy( le_mesh_o* self ) {
	delete self->indices_data;
	delete self;
}

// ----------------------------------------------------------------------

static void le_mesh_clear( le_mesh_o* self ) {
	self->existing_attribute_names = {};
	self->num_vertices = 0;
	self->data_descriptors.clear();
	self->data.clear();
	delete ( self->indices_data );
}

// ----------------------------------------------------------------------

static void le_mesh_read_vertex_data_into_buffer( le_mesh_o const* self, void* target, size_t target_capacity_num_bytes,
                                                  le_mesh_attribute_info_t const* dst_attribute_info,
                                                  size_t dst_attribute_info_count, size_t first_vertex ) {

	struct it_t {
		uint8_t const* src;        // source data pointer (these may be into different source data buffers)
		uint8_t*       dst;        // dst data pointer - stride it the same for all dst data pointers.
		uint8_t        src_stride; // increment to source data pointer on every iteration
		uint16_t       n_bytes;    // number of bytes that need to be copied for every iteration
	};

	std::vector<it_t> iterators;
	size_t            dst_stride = 0;

	{
		std::vector<le_mesh_attribute_info_t> dst_info;

		if ( dst_attribute_info == nullptr ) {

			// If no dst_attribute_info was specified, interpret this as all attributes being selected
			if ( dst_attribute_info_count == 0 ) {
				dst_attribute_info_count = self->data_descriptors.size();
			}
			size_t num_descriptors = 0;
			for ( auto const& [ key, data ] : self->data_descriptors ) {
				if ( num_descriptors >= dst_attribute_info_count ) {
					break;
				}

				dst_info.emplace_back( key, data.bytes_per_vertex );

				num_descriptors++;
			}
		} else {
			dst_info.insert( dst_info.begin(), dst_attribute_info, dst_attribute_info + dst_attribute_info_count );
		}

		uint8_t* target_head = reinterpret_cast<uint8_t*>( target );

		iterators.reserve( dst_info.size() );

		size_t dst_offset_sum = 0;

		for ( auto& d : dst_info ) {

			if ( ( uint8_t( d.name ) & uint8_t( self->existing_attribute_names ) ) == 0 ) {

				// if name not in source names or if it does not exist in our data we must ignore it
				d.name = le_mesh_attribute_name::eUndefined;

			} else {

				auto const& desc = self->data_descriptors.at( d.name );

				it_t iterator{
				    .src        = self->data[ desc.data_idx ].cpu_data.data() + desc.interleave_offset,
				    .dst        = target_head + dst_offset_sum,
				    .src_stride = uint8_t( self->data[ desc.data_idx ].cpu_data.size() / self->num_vertices ),
				    .n_bytes    = std::min<uint16_t>( desc.bytes_per_vertex, d.bytes_per_vertex ), // Note: If dst < src this means that there may be garbage data in dst if dst if not zeroed out before copy
				};

				iterators.emplace_back( std::move( iterator ) );
			}

			dst_offset_sum += d.bytes_per_vertex;
		}

		dst_stride = dst_offset_sum;
	}

	// We want to stop when either one of our source iterators runs out - that's bounded by number of vertices
	// or the target capacity runs out.

	size_t target_capacity_in_vertices = target_capacity_num_bytes / dst_stride;
	size_t num_max_iterations          = std::min<size_t>( target_capacity_in_vertices, self->num_vertices );
	size_t num_vertices_to_copy        = first_vertex >= num_max_iterations ? 0 : num_max_iterations - first_vertex;

	if ( num_vertices_to_copy == 0 ) {
		// nothing to do.
		return;
	}

	// OPTIMIZATION: If we have a single iterator, and this iterator has .src_stride == .n_bytes
	// then we can do a block memcpy, as vertices are tightly packed in both source and dst.
	//
	if ( iterators.size() == 1 ) {
		auto& it = iterators.front();
		if ( it.n_bytes == dst_stride && dst_stride == it.src_stride ) {
			memcpy( it.dst, it.src + dst_stride * first_vertex, dst_stride * num_vertices_to_copy );
			return;
		}
	}

	{
		// OPTIMIZATION:
		//
		// If all iterators use the same input buffer, and the
		// input buffer is tightly packed, and in the same order
		// as the output, then we can copy everything in bulk.

		// Conditions:
		// - src_stride needs to match dst_stride, which is unique, and pre-calculated above.
		//   - implicitly covered by this: src_stride needs to be identical over all iterators
		// - for each attribute_info:
		// 		- .src and .dst need to be the same, relative to their start value
		// 		- .src and .dst need to start at 0, relative to start value
		// - both last .src and last .dst + n_bytes needs to match dst_stride

		uint8_t const* prev_p       = self->data.front().cpu_data.data();
		ptrdiff_t      next_diff    = 0;
		size_t         total_stride = dst_stride;

		for ( auto const& it : iterators ) {

			ptrdiff_t diff = it.src - prev_p;

			if ( next_diff != diff || it.src_stride != dst_stride ) {
				// inconsistency detected
				break;
			}

			prev_p    = it.src;
			next_diff = it.n_bytes;
			total_stride -= it.n_bytes;
		}

		// if total_stride = 0 this means that the loop has completed successfully, which means that iterators are consistent
		if ( total_stride == 0 ) {
			// we can copy in bulk.
			memcpy( iterators.front().dst, iterators.front().src, dst_stride * num_vertices_to_copy );
			return;
		}
	}

	// ---------| Invariant: Vertices are not tightly packed in src and dst.

	// Process one iterator at a time, because we hope
	// that this will lead to better cache locality as
	// it means less hopping between buffers.
	for ( it_t& it : iterators ) {
		it.src += it.src_stride * first_vertex;
		for ( size_t i = first_vertex; i != num_max_iterations; i++ ) {
			memcpy( it.dst, it.src, it.n_bytes );
			it.src += it.src_stride;
			it.dst += dst_stride;
		}
	}
}

// ----------------------------------------------------------------------

static void le_mesh_read_attribute_data_into(
    le_mesh_o const* self,
    void* target, size_t target_capacity_num_bytes,
    le_mesh_attribute_name attribute_name,
    uint32_t*              out_num_bytes_per_vertex,
    size_t*                num_vertices,
    size_t                 first_vertex, // first vertex
    uint32_t               stride,
    uint32_t               initial_stride_offset ) {

	auto d_it = self->data_descriptors.find( attribute_name );

	if ( d_it == self->data_descriptors.end() ) {
		logger.warn( "Could not find descriptor with attribute name: %d", attribute_name );
		return;
	}

	// ----------| invariant d_it contains a valid iterator

	if ( out_num_bytes_per_vertex ) {
		*out_num_bytes_per_vertex = d_it->second.bytes_per_vertex;
	}

	if ( num_vertices ) {
		*num_vertices = self->num_vertices;
	}

	if ( target == nullptr ) {
		return;
	}

	if ( stride == 0 ) {
		stride = d_it->second.bytes_per_vertex + initial_stride_offset;
	}

	le_mesh_attribute_info_t attribute_infos[] = {
	    {
	        .name             = le_mesh_attribute_name::ePadding,
	        .bytes_per_vertex = initial_stride_offset,
	    },
	    {
	        .name             = attribute_name,
	        .bytes_per_vertex = d_it->second.bytes_per_vertex,
	    },
	    {
	        .name             = le_mesh_attribute_name::ePadding,
	        .bytes_per_vertex = stride - initial_stride_offset - d_it->second.bytes_per_vertex,
	    },
	};

	le_mesh_read_vertex_data_into_buffer( self, target, target_capacity_num_bytes, attribute_infos, 3, first_vertex );

	if ( num_vertices ) {
		*num_vertices = std::min<size_t>( target_capacity_num_bytes / stride, self->num_vertices );
	}
}
// ----------------------------------------------------------------------

static void le_mesh_read_index_data_into( le_mesh_o const* self, void* target, size_t target_capacity_num_bytes, uint32_t* num_bytes_per_index, size_t* num_indices, size_t first_index ) {

	if ( self->indices_data == nullptr ) {
		return;
	}

	size_t existing_bytes_per_index = self->indices_data->num_bytes_per_stride;

	size_t num_indices_available = self->indices_data->cpu_data.size() / existing_bytes_per_index;
	size_t num_indices_requested = ( num_indices ) ? ( *num_indices ) : num_indices_available;

	if ( first_index >= num_indices_available ) {
		return;
	}

	size_t num_indices_to_copy = std::min( num_indices_requested, num_indices_available - first_index );

	// Now, limit number of vertices to however many as we can store back into the target
	num_indices_to_copy = std::min( target_capacity_num_bytes / existing_bytes_per_index, num_indices_to_copy );

	if ( num_indices ) {
		*num_indices = num_indices_to_copy;
	}
	if ( num_bytes_per_index ) {
		*num_bytes_per_index = existing_bytes_per_index;
	}
	if ( target ) {
		size_t offset_in_bytes   = first_index * existing_bytes_per_index;
		size_t num_bytes_to_copy = num_indices_to_copy * existing_bytes_per_index;
		memcpy( target, self->indices_data->cpu_data.data() + offset_in_bytes, num_bytes_to_copy );
	}
}

// ----------------------------------------------------------------------

static le_buffer_resource_handle le_mesh_create_index_buffer( le_mesh_o* self, le_renderer_o* renderer ) {
	if ( nullptr == renderer ) {
		return nullptr;
	}
	char label[ 64 ] = {};
	snprintf( label, sizeof( label ), "%s-indices", self->debug_name.c_str() );
	return le_renderer_api_i->le_renderer_i.create_buf_resource_handle( renderer, self->debug_name.empty() ? "" : label, 0, 0 );
}

// ----------------------------------------------------------------------

static le_buffer_resource_handle le_mesh_create_vertex_buffer_resource( le_mesh_o* mesh, le_renderer_o* renderer, uint32_t buf_idx ) {
	if ( nullptr == renderer ) {
		return nullptr;
	}
	char label[ 64 ] = {};
	snprintf( label, sizeof( label ), "%s-vtx_%d", mesh->debug_name.c_str(), buf_idx );
	return le_renderer_api_i->le_renderer_i.create_buf_resource_handle( renderer, label, 0, 0 );
}

// ----------------------------------------------------------------------

static void* le_mesh_allocate_index_data( le_mesh_o* self, size_t num_indices, uint32_t* num_bytes_per_index, le_renderer_o* optional_renderer ) {

	if ( nullptr == num_bytes_per_index ) {
		logger.error( "You must specify the number of bytes per index pointer." );
		return nullptr;
	}

	size_t num_bytes_required = 0;
	{
		// If number of vertices is greater than what can be represented with an 16 bit index, we must use
		// 32 bit indices.

		uint32_t required_num_bytes_per_index = ( self->num_vertices <= ( 1 << 16 ) ) ? 2 : 4;

		// Go for the lowest number of bytes per index that you can get away with,
		// but respect the client's request if they want a higher number of indices.
		*num_bytes_per_index = std::min( std::max( required_num_bytes_per_index, *num_bytes_per_index ), uint32_t( 4 ) );
		num_bytes_required   = *num_bytes_per_index * num_indices;
	}

	// If this mesh has not had indices before, then we must create index data.

	if ( nullptr == self->indices_data ) {
		self->indices_data                  = new buffer_data_t{};
		self->indices_data->buffer_resource = le_mesh_create_index_buffer( self, optional_renderer );
		self->indices_data->buffer_resource_info =
		    le::BufferInfoBuilder()
		        .addUsageFlags( le::BufferUsageFlagBits::eTransferDst | le::BufferUsageFlagBits::eIndexBuffer )
		        .build();
	}

	self->indices_data->buffer_resource_info.buffer.size = num_bytes_required;
	self->indices_data->num_bytes_per_stride             = *num_bytes_per_index;

	// This potentially re-allocates indices, and it invalidates any previous pointers to index data
	self->indices_data->cpu_data.resize( num_bytes_required );

	return self->indices_data->cpu_data.data();
}

// ----------------------------------------------------------------------
// allocate data for a buffer of interleaved vertex data
// attribute_infos must hold information for the current buffer
// and any data that gets interleaved with this buffer.
static void* le_mesh_allocate_vertex_data( le_mesh_o* self, le_mesh_attribute_info_t const* attribute_infos, size_t num_attribute_infos, le_renderer_o* optional_renderer ) {

	if ( attribute_infos == nullptr || num_attribute_infos == 0 || self->num_vertices == 0 ) {
		logger.warn( "Cannot allocate vertex data. `num_vertices`: %d", self->num_vertices );
		return nullptr;
	}

	// ---------| invariant: attribute_infos is valid, num_attribute_infos > 0

	// first we need to make sure that none of the given attributes is already present

	le_mesh_attribute_name new_attribute_names = {};
	for ( auto p_attr = attribute_infos; p_attr != attribute_infos + num_attribute_infos; p_attr++ ) {
		new_attribute_names = le_mesh_attribute_name( uint8_t( new_attribute_names ) | uint8_t( p_attr->name ) );
	}

	if ( uint8_t( self->existing_attribute_names ) & uint8_t( new_attribute_names ) ) {
		logger.warn( "attribute name does already exist: %d", uint8_t( self->existing_attribute_names ) & uint8_t( new_attribute_names ) );
		return nullptr;
	}

	// --------| invariant none of these attribute names already exits, we can add a new buffer

	uint32_t num_bytes_per_vertex = 0;

	uint32_t buffer_id = self->data.size();

	for ( auto p_attr = attribute_infos; p_attr != attribute_infos + num_attribute_infos; p_attr++ ) {
		buffer_data_descriptor d{
		    .data_idx          = buffer_id,
		    .interleave_offset = num_bytes_per_vertex,
		    .bytes_per_vertex  = p_attr->bytes_per_vertex,
		};
		self->data_descriptors.emplace( p_attr->name, d );
		num_bytes_per_vertex += p_attr->bytes_per_vertex;
	}

	buffer_data_t data_entry{
	    .attribute_infos      = { attribute_infos, attribute_infos + num_attribute_infos },
	    .cpu_data             = std::vector<uint8_t>( num_bytes_per_vertex * self->num_vertices ),
	    .buffer_resource      = le_mesh_create_vertex_buffer_resource( self, optional_renderer, self->data.size() ),
	    .buffer_resource_info = le::BufferInfoBuilder()
	                                .addUsageFlags( le::BufferUsageFlagBits::eTransferDst | le::BufferUsageFlagBits::eVertexBuffer )
	                                .setSize( num_bytes_per_vertex * self->num_vertices )
	                                .build(),
	    .num_bytes_per_stride = num_bytes_per_vertex,
	    .is_tainted           = true,
	};

	self->data.emplace_back( std::move( data_entry ) );

	self->existing_attribute_names = le_mesh_attribute_name( uint8_t( self->existing_attribute_names ) | uint8_t( new_attribute_names ) );

	return self->data.back().cpu_data.data();
}

// ----------------------------------------------------------------------

static void* le_mesh_allocate_attribute_data( le_mesh_o* self, le_mesh_attribute_name attribute_name, uint32_t num_bytes_per_vertex ) {
	le_mesh_attribute_info_t info{
	    .name             = attribute_name,
	    .bytes_per_vertex = num_bytes_per_vertex };
	return le_mesh_allocate_vertex_data( self, &info, 1, nullptr );
};

// ----------------------------------------------------------------------

static void le_mesh_set_vertex_count( le_mesh_o* self, size_t num_vertices, bool* did_reallocate ) {

	bool did_realloc   = false;
	self->num_vertices = num_vertices;

	for ( auto& buffers : self->data ) {

		// Find size, make sure that it matches with num-vertices.
		//
		// If it doesn't re-allocate this attribute so that it fits
		// fill with zeroes, if necessary.

		if ( ( buffers.cpu_data.size() / buffers.num_bytes_per_stride ) < num_vertices ) {
			buffers.is_tainted = true;
			buffers.cpu_data.resize( num_vertices * buffers.num_bytes_per_stride, {} );
			did_realloc = true;
		}
	}

	if ( did_reallocate ) {
		*did_reallocate = did_realloc;
	}
};

// ----------------------------------------------------------------------

static size_t le_mesh_get_vertex_count( le_mesh_o* self ) {
	return self->num_vertices;
}

// ----------------------------------------------------------------------

static size_t le_mesh_get_index_count( le_mesh_o* self, uint32_t* num_bytes_per_index ) {

	if ( nullptr == self->indices_data ) {
		return 0;
	}

	if ( num_bytes_per_index ) {
		*num_bytes_per_index = self->indices_data->num_bytes_per_stride;
	}

	return self->indices_data->cpu_data.size() / self->indices_data->num_bytes_per_stride;
};

// ----------------------------------------------------------------------

static bool le_mesh_get_attribute_infos_for_binding( le_mesh_o* self, size_t const binding_number,
                                                     le_mesh_attribute_info_t* out_attr_info, size_t* out_attr_info_count ) {

	if ( nullptr == out_attr_info_count ) {
		return false;
	}

	// ----------| Invariant: out_att_info_count is valid pointer

	if ( binding_number >= self->data.size() ) {
		*out_attr_info_count = 0;
		return false;
	}

	// ----------| Invariant: binding number is valid

	auto const& attr_infos      = self->data[ binding_number ].attribute_infos;
	size_t      attr_info_count = attr_infos.size();

	if ( nullptr == out_attr_info || *out_attr_info_count < attr_info_count ) {
		*out_attr_info_count = attr_info_count;
		return false;
	}

	memcpy( out_attr_info, attr_infos.data(), sizeof( le_mesh_attribute_info_t ) * attr_info_count );

	*out_attr_info_count = attr_info_count;

	return true;
}

// ----------------------------------------------------------------------

/// Return the number of backing buffers used for vertex attribute data storage
static size_t le_mesh_get_vertex_buffers_count( le_mesh_o* self ) {
	return self->data_descriptors.size();
};

// ----------------------------------------------------------------------

static void* le_mesh_get_attribute_data( le_mesh_o* self, le_mesh_attribute_name attribute_name, size_t* out_stride ) {

	auto it = self->data_descriptors.find( attribute_name );

	if ( it == self->data_descriptors.end() ) {
		return nullptr;
	}

	// -----------| invariant attribute exists

	size_t required_stride = it->second.bytes_per_vertex;

	if ( nullptr == out_stride ) {
		return nullptr;
	} else {
		*out_stride = required_stride;
	}

	self->data[ it->second.data_idx ].is_tainted = true;

	return self->data[ it->second.data_idx ].cpu_data.data() + it->second.interleave_offset;
}

// ----------------------------------------------------------------------

static void* le_mesh_get_index_data( le_mesh_o* self, size_t* out_stride, size_t* num_indices ) {

	if ( nullptr == self->indices_data ) {
		return nullptr;
	}

	// ---------| invariant: there is index data
	size_t stride = self->indices_data->num_bytes_per_stride;

	if ( out_stride ) {
		*out_stride = stride;
	}

	if ( num_indices ) {
		*num_indices = self->indices_data->cpu_data.size() / stride;
	}

	self->indices_data->is_tainted = true;

	return self->indices_data->cpu_data.data();
}

// ----------------------------------------------------------------------

static le_buffer_resource_handle le_mesh_get_attribute_buffer( le_mesh_o* self, le_mesh_attribute_name attribute_name, le_resource_info_t* optional_resource_info ) {

	auto it = self->data_descriptors.find( attribute_name );

	if ( it == self->data_descriptors.end() ) {
		return nullptr;
	}

	// -----------| invariant attribute exists

	auto& buf_data = self->data[ it->second.data_idx ];

	if ( optional_resource_info ) {
		*optional_resource_info = buf_data.buffer_resource_info;
	}

	return buf_data.buffer_resource;
}

// ----------------------------------------------------------------------

static le_buffer_resource_handle le_mesh_get_index_buffer( le_mesh_o* self, le_resource_info_t* optional_resource_info ) {

	if ( nullptr == self->indices_data ) {
		return nullptr;
	}

	// ---------| invariant: there is index buffer
	if ( optional_resource_info ) {
		*optional_resource_info = self->indices_data->buffer_resource_info;
	}

	return self->indices_data->buffer_resource;
}

// ----------------------------------------------------------------------

static void le_mesh_submit_meshes_to_rendergraph( le_mesh_o** meshes, size_t meshes_count, le_rendergraph_o* rg, le_renderer_o* renderer ) {

	struct upload_data_item_t {
		le_buffer_resource_handle buffer;
		std::vector<uint8_t>*     p_attribute_data;
	};

	std::vector<upload_data_item_t> upload_items; // upload data items

	// First, decant meshes into a set so that we can be sure
	// that we only submit each mesh once.
	std::set<le_mesh_o*> unique_meshes{ meshes, meshes + meshes_count };

	for ( le_mesh_o* mesh : unique_meshes ) {

		uint32_t buf_count = 0;
		for ( auto& buf_data : mesh->data ) {

			// declare resource to rendergraph

			if ( nullptr == buf_data.buffer_resource ) {
				// we need to allocate the buffer resource first
				buf_data.buffer_resource = le_mesh_create_vertex_buffer_resource( mesh, renderer, buf_count );
			}

			if ( buf_data.is_tainted ) {
				upload_items.emplace_back( buf_data.buffer_resource, &buf_data.cpu_data );
				size_t num_bytes                          = mesh->num_vertices * buf_data.num_bytes_per_stride;
				buf_data.buffer_resource_info.buffer.size = num_bytes;
				buf_data.buffer_resource_info =
				    le::BufferInfoBuilder()
				        .addUsageFlags( le::BufferUsageFlagBits::eTransferDst | le::BufferUsageFlagBits::eVertexBuffer )
				        .setSize( num_bytes )
				        .build();
				buf_data.is_tainted = false;
			}

			le_renderer_api_i->le_rendergraph_i.declare_resource( rg, buf_data.buffer_resource, buf_data.buffer_resource_info );
			buf_count++;
		}

		if ( mesh->indices_data ) {

			if ( nullptr == mesh->indices_data->buffer_resource ) {
				mesh->indices_data->buffer_resource = le_mesh_create_index_buffer( mesh, renderer );
			}

			if ( mesh->indices_data->is_tainted ) {
				size_t num_bytes = mesh->indices_data->cpu_data.size();
				mesh->indices_data->buffer_resource_info =
				    le::BufferInfoBuilder()
				        .addUsageFlags( le::BufferUsageFlagBits::eTransferDst | le::BufferUsageFlagBits::eIndexBuffer )
				        .setSize( num_bytes )
				        .build();
				upload_items.emplace_back( mesh->indices_data->buffer_resource, &mesh->indices_data->cpu_data );
				mesh->indices_data->is_tainted = false;
			}

			// set index buffer info so that is has index read
			le_renderer_api_i->le_rendergraph_i.declare_resource( rg, mesh->indices_data->buffer_resource, mesh->indices_data->buffer_resource_info );
		}
	}

	// If there are any upload items - these need to be transferred
	//
	if ( !upload_items.empty() ) {

		auto rp = le::RenderPass( "le_mesh:xfer_meshes", le::QueueFlagBits::eTransfer );

		// Declare that all resources will get transferred in this pass.
		for ( auto& u : upload_items ) {
			rp.useBufferResource( u.buffer, le::AccessFlagBits2::eTransferWrite );
		}
		// ---------- set up transfer pass for data items.

		struct mesh_closure_t {
			size_t             num_upload_items;
			upload_data_item_t upload_data_items[];
		};

		// manually allocate closure data
		size_t mesh_closure_data_num_bytes = sizeof( size_t ) + sizeof( upload_data_item_t ) * upload_items.size();
		auto   mesh_closure_data           = ( mesh_closure_t* )malloc( mesh_closure_data_num_bytes );

		// fill in closure data
		mesh_closure_data->num_upload_items = upload_items.size();
		memcpy( mesh_closure_data->upload_data_items, upload_items.data(), sizeof( upload_data_item_t ) * upload_items.size() );

		rp.setExecuteCallbackWithLocalUserData( mesh_closure_data, mesh_closure_data_num_bytes, []( le_command_buffer_encoder_o* encoder_, void* user_data ) {
			le::TransferEncoder encoder( encoder_ );
			// extract closure data from callback local data
			auto mesh_data = ( mesh_closure_t* )( user_data );

			for ( upload_data_item_t* it = mesh_data->upload_data_items; it != mesh_data->upload_data_items + mesh_data->num_upload_items; it++ ) {

				// now we allocate memory and upload the data items here
				void* gpu_memory = nullptr;
				// this copies the mesh data from cpu memory into the gpu buffer
				if ( encoder.mapBufferMemory( it->buffer, 0, it->p_attribute_data->size(), &gpu_memory ) ) {
					// Then write into mapped memory which is directly managed by the GPU
					memcpy( gpu_memory, it->p_attribute_data->data(), it->p_attribute_data->size() );
				}
			}
		} );

		// add the renderpass to the current rendergraph
		le::RenderGraph( rg ).addRenderPass( rp );
		free( mesh_closure_data );
	}
};

// ----------------------------------------------------------------------

static bool le_mesh_get_vertex_input_descriptions(
    le_mesh_o*                             self,
    le_mesh_attribute_info_t const*        attribute_infos,
    size_t                                 attribute_infos_count,
    le_vertex_input_attribute_description* out_attribute_descriptions,
    size_t*                                out_attribute_descriptions_count,
    le_vertex_input_binding_description*   out_binding_descriptions,
    size_t*                                out_binding_descriptions_count ) {

	// if ( self->num_vertices == 0 ) {
	// 	return false;
	// }

	std::vector<le_mesh_attribute_info_t> attribute_info_vec{ attribute_infos, attribute_infos + attribute_infos_count };

	if ( attribute_infos == nullptr || attribute_infos_count == 0 ) {
		// of no attributes were given, when we use all available attributes
		for ( auto& d : self->data ) {
			for ( auto& a : d.attribute_infos ) {
				attribute_info_vec.push_back( a );
			}
		}
	}

	if ( nullptr == out_attribute_descriptions ) {
		return false;
	}

	if ( nullptr == out_binding_descriptions ) {
		return false;
	}

	std::map<uint8_t, le_vertex_input_binding_description> binding_descriptors;

	{
		auto out_description      = out_attribute_descriptions;
		auto out_descriptions_end = out_attribute_descriptions + *out_attribute_descriptions_count;

		// We can't really calculate the location because this is ultimately specified by the shader.
		//
		// We can, however, assume that the order in which our attribute infos are given has meaning
		// and that it represents the location in which we would find the attributes in the shader.
		//
		uint8_t location                              = 0;
		size_t  used_out_attribute_descriptions_count = 0;

		for ( auto const& a : attribute_info_vec ) {

			if ( out_description == out_descriptions_end ) {
				logger.error( "not enough out attribute descriptors provided." );
				return false;
			}

			auto it = self->data_descriptors.find( a.name );
			if ( it != self->data_descriptors.end() ) {

				auto const& [ key, buffer_data ] = *it;

				if ( buffer_data.bytes_per_vertex != a.bytes_per_vertex ) {
					logger.error( "attribute has incorrect number of bytes per vertex" );
					return false;
				}

				out_description->location       = location;                                                  /// 0..31 shader attribute location (this is set in shader code - and needs to be matched manually or via reflection)
				out_description->binding        = uint8_t( buffer_data.data_idx );                           /// 0..31 binding slot (this is the binding slot we want to bind to - usually the index of the data buffer)
				out_description->binding_offset = uint16_t( buffer_data.interleave_offset );                 /// 0..65565 offset for this location within binding (careful: must not be larger than maxVertexInputAttributeOffset [0.0x7ff])
				out_description->type           = le_num_type::eF32;                                         /// base type for attribute
				out_description->vecsize        = uint8_t( buffer_data.bytes_per_vertex / sizeof( float ) ); /// 0..7 number of elements of base type
				out_description->isNormalised   = 0;                                                         /// whether this input comes pre-normalized

				auto& b   = binding_descriptors[ out_description->binding ];
				b.binding = out_description->binding;
				b.stride     = self->data[ out_description->binding ].num_bytes_per_stride;
				b.input_rate = le_vertex_input_rate::ePerVertex;

				used_out_attribute_descriptions_count++;
				out_description++;
			} else {
				// Is it possible to leave a binding unoccupied?
				logger.error( "Could not find attribute info. Attribute %d does not exist in this mesh.", a.name );
				return false;
			}
			location++;
		}

		*out_attribute_descriptions_count = used_out_attribute_descriptions_count;
	}

	{
		size_t used_out_binding_descriptions_count = 0;
		// Return binding descriptions
		auto binding      = out_binding_descriptions;
		auto bindings_end = binding + *out_binding_descriptions_count;
		for ( auto const& [ key, b ] : binding_descriptors ) {
			if ( binding == bindings_end ) {
				logger.error( "Not enough out binding descriptors provided" );
				return false;
			}
			*binding++ = b;
			used_out_binding_descriptions_count++;
		}
		*out_binding_descriptions_count = used_out_binding_descriptions_count;
	}

	return true;
}

// ----------------------------------------------------------------------

static bool le_mesh_apply_vertex_input_descriptions( le_mesh_o* self, le_graphics_pipeline_builder_o* pipeline_builder, le_mesh_attribute_info_t const* optional_attribute_infos, size_t optional_attribute_infos_count ) {

	if ( pipeline_builder == nullptr ) {
		return false;
	}

	size_t count = optional_attribute_infos_count;

	if ( count == 0 ) {
		for ( auto& d : self->data ) {
			count += d.attribute_infos.size();
		}
	}

	std::vector<le_vertex_input_attribute_description> viad( count );
	size_t                                             n_viad = count;
	std::vector<le_vertex_input_binding_description>   vibd( count );
	size_t                                             n_vibd = count;

	bool result = le_mesh_get_vertex_input_descriptions(
	    self,
	    optional_attribute_infos, optional_attribute_infos_count,
	    viad.data(), &n_viad,
	    vibd.data(), &n_vibd );

	le_pipeline_builder_api_i->le_graphics_pipeline_builder_i
	    .set_vertex_input_attribute_descriptions( pipeline_builder, viad.data(), n_viad );

	le_pipeline_builder_api_i->le_graphics_pipeline_builder_i
	    .set_vertex_input_binding_descriptions( pipeline_builder, vibd.data(), n_vibd );

	return result;
}

// ----------------------------------------------------------------------

static uint32_t le_mesh_bind_to_encoder( le_mesh_o* self, le_command_buffer_encoder_o* encoder_, le_mesh_attribute_info_t const* optional_attribute_infos, size_t optional_attribute_infos_count, bool should_ignore_indices = false ) {

	// consolidate all bindings for the attributes in question
	le::GraphicsEncoder encoder{ encoder_ };

	std::set<uint32_t> used_buffers; // unique, automatically sorted

	if ( optional_attribute_infos == nullptr ) {
		// If no attribute infos are specified, we read this as the requirement to bind all buffers to this encoder.
		for ( int i = 0; i != self->data.size(); i++ ) {
			used_buffers.insert( i );
		}
	} else {
		for ( auto a = optional_attribute_infos; a != optional_attribute_infos + optional_attribute_infos_count; a++ ) {
			auto it = self->data_descriptors.find( a->name );
			if ( it != self->data_descriptors.end() ) {
				used_buffers.emplace( it->second.data_idx );
			}
		}
	}

	if ( used_buffers.empty() && self->indices_data == nullptr ) {
		return 0;
	}

	std::vector<le_buffer_resource_handle> buffers;

	size_t first_binding = 0;

	for ( auto const& b_idx : used_buffers ) {
		auto& b = self->data[ b_idx ];

		if ( b.buffer_resource ) {
			buffers.push_back( b.buffer_resource );
		} else {
			// we need to flush in case not all buffers have been uploaded to gpu yet.
			if ( !buffers.empty() ) {
				encoder.bindVertexBuffers( first_binding, buffers.size(), buffers.data() );
				buffers.clear();
			}
			encoder.setVertexData( b.cpu_data.data(), b.cpu_data.size(), b_idx );
			first_binding = b_idx + 1;
		}
	}

	if ( !buffers.empty() ) {
		encoder.bindVertexBuffers( first_binding, buffers.size(), buffers.data() );
		buffers.clear();
	}

	uint32_t num_drawable_entities = self->num_vertices;

	if ( self->indices_data && false == should_ignore_indices ) {
		// In case there this mesh has index data
		// we first try if we can set it from gpu index buffer data
		// otherwise we set it via cpu index buffer data.
		size_t bytes_per_index = self->indices_data->num_bytes_per_stride;
		if ( self->indices_data->buffer_resource ) {
			encoder.bindIndexBuffer( self->indices_data->buffer_resource, 0, bytes_per_index == 2 ? le::IndexType::eUint16 : le::IndexType::eUint32 );
		} else if ( !self->indices_data->cpu_data.empty() ) {
			encoder.setIndexData( self->indices_data->cpu_data.data(), self->indices_data->cpu_data.size(), bytes_per_index == 2 ? le::IndexType::eUint16 : le::IndexType::eUint32 );
		}
		num_drawable_entities = le_mesh_get_index_count( self, nullptr );
	}

	return num_drawable_entities;
}

// ----------------------------------------------------------------------

static void le_mesh_use_with_renderpass( le_mesh_o* self, le_renderpass_o* rp_, le_mesh_attribute_info_t const* attribute_infos, size_t attribute_infos_count ) {

	le::RenderPass rp( rp_ );

	if ( attribute_infos == nullptr ) {

		// All buffers owned by this mesh are in use.

		for ( auto const& b : self->data ) {
			if ( b.buffer_resource ) {
				rp.useBufferResource( b.buffer_resource, le::AccessFlagBits2::eVertexAttributeRead );
			}
		}

	} else {

		// We must filter by the buffers that are named in attribute_infos

		std::set<le_buffer_resource_handle> used_buffers; // unique, automatically sorted

		for ( auto a = attribute_infos; a != attribute_infos + attribute_infos_count; a++ ) {
			auto it = self->data_descriptors.find( a->name );
			if ( it != self->data_descriptors.end() ) {
				auto buffer_resource = self->data[ it->second.data_idx ].buffer_resource;
				if ( buffer_resource ) {
					used_buffers.emplace( buffer_resource );
				}
			}
		}
		for ( auto const& b : used_buffers ) {
			rp.useBufferResource( b, le::AccessFlagBits2::eVertexAttributeRead );
		}
	}

	// Flag index resource as used, if there exists an index resource in the mesh

	if ( self->indices_data && self->indices_data->buffer_resource ) {
		rp.useBufferResource( self->indices_data->buffer_resource, le::AccessFlagBits2::eIndexRead );
	}
}

// ----------------------------------------------------------------------

static void le_mesh_debug_draw_meshes( le_mesh_debug_draw_data_t* meshes, size_t meshes_count, le_renderpass_o* rp_ ) {

	static constexpr size_t                        C_ATTR_COUNT               = 1;
	static constexpr le_mesh_attribute_info_t      attributes[ C_ATTR_COUNT ] = {
        { le_mesh_attribute_name::ePosition, sizeof( float ) * 3 }, // location 0

    };

	// ---------- Declare Resources to be used with given renderpass

	le::RenderPass rp( rp_ );

	std::set<le_mesh_o*> unique_meshes;

	for ( auto m = meshes; m != meshes + meshes_count; m++ ) {
		unique_meshes.insert( m->mesh );
	}

	for ( auto& m : unique_meshes ) {
		le_mesh_use_with_renderpass( m, rp_, attributes, 1 );
	}

	// ---------- Draw Meshes into given renderpass

	// First, we need to capture the per-mesh parameters into a closure,
	// so that these will be available in the draw callback.
	//
	struct mesh_draw_capture_t {
		size_t                    num_items;
		le_mesh_debug_draw_data_t items[];
	};

	size_t               closure_sz   = sizeof( size_t ) + sizeof( le_mesh_debug_draw_data_t ) * meshes_count;
	mesh_draw_capture_t* closure_data = ( mesh_draw_capture_t* )malloc( closure_sz );

	closure_data->num_items = meshes_count;

	// We need to capture the sample count for the current renderpass,
	// as this has a direct effect on the graphics pipeline;

	memcpy( closure_data->items, meshes, sizeof( le_mesh_debug_draw_data_t ) * meshes_count );

	rp.setExecuteCallbackWithLocalUserData( closure_data, closure_sz, []( le_command_buffer_encoder_o* encoder_, void* user_data ) {
		// Draw main scene

		le::GraphicsEncoder encoder{ encoder_ };

		auto extents = encoder.getRenderpassExtent();

		le::Viewport viewports[ 1 ] = {
		    { 0.f, 0.f, float( extents.width ), float( extents.height ), 0.f, 1.f },
		};

		// Data as it is laid out in the shader ubo.
		// Be careful to respect std430 or std140 layout
		// depending on what you specify in the
		// shader.
		struct MvpUbo {
			float mvp[ 16 ];
			float colour[ 4 ];
		};

		// Create shader modules using local shaders.
		static auto shaderVert =
		    LeShaderModuleBuilder( encoder.getPipelineManager() )
		        .setShaderStage( le::ShaderStage::eVertex )
		        .setSpirvCode( SPIRV_SOURCE_DEFAULT_VERT, sizeof( SPIRV_SOURCE_DEFAULT_VERT ) / sizeof( uint32_t ) )
		        .setSourceLanguage( le::ShaderSourceLanguage::eSpirv )
		        .build();

		static auto shaderFrag =
		    LeShaderModuleBuilder( encoder.getPipelineManager() )
		        .setShaderStage( le::ShaderStage::eFragment )
		        .setSpirvCode( SPIRV_SOURCE_DEFAULT_FRAG, sizeof( SPIRV_SOURCE_DEFAULT_FRAG ) / sizeof( uint32_t ) )
		        .setSourceLanguage( le::ShaderSourceLanguage::eSpirv )
		        .build();

		// ---------

		static std::unordered_map<uint64_t, le_gpso_handle> pipeline_cache;

		auto   closure               = ( mesh_draw_capture_t* )user_data;
		size_t previous_binding_hash = 0;
		le_mesh_o* previous_mesh         = nullptr;
		encoder.setLineWidth( 1 );
		for ( auto m = closure->items; m != closure->items + closure->num_items; m++ ) {

			size_t num_ad = C_ATTR_COUNT;
			size_t num_bd = C_ATTR_COUNT;

			struct binding_info_t {
				le_vertex_input_attribute_description ad[ C_ATTR_COUNT ] = {};
				le_vertex_input_binding_description   bd[ C_ATTR_COUNT ] = {};
			} binding_info;

			auto result = le_mesh_get_vertex_input_descriptions( m->mesh, attributes, C_ATTR_COUNT, binding_info.ad, &num_ad, binding_info.bd, &num_bd );
			assert( result == true );

			// ---------

			uint64_t hash_seed    = 0;
			uint64_t binding_hash = le_core_spooky_hash_64( &binding_info, sizeof( binding_info ), hash_seed );

			auto& pipeline_handle = pipeline_cache[ binding_hash ];

			if ( pipeline_handle == nullptr ) {
				// Create a pipeline using these shader modules
				// -- Note: you must make sure that the
				// pipeline uses the right sample count.
				pipeline_handle =
				    LeGraphicsPipelineBuilder( encoder.getPipelineManager() )
				        .addShaderStage( shaderVert )
				        .addShaderStage( shaderFrag )
				        .withRasterizationState()
				        .setPolygonMode( le::PolygonMode::eLine )
				        .end()
				        .setVertexInputBindingDescriptions( binding_info.bd, num_bd )
				        .setVertexInputAttributeDescriptions( binding_info.ad, num_ad )
				        .build();
			}

			MvpUbo mvp{};

			memcpy( mvp.mvp, m->mvp, sizeof( m->mvp ) );
			memcpy( mvp.colour, m->colour, sizeof( mvp.colour ) );

			if ( previous_binding_hash != binding_hash ) {
				// Only bind pipeline if the pipeline has changed
				encoder.bindGraphicsPipeline( pipeline_handle );
			}

			encoder.setArgumentData( LE_ARGUMENT_NAME( "Mvp" ), &mvp, sizeof( MvpUbo ) );

			if ( previous_mesh != m->mesh ) {
				// Only bind mesh if the mesh has changed
				le_mesh_bind_to_encoder( m->mesh, encoder, attributes, C_ATTR_COUNT );
				previous_mesh = m->mesh;
			}

			if ( m->mesh->indices_data ) {
				encoder.drawIndexed( m->mesh->indices_data->cpu_data.size() / m->mesh->indices_data->num_bytes_per_stride );
			} else {
				encoder.draw( m->mesh->num_vertices );
			}

			previous_binding_hash = binding_hash;
		}
	} );

	free( closure_data );
}

// ----------------------------------------------------------------------

static void le_mesh_debug_draw( le_mesh_o* mesh, float mvp[ 16 ], float colour[ 4 ] ) {

	le_mesh_debug_draw_data_t data{
	    .mesh = mesh,
	};

	memcpy( data.mvp, mvp, sizeof( float ) * 16 );
	memcpy( data.colour, colour, sizeof( float ) * 4 );

	auto& controller = le_mesh_api_i->le_mesh_singleton;
	// TODO: we should proabably protect the controller state vector via a mutex
	// and this should be per-frame.
	if ( controller->current_draw_batch == nullptr ) {
		controller->current_draw_batch = new draw_data_t{};
	}

	controller->current_draw_batch->draw_data.emplace_back( std::move( data ) );
}

// ----------------------------------------------------------------------

static void debug_cb_on_frame_clear_callback( void* data ) {
	draw_data_t* d = ( draw_data_t* )( data );
	// Clear temporary data that was used to draw debug meshes for this frame
	delete d;
}

// ----------------------------------------------------------------------

static void debug_batch_draw( le_renderpass_o* rp, le_rendergraph_o* rg, bool should_keep_data ) {
	auto& controller = le_mesh_api_i->le_mesh_singleton;

	if ( nullptr == controller->current_draw_batch ) {
		return;
	}

	le_mesh_debug_draw_meshes( controller->current_draw_batch->draw_data.data(), controller->current_draw_batch->draw_data.size(), rp );

	if ( false == should_keep_data ) {

		// this should happen in the on_frame_recorded_complete_callback

		le_on_frame_clear_callback_data_t cb{
		    .cb_fun    = &le_mesh_api_i->le_mesh_debug_helpers_i.on_frame_clear_cb,
		    .user_data = controller->current_draw_batch, // takes ownership
		};

		le_renderer_api_i->le_rendergraph_i.add_on_frame_clear_callbacks( rg, &cb, 1 );
		controller->current_draw_batch = nullptr;
	}
}

// ----------------------------------------------------------------------

ISL_API_ATTR void le_module_register_le_mesh_load_from_ply( void* api ); // ffdecl.

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_mesh, api ) {
	auto const& api_i     = static_cast<le_mesh_api*>( api );
	auto&       le_mesh_i = static_cast<le_mesh_api*>( api )->le_mesh_i;
	auto&       le_mesh_debug_helpers_i = static_cast<le_mesh_api*>( api )->le_mesh_debug_helpers_i;

	le_module_register_le_mesh_load_from_ply( api );

	le_mesh_i.allocate_vertex_data         = le_mesh_allocate_vertex_data;
	le_mesh_i.allocate_index_data          = le_mesh_allocate_index_data;
	le_mesh_i.read_attribute_data_into     = le_mesh_read_attribute_data_into;
	le_mesh_i.read_vertex_data_into_buffer = le_mesh_read_vertex_data_into_buffer;

	le_mesh_i.set_vertex_count = le_mesh_set_vertex_count;

	le_mesh_i.get_vertex_count = le_mesh_get_vertex_count;
	le_mesh_i.get_index_count  = le_mesh_get_index_count;

	le_mesh_i.get_attribute_infos_for_binding = le_mesh_get_attribute_infos_for_binding;
	le_mesh_i.get_vertex_buffers_count        = le_mesh_get_vertex_buffers_count;
	le_mesh_i.read_index_data_into            = le_mesh_read_index_data_into;

	le_mesh_i.get_attribute_data = le_mesh_get_attribute_data;
	le_mesh_i.get_index_data     = le_mesh_get_index_data;

	le_mesh_i.get_attribute_buffer = le_mesh_get_attribute_buffer;
	le_mesh_i.get_index_buffer     = le_mesh_get_index_buffer;

	le_mesh_i.bind_to_encoder  = le_mesh_bind_to_encoder;
	le_mesh_i.use_with_renderpass = le_mesh_use_with_renderpass;

	le_mesh_i.submit_meshes_to_rendergraph  = le_mesh_submit_meshes_to_rendergraph;
	le_mesh_i.get_vertex_input_descriptions = le_mesh_get_vertex_input_descriptions;

	le_mesh_i.apply_vertex_input_descriptions = le_mesh_apply_vertex_input_descriptions;

	le_mesh_i.debug_draw            = le_mesh_debug_draw;

	le_mesh_i.clear   = le_mesh_clear;
	le_mesh_i.create  = le_mesh_create;
	le_mesh_i.destroy = le_mesh_destroy;

	// Debug Helpers

	le_mesh_debug_helpers_i.debug_batch_draw      = debug_batch_draw;
	le_mesh_debug_helpers_i.on_frame_clear_cb     = debug_cb_on_frame_clear_callback;

	if ( nullptr == api_i->le_mesh_singleton ) {
		api_i->le_mesh_singleton = new le_mesh_singleton_o{};
	}
}

// ----------------------------------------------------------------------

LE_MODULE_UNREGISTER_IMPL( le_mesh, api ) {
	auto const& api_i = static_cast<le_mesh_api*>( api );
	if ( nullptr != api_i->le_mesh_singleton ) {
		delete api_i->le_mesh_singleton->current_draw_batch;
		delete api_i->le_mesh_singleton;
	}
}
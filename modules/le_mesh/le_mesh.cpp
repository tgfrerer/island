#include "le_mesh.h"
#include "le_core.h"
#include "le_log.h"
#include "le_renderer.h"
#include "le_renderer.hpp"

#include <cstdlib>
#include <vector>
#include <cstring> // for memcopy
#include <map>
#include <set>
#include <memory>

static auto logger = le::Log( "le_mesh" );

struct buffer_data_t {
	std::vector<uint8_t> cpu_data;

	le_buffer_resource_handle buffer_resource      = nullptr;
	le_resource_info_t        buffer_resource_info = {};
	//
	uint32_t num_bytes_per_stride = 0;    // bytes per index or bytes per vertex on this buffer
	bool     is_tainted           = true; // whether the resource needs to be uploaded or not
};

/*

  TODO: We want a way to convert our mesh from SOA to AOS, so that we may interleave
  attributes. this only makes sense if we know which attributes we will need when drawing.

    - maybe we can even generate bindings from the current attribute setup
    - a mesh should be able to convert to a different layout
    - a mesh should know when to re-submit itself



*/

struct buffer_data_descriptor {
	uint32_t idx               = 0;
	uint32_t interleave_offset = 0;
	uint32_t bytes_per_vertex  = 0;
};

struct le_mesh_o {
	size_t num_vertices = 0; // number of vertices - all attribute_data must have this count

	std::vector<buffer_data_t>                                      data;
	std::map<le_mesh_api::attribute_name_t, buffer_data_descriptor> data_descriptors;

	le_mesh_api::attribute_name_t existing_attribute_names;

	buffer_data_t* indices_data = nullptr;
};

// ----------------------------------------------------------------------

static le_mesh_o* le_mesh_create() {
	auto self = new le_mesh_o{};
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
// write contents of our internal data out to gpu memory - or to other kind
// of memory, really.

// Now, this method can get quite complicated
// because we cannot assume that our source data is continuous.
//
// how about we create something like an iterator for each attribute
// if our source data is not continuous?
//
// we also need to take into account that the output data might
// be interleaved, but in a different way.
//
// we want this to be fast, but it should not be optimized to the point
// where it becomes unreadable -- data is usually only written out
// rarely.
//
// but because the data can be quite substantial, we want this to be
// as contiguous as possible.
//
// in case we want to interleave our output, we want to
// change the signature for this function so that it supports
// requesting interleaved data via a vector of `attribute_info`
//
// if the attribute info array that we get perfectly aligns
// with the attribute info array for an existing buffer, we can
// copy out the buffer in one go -- that's the fast path.
//
// if the out attribute info array does not match the current attribute info array
// we need to build some iterators, i think; perhaps we can use an output_iterator
// and hope that the compiler does the work for us?
//
//
// Another thing that we might want to have is a reformat() function for mesh
// which allows us to re-arrange our data and make it interleave or do some other
// things with it. there's also zeux' meshopt that could come in handy.
//

static void le_mesh_read_vertex_data_into_buffer( le_mesh_o const* self, void* target, size_t target_capacity_num_bytes,
                                                  le_mesh_api::attribute_info_t* dst_attribute_info,
                                                  size_t dst_attribute_info_count, size_t first_vertex ) {

	struct it_t {
		uint8_t const* src;        // source data pointer (these may be into different source data buffers)
		uint8_t        src_stride; // increment to source data pointer on every iteration
		uint16_t       n_bytes;    // number of bytes that need to be copied for every iteration
		uint8_t*       dst;        // dst data pointer - stride it the same for all dst data pointers.
	};

	std::vector<it_t> iterators;
	size_t            dst_stride = 0;

	{
		std::vector<le_mesh_api::attribute_info_t> dst_info{ dst_attribute_info, dst_attribute_info + dst_attribute_info_count };

		uint8_t* target_head = reinterpret_cast<uint8_t*>( target );

		iterators.reserve( dst_info.size() );

		size_t dst_offset_sum = 0;

		for ( auto& d : dst_info ) {

			if ( ( d.name & self->existing_attribute_names ) == 0 ) {

				// if name not in source names or if it does not exist in our data we must ignore it
				d.name = le_mesh_api::attribute_name_t::eUndefined;

			} else {

				auto const& desc = self->data_descriptors.at( d.name );

				it_t iterator{
				    .src        = self->data[ desc.idx ].cpu_data.data() + desc.interleave_offset,
				    .src_stride = uint8_t( self->data[ desc.idx ].cpu_data.size() / self->num_vertices ),
				    .n_bytes    = std::min<uint16_t>( desc.bytes_per_vertex, d.bytes_per_vertex ), // note: if dst < src this means that there may be garbage data in dst if dst if not zeroed out before copy
				    .dst        = target_head + dst_offset_sum,
				};

				iterators.emplace_back( std::move( iterator ) );
			}

			dst_offset_sum += d.bytes_per_vertex;
		}

		dst_stride = dst_offset_sum;
	}

	// we want to stop when either one of our source iterators runs out - that's bounded by number of vertices
	// or the target capacity runs out.

	size_t target_capacity_in_vertices = target_capacity_num_bytes / dst_stride;
	size_t num_max_iterations          = std::min<size_t>( target_capacity_in_vertices, self->num_vertices );

	if ( first_vertex >= num_max_iterations ) {
		// nothing to do.
		return;
	}

	// optimization: if we have a single iterator,
	// and this iterator has .src_stride == .n_bytes
	// then we can do a block memcpy.

	// we go over all iterators - first, then iterate down the line
	// the hope is that this will lead to greater cache locality.
	for ( it_t it : iterators ) {
		it.src += it.src_stride * first_vertex;
		// it.dst += dst_stride * first_vertex; // if we do this
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
    le_mesh_api::attribute_name_t attribute_name,
    uint32_t*                     out_num_bytes_per_vertex,
    size_t*                       num_vertices,
    size_t                        first_vertex, // first vertex
    uint32_t                      stride,
    uint32_t                      initial_stride_offset ) {

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

	le_mesh_api::attribute_info_t attribute_infos[] = {
	    {
	        .name             = le_mesh_api::attribute_name_t::ePadding,
	        .bytes_per_vertex = initial_stride_offset,
	    },
	    {
	        .name             = attribute_name,
	        .bytes_per_vertex = d_it->second.bytes_per_vertex,
	    },
	    {
	        .name             = le_mesh_api::attribute_name_t::ePadding,
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

	auto& bytes_vec = self->indices_data;

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

static void* le_mesh_allocate_index_data( le_mesh_o* self, size_t num_indices, uint32_t* num_bytes_per_index ) {

	if ( nullptr == num_bytes_per_index ) {
		logger.error( "You must specify the number of bytes per index pointer." );
		return nullptr;
	}

	if ( nullptr == self->indices_data ) {
		self->indices_data = new buffer_data_t{};
	}

	// If number of vertices is greater than what can be represented with an 16 bit index, we must use
	// 32 bit indices.

	{
		uint32_t required_num_bytes_per_index = ( self->num_vertices <= ( 1 << 16 ) ) ? 2 : 4;

		// Go for the lowest number of bytes per index that you can get away with,
		// but respect the client's request if they want a higher number of indices.
		*num_bytes_per_index              = std::min( std::max( required_num_bytes_per_index, *num_bytes_per_index ), uint32_t( 4 ) );
		self->indices_data->num_bytes_per_stride = *num_bytes_per_index;
	}

	self->indices_data->cpu_data.resize( self->indices_data->num_bytes_per_stride * num_indices );

	return self->indices_data->cpu_data.data();
}

// ----------------------------------------------------------------------
// allocate data for a buffer of interleaved vertex data
// attribute_infos must hold information for the current buffer
// and any data that gets interleaved with this buffer.
static void* le_mesh_allocate_vertex_buffer( le_mesh_o* self, le_mesh_api::attribute_info_t const* attribute_infos, size_t num_attribute_infos ) {

	if ( attribute_infos == nullptr || num_attribute_infos == 0 || self->num_vertices == 0 ) {
		return nullptr;
	}

	// ---------| invariant: attribute_infos is valid, num_attribute_infos > 0

	// first we need to make sure that none of the given attributes is already present


	le_mesh_api::attribute_name_t new_attribute_names = {};
	for ( auto p_attr = attribute_infos; p_attr != attribute_infos + num_attribute_infos; p_attr++ ) {
		new_attribute_names = le_mesh_api::attribute_name_t( new_attribute_names | p_attr->name );
	}

	if ( self->existing_attribute_names & new_attribute_names ) {
		logger.warn( "attribute name does already exist: %d", self->existing_attribute_names & new_attribute_names );
		return nullptr;
	}

	// --------| invariant none of these attribute names already exits, we can add a new buffer

	uint32_t num_bytes_per_vertex = 0;

	uint32_t buffer_id = self->data.size();

	for ( auto p_attr = attribute_infos; p_attr != attribute_infos + num_attribute_infos; p_attr++ ) {
		buffer_data_descriptor d{
		    .idx               = buffer_id,
		    .interleave_offset = num_bytes_per_vertex,
		    .bytes_per_vertex  = p_attr->bytes_per_vertex,
		};
		self->data_descriptors.emplace( p_attr->name, d );
		num_bytes_per_vertex += p_attr->bytes_per_vertex;
	}

	buffer_data_t data_entry{
	    .cpu_data             = std::vector<uint8_t>( num_bytes_per_vertex * self->num_vertices ),
	    .buffer_resource      = nullptr,
	    .buffer_resource_info = le::BufferInfoBuilder().build(),
	    .num_bytes_per_stride = num_bytes_per_vertex,
	    .is_tainted           = true,
	};

	self->data.emplace_back( std::move( data_entry ) );

	self->existing_attribute_names = le_mesh_api::attribute_name_t( self->existing_attribute_names | new_attribute_names );

	return self->data.back().cpu_data.data();
}

// ----------------------------------------------------------------------

static void* le_mesh_allocate_attribute_data( le_mesh_o* self, le_mesh_api::attribute_name_t attribute_name, uint32_t num_bytes_per_vertex ) {
	le_mesh_api::attribute_info_t info{
	    .name             = attribute_name,
	    .bytes_per_vertex = num_bytes_per_vertex };
	return le_mesh_allocate_vertex_buffer( self, &info, 1 );
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

// // ----------------------------------------------------------------------
// // read attribute info into a given array of data
static void le_mesh_read_attribute_infos_into( le_mesh_o* self, le_mesh_api::attribute_info_t* target, size_t* num_attributes_in_target ) {

	if ( nullptr == num_attributes_in_target ) {
		return;
	}

	// ----------| invariant: num_attributes_in_target was set

	size_t num_available_slots = *num_attributes_in_target;

	// write back the number of attributes that this mesh contains.
	*num_attributes_in_target = self->data_descriptors.size();

	if ( target ) {

		for ( auto const& a_e : self->data_descriptors ) {
			if ( num_available_slots == 0 ) {
				break;
			}

			auto& [ key, a ] = a_e;

			*target++ = {
			    .name             = key,
			    .bytes_per_vertex = a.bytes_per_vertex,
			};

			num_available_slots--;
		}
	}
}

// ----------------------------------------------------------------------

static void le_mesh_submit_meshes_to_rendergraph( le_mesh_o* const* meshes, size_t meshes_count, le_rendergraph_o* rg, le_renderer_o* renderer ) {

	struct upload_data_item_t {
		le_buffer_resource_handle buffer;
		std::vector<uint8_t>*     p_attribute_data;
	};

	std::vector<upload_data_item_t> upload_items; // upload data items

	for ( size_t i = 0; i != meshes_count; i++ ) {
		le_mesh_o* mesh = meshes[ i ];

		for ( auto& descriptor : mesh->data ) {

			// declare resource to rendergraph

			if ( nullptr == descriptor.buffer_resource ) {
				// we need to allocate the buffer resource first
				le_renderer_api_i->le_renderer_i.create_buf_resource_handle( renderer, "", 0, 0 );
			}

			if ( descriptor.is_tainted ) {
				upload_items.emplace_back( descriptor.buffer_resource, &descriptor.cpu_data );
				size_t num_bytes                            = mesh->num_vertices * descriptor.num_bytes_per_stride;
				descriptor.buffer_resource_info.buffer.size = num_bytes;
				descriptor.buffer_resource_info =
				    le::BufferInfoBuilder()
				        .addUsageFlags( le::BufferUsageFlagBits::eTransferDst | le::BufferUsageFlagBits::eVertexBuffer )
				        .setSize( num_bytes )
				        .build();
				descriptor.is_tainted = false;
			}

			le_renderer_api_i->le_rendergraph_i.declare_resource( rg, descriptor.buffer_resource, descriptor.buffer_resource_info );
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

	// now we have all upload items -- we need to add a transfer pass to the rendergraph
	// that does the transfer for us.

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
};

// ----------------------------------------------------------------------

ISL_API_ATTR void le_module_register_le_mesh_load_from_ply( void* api ); // ffdecl.

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_mesh, api ) {
	auto& le_mesh_i = static_cast<le_mesh_api*>( api )->le_mesh_i;

	le_module_register_le_mesh_load_from_ply( api );

	le_mesh_i.allocate_vertex_buffer   = le_mesh_allocate_vertex_buffer;
	le_mesh_i.allocate_attribute_data  = le_mesh_allocate_attribute_data;
	le_mesh_i.allocate_index_data      = le_mesh_allocate_index_data;
	le_mesh_i.read_attribute_data_into = le_mesh_read_attribute_data_into;

	le_mesh_i.set_vertex_count = le_mesh_set_vertex_count;
	le_mesh_i.get_vertex_count = le_mesh_get_vertex_count;

	le_mesh_i.get_index_count           = le_mesh_get_index_count;
	le_mesh_i.read_attribute_infos_into = le_mesh_read_attribute_infos_into;
	le_mesh_i.read_index_data_into      = le_mesh_read_index_data_into;

	le_mesh_i.clear   = le_mesh_clear;
	le_mesh_i.create  = le_mesh_create;
	le_mesh_i.destroy = le_mesh_destroy;
}

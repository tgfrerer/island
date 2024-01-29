#include "le_resource_manager.h"
#include "le_core.h"
#include "le_renderer.hpp"
#include "le_pixels.h"
#include "le_file_watcher.h"
#include "le_log.h"

#include <string>
#include <vector>
#include <assert.h>

#include <unordered_map>

// only used to print debug messages:
#include "private/le_renderer/le_resource_handle_t.inl"

static auto logger = le::Log( "resource_manager" );

// ----------------------------------------------------------------------
// TODO:
// * add a method to remove resources from the manager
// * add a method to update resources from within the manager
// ----------------------------------------------------------------------

struct le_resource_manager_o {

	le_file_watcher_o* file_watcher = nullptr;

	struct image_data_layer_t {
		le_pixels_o*                   pixels;
		std::string                    path;
		bool                           was_uploaded     = false;
		bool                           extents_inferred = false;
		le_resource_info_t::ImageInfo* image_info; // non-owning
		int                            watch_id = -1;
		uint32_t                       width;
		uint32_t                       height;
	};

	struct resource_item_t {
		le_img_resource_handle          image_handle;
		le_resource_info_t              image_info;
		std::vector<image_data_layer_t> image_layers; // must have at least one element
	};

	std::unordered_map<le_resource_handle, resource_item_t> resources;
};

// ----------------------------------------------------------------------

static bool setupTransferPass( le_renderpass_o* pRp, void* user_data ) {
	le::RenderPass rp{ pRp };
	auto           manager = static_cast<le_resource_manager_o const*>( user_data );

	if ( manager->resources.empty() ) {
		return false;
	}

	// --------| invariant: some elements need upload

	bool needsTransfer = false;

	for ( auto const& [ k, r ] : manager->resources ) {

		bool uses_resource = false;

		for ( auto const& layer : r.image_layers ) {
			if ( layer.was_uploaded == false ) {
				uses_resource = true;
				break;
			}
		}

		if ( uses_resource ) {
			rp.useImageResource( r.image_handle, le::AccessFlagBits2::eTransferWrite );
			needsTransfer = true;
		}
	}

	return needsTransfer;
}

// ----------------------------------------------------------------------

static void execTransferPass( le_command_buffer_encoder_o* pEncoder, void* user_data ) {
	le::TransferEncoder encoder{ pEncoder };
	auto                manager = static_cast<le_resource_manager_o*>( user_data );

	// we will probably end up with a number of write operations which will all target the same image resource,
	// but will write into different aspects of memory associated with the resource.

	// First figure out whether we must write to an image at all

	using namespace le_pixels;

	for ( auto& [ k, r ] : manager->resources ) {

		uint32_t const num_layers     = uint32_t( r.image_layers.size() );
		uint32_t const image_width    = r.image_info.image.extent.width;
		uint32_t const image_height   = r.image_info.image.extent.height;
		uint32_t const image_depth    = r.image_info.image.extent.depth;
		uint32_t const num_mip_levels = r.image_info.image.mipLevels;

		uint32_t layer_index = 0;
		for ( auto& layer : r.image_layers ) {

			// We can fill in the correct handling for mutiple mip levels later.
			// for now, assert that there is exatcly one mip level.

			if ( layer.was_uploaded ) {
				layer_index++;
				continue;
			}

			// --------| invariant: layer was not yet uploaded.

			for ( uint32_t mip_level = 0; mip_level != 1; mip_level++ ) {

				assert( mip_level == 0 && "mip level greater than 0 not implemented" );
				uint32_t width  = layer.extents_inferred ? ( layer.width >> mip_level ) : ( image_width >> mip_level );
				uint32_t height = layer.extents_inferred ? ( layer.height >> mip_level ) : ( image_height >> mip_level );
				uint32_t depth  = image_depth;

				le_write_to_image_settings_t write_info =
				    le::WriteToImageSettingsBuilder()
				        .setDstMiplevel( mip_level )
				        .setNumMiplevels( num_mip_levels )
				        .setArrayLayer( layer_index ) // faces are indexed: +x, -x, +y, -y, +z, -z
				        .setImageH( height )
				        .setImageW( width )
				        .setImageD( depth )
				        .build();

				auto     pixels    = layer.pixels;
				auto     info      = le_pixels_i.get_info( pixels );
				uint32_t num_bytes = info.byte_count; // TODO: make sure to get correct byte count for mip level, or compressed image.
				void*    bytes     = le_pixels_i.get_data( pixels );

				encoder.writeToImage( r.image_handle, write_info, bytes, num_bytes );
			}
			layer.was_uploaded = true;
			layer_index++;
		}
	}
}

// ----------------------------------------------------------------------

static void infer_from_le_format( le::Format const& format, uint32_t* num_channels, le_pixels_info::Type* pixels_type ) {
	switch ( format ) {
	case le::Format::eUndefined: // deliberate fall-through
	case le::Format::eR8G8B8A8Unorm:
		*num_channels = 4;
		*pixels_type  = le_pixels_info::Type::eUInt8;
		return;
	case le::Format::eR8Unorm:
		*num_channels = 1;
		*pixels_type  = le_pixels_info::Type::eUInt8;
		return;
	case le::Format::eR16G16B16A16Unorm:
		*num_channels = 4;
		*pixels_type  = le_pixels_info::Type::eUInt16;
		return;
	default:
		logger.error( "Unhandled image format" );
		assert( false && "Unhandled image format." );
	}
}

// ----------------------------------------------------------------------

static void update_image_array_layer( le_resource_manager_o::image_data_layer_t& layer_data ) {

	// we must find out the pixels type from image info format
	uint32_t             num_channels = 0;
	le_pixels_info::Type pixels_type{};

	infer_from_le_format( layer_data.image_info->format, &num_channels, &pixels_type );

	le_pixels_o* new_pixels = le_pixels::le_pixels_i.create( layer_data.path.c_str(), num_channels, pixels_type );

	if ( layer_data.pixels && new_pixels ) {
		le_pixels::le_pixels_i.destroy( layer_data.pixels );
	}

	if ( new_pixels ) {
		layer_data.pixels = new_pixels;
	} else {
		return;
	}

	auto info = le_pixels::le_pixels_i.get_info( layer_data.pixels );
	if ( layer_data.extents_inferred ) {
		layer_data.image_info->extent.depth  = info.depth;
		layer_data.image_info->extent.width  = info.width;
		layer_data.image_info->extent.height = info.height;
	}
	layer_data.width  = info.width;
	layer_data.height = info.height;

	layer_data.image_info->usage |= ( le::ImageUsageFlagBits::eTransferDst | le::ImageUsageFlagBits::eSampled | le::ImageUsageFlagBits::eStorage );

	layer_data.was_uploaded = false;
}

// ----------------------------------------------------------------------

static void le_resource_manager_file_watcher_callback( char const* path, void* user_data ) {
	// we must update the image array layer in question
	auto layer = static_cast<le_resource_manager_o::image_data_layer_t*>( user_data );
	logger.info( "Reloading file: %s", path );
	update_image_array_layer( *layer );
}

// ----------------------------------------------------------------------

static le_resource_manager_o* le_resource_manager_create() {
	auto self = new le_resource_manager_o{};

	return self;
}

// ----------------------------------------------------------------------

static void le_resource_manager_destroy( le_resource_manager_o* self ) {

	using namespace le_pixels;

	if ( self->file_watcher ) {
		le_file_watcher::le_file_watcher_i.destroy( self->file_watcher );
	}

	for ( auto& [ k, r ] : self->resources ) {
		for ( auto& l : r.image_layers ) {
			if ( l.pixels ) {
				le_pixels_i.destroy( l.pixels );
				l.pixels = nullptr;
			}
		}
	}
	delete ( self );
}

// ----------------------------------------------------------------------

static void le_resource_manager_update( le_resource_manager_o* self, le_rendergraph_o* rg ) {
	using namespace le_renderer;

	// Poll for any files that might have changed on-disk -
	// this will trigger callbacks for any files which have changed.
	if ( self->file_watcher ) {
		le_file_watcher::le_file_watcher_i.poll_notifications( self->file_watcher );
	}

	for ( auto& [ k, r ] : self->resources ) {
		rendergraph_i.declare_resource( rg, k, r.image_info );
	}

	auto renderPassTransfer =
	    le::RenderPass( "xfer_le_resource_manager", le::QueueFlagBits::eTransfer )
	        .setSetupCallback( self, setupTransferPass )  // decide whether to go forward
	        .setExecuteCallback( self, execTransferPass ) //
	    ;

	rendergraph_i.add_renderpass( rg, renderPassTransfer );
}

// ----------------------------------------------------------------------
// NOTE: You must provide an array of paths in image_paths, and the
// array's size must match `image_info.image.arrayLayers`
// Most meta-data about the image file is loaded via image_info
static void le_resource_manager_add_item( le_resource_manager_o*        self,
                                          le_img_resource_handle const* image_handle,
                                          le_resource_info_t const*     image_info,
                                          char const**                  image_paths,
                                          bool                          should_watch ) {

    auto [ it, was_emplaced ] = self->resources.emplace( *image_handle, le_resource_manager_o::resource_item_t{} );

	if ( was_emplaced ) {
		auto& item = it->second;

		item.image_handle = *image_handle;
		item.image_info   = *image_info;
		item.image_layers.resize( image_info->image.arrayLayers );

		bool extents_inferred = false;
		if ( item.image_info.image.extent.width == 0 ||
		     item.image_info.image.extent.height == 0 ||
		     item.image_info.image.extent.depth == 0 ) {
			extents_inferred = true;
		}

		{
			int i = 0;
			for ( auto& l : item.image_layers ) {
				l.path             = image_paths[ i ];
				l.was_uploaded     = false;
				l.image_info       = &item.image_info.image;
				l.extents_inferred = extents_inferred;

				update_image_array_layer( l );
				i++;
			}
		}

		if ( should_watch ) {
			if ( nullptr == self->file_watcher ) {
				self->file_watcher = le_file_watcher::le_file_watcher_i.create();
			}
			for ( auto& l : item.image_layers ) {
				le_file_watcher_watch_settings watch_settings{};
				watch_settings.filePath           = l.path.c_str();
				watch_settings.callback_fun       = le_core_forward_callback( le_resource_manager::api->le_resource_manager_private_i.file_watcher_callback );
				watch_settings.callback_user_data = &l;
				l.watch_id                        = le_file_watcher::le_file_watcher_i.add_watch( self->file_watcher, &watch_settings );
			}
		}

		assert( item.image_info.image.extent.width != 0 &&
		        item.image_info.image.extent.height != 0 &&
		        item.image_info.image.extent.depth != 0 &&
		        "Image extents for resource are not valid." );
	} else {
		logger.error( "Resource '%s' was added more than once.", ( *image_handle )->data->debug_name );
	}
}

// ----------------------------------------------------------------------

static bool le_resource_manager_remove_item( le_resource_manager_o* self, le_img_resource_handle const* resource_handle ) {

	// TODO: you must be careful not to remove an item that might still be used for a transfer
	// you might want to tap into the backend's on_fence_reached callback to only remove resources
	// once we can be sure that there is no more dependency on them.
	//
	// Although for now we assume that the recording step of our pipeline always happens on the same thread as the
	// thread that declares the rendergraph.

	auto it = self->resources.find( *resource_handle );

	if ( it == self->resources.end() ) {
		logger.warn( "Could not remove resource. Resource '%s' not found.", ( *resource_handle )->data->debug_name );
		return false;
	}

	// ----------| Invariant: Resource was found

	auto [ k, r ] = *it;

	for ( auto& l : r.image_layers ) {

		if ( l.watch_id != -1 ) {
			le_file_watcher::le_file_watcher_i.remove_watch( self->file_watcher, l.watch_id );
			l.watch_id = -1;
		}
		if ( l.pixels ) {
			le_pixels::le_pixels_i.destroy( l.pixels );
			l.pixels = nullptr;
		}
	}

	self->resources.erase( it );

	return true;
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_resource_manager, api ) {
	auto& le_resource_manager_i         = static_cast<le_resource_manager_api*>( api )->le_resource_manager_i;
	auto& le_resource_manager_private_i = static_cast<le_resource_manager_api*>( api )->le_resource_manager_private_i;

	le_resource_manager_i.create      = le_resource_manager_create;
	le_resource_manager_i.destroy     = le_resource_manager_destroy;
	le_resource_manager_i.update      = le_resource_manager_update;
	le_resource_manager_i.add_item    = le_resource_manager_add_item;
	le_resource_manager_i.remove_item = le_resource_manager_remove_item;

	le_resource_manager_private_i.file_watcher_callback = le_resource_manager_file_watcher_callback;
}

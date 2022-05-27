#include "le_resource_manager.h"
#include "le_core.h"
#include "le_renderer.h"
#include "le_pixels.h"

#include <string>
#include <vector>
#include <assert.h>

#include "private/le_resource_handle_t.inl"

// ----------------------------------------------------------------------

struct le_resource_manager_o {

	struct image_data_layer_t {
		le_pixels_o* pixels;
		std::string  path;
		bool         was_uploaded = false;
	};

	struct resource_item_t {
		le_img_resource_handle          image_handle;
		le_resource_info_t              image_info;
		std::vector<image_data_layer_t> image_layers; // must have at least one element
	};

	std::vector<resource_item_t> resources;
};

// TODO:
// * add a method to remove resources from the manager

// ----------------------------------------------------------------------
static bool setupTransferPass( le_renderpass_o* pRp, void* user_data ) {
	le::RenderPass rp{ pRp };
	auto           manager = static_cast<le_resource_manager_o const*>( user_data );

	if ( manager->resources.empty() ) {
		return false;
	}

	// --------| invariant: some elements need upload

	bool needsTransfer = false;

	for ( auto const& r : manager->resources ) {

		bool uses_resource = false;

		for ( auto const& layer : r.image_layers ) {
			if ( layer.was_uploaded == false ) {
				uses_resource = true;
				break;
			}
		}

		if ( uses_resource ) {
			rp.useImageResource( r.image_handle, le::ImageUsageFlags( le::ImageUsageFlagBits::eTransferDst ) );
			needsTransfer = true;
		}
	}

	return needsTransfer;
}

// ----------------------------------------------------------------------
static void execTransferPass( le_command_buffer_encoder_o* pEncoder, void* user_data ) {
	le::Encoder encoder{ pEncoder };
	auto        manager = static_cast<le_resource_manager_o*>( user_data );

	// we will probably end up with a number of write operations which will all target the same image resource,
	// but will write into different aspects of memory associated with the resource.

	// First figure out whether we must write to an image at all

	using namespace le_pixels;

	for ( auto& r : manager->resources ) {

		uint32_t const num_layers     = uint32_t( r.image_layers.size() );
		uint32_t const image_width    = r.image_info.image.extent.width;
		uint32_t const image_height   = r.image_info.image.extent.height;
		uint32_t const image_depth    = r.image_info.image.extent.depth;
		uint32_t const num_mip_levels = r.image_info.image.mipLevels;

		for ( uint32_t layer = 0; layer != num_layers; layer++ ) {

			// we can fill in the correct handling for mutliple mip levels later.
			// for now, assert that there is exatcly one mip level.
			if ( r.image_layers[ layer ].was_uploaded ) {
				continue;
			}

			// --------| invariant: layer was not yet uploaded.

			for ( uint32_t mip_level = 0; mip_level != 1; mip_level++ ) {

				assert( mip_level == 0 && "mip level greater than 0 not implemented" );
				uint32_t width  = image_width >> mip_level;
				uint32_t height = image_height >> mip_level;
				uint32_t depth  = image_depth;

				le_write_to_image_settings_t write_info =
				    le::WriteToImageSettingsBuilder()
				        .setDstMiplevel( mip_level )
				        .setNumMiplevels( num_mip_levels )
				        .setArrayLayer( layer ) // faces are indexed: +x, -x, +y, -y, +z, -z
				        .setImageH( height )
				        .setImageW( width )
				        .setImageD( depth )
				        .build();

				auto     pixels    = r.image_layers[ layer ].pixels;
				auto     info      = le_pixels_i.get_info( pixels );
				uint32_t num_bytes = info.byte_count; // TODO: make sure to get correct byte count for mip level, or compressed image.
				void*    bytes     = le_pixels_i.get_data( pixels );

				encoder.writeToImage( r.image_handle, write_info, bytes, num_bytes );
			}
			r.image_layers[ layer ].was_uploaded = true;
		}
	}
}

// ----------------------------------------------------------------------

static void le_resource_manager_update( le_resource_manager_o* manager, le_rendergraph_o* module ) {
	using namespace le_renderer;

	// TODO: reload any images if you detect that their source on disk has changed.

	for ( auto& r : manager->resources ) {
		rendergraph_i.declare_resource( module, r.image_handle, r.image_info );
	}

	auto renderPassTransfer =
	    le::RenderPass( "xfer_le_resource_manager", le::RenderPassType::eTransfer )
	        .setSetupCallback( manager, setupTransferPass )  // decide whether to go forward
	        .setExecuteCallback( manager, execTransferPass ) //
	    ;

	rendergraph_i.add_renderpass( module, renderPassTransfer );
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
		assert( false && "Unhandled image format." );
	}
}

// ----------------------------------------------------------------------
// NOTE: You must provide an array of paths in image_paths, and the
// array's size must match `image_info.image.arrayLayers`
// Most meta-data about the image file is loaded via image_info
static void le_resource_manager_add_item( le_resource_manager_o*        self,
                                          le_img_resource_handle const* image_handle,
                                          le_resource_info_t const*     image_info,
                                          char const* const*            image_paths ) {

	le_resource_manager_o::resource_item_t item{};

	item.image_handle = *image_handle;
	item.image_info   = *image_info;
	item.image_layers.reserve( image_info->image.arrayLayers );

	bool extents_inferred = false;
	if ( item.image_info.image.extent.width == 0 ||
	     item.image_info.image.extent.height == 0 ||
	     item.image_info.image.extent.depth == 0 ) {
		extents_inferred = true;
	}

	for ( size_t i = 0; i != item.image_info.image.arrayLayers; ++i ) {
		le_resource_manager_o::image_data_layer_t layer_data{};
		layer_data.path         = std::string{ image_paths[ i ] };
		layer_data.was_uploaded = false;

		// we must find out the pixels type from image info format
		uint32_t             num_channels = 0;
		le_pixels_info::Type pixels_type{};

		infer_from_le_format( item.image_info.image.format, &num_channels, &pixels_type );

		layer_data.pixels = le_pixels::le_pixels_i.create( layer_data.path.c_str(), num_channels, pixels_type );

		if ( extents_inferred ) {
			auto info                           = le_pixels::le_pixels_i.get_info( layer_data.pixels );
			item.image_info.image.extent.depth  = std::max( item.image_info.image.extent.depth, info.depth );
			item.image_info.image.extent.width  = std::max( item.image_info.image.extent.width, info.width );
			item.image_info.image.extent.height = std::max( item.image_info.image.extent.height, info.height );
		}

		item.image_layers.emplace_back( layer_data );
	}

	assert( item.image_info.image.extent.width != 0 &&
	        item.image_info.image.extent.height != 0 &&
	        item.image_info.image.extent.depth != 0 &&
	        "Image extents for resource are not valid." );

	self->resources.emplace_back( item );
}

// ----------------------------------------------------------------------

static le_resource_manager_o* le_resource_manager_create() {
	auto self = new le_resource_manager_o{};
	return self;
}

// ----------------------------------------------------------------------

static void le_resource_manager_destroy( le_resource_manager_o* self ) {

	using namespace le_pixels;

	for ( auto& r : self->resources ) {
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

LE_MODULE_REGISTER_IMPL( le_resource_manager, api ) {
	auto& le_resource_manager_i = static_cast<le_resource_manager_api*>( api )->le_resource_manager_i;

	le_resource_manager_i.create   = le_resource_manager_create;
	le_resource_manager_i.destroy  = le_resource_manager_destroy;
	le_resource_manager_i.update   = le_resource_manager_update;
	le_resource_manager_i.add_item = le_resource_manager_add_item;
}

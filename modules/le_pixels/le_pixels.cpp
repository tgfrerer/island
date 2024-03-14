#include "le_pixels.h"
#include "le_log.h"
#include "le_core.h"
#include "3rdparty/stb_image.h"
#include "assert.h"
#include <iostream>
#include <iomanip>
#include <cstring> // for memcpy

#include "private/le_renderer/le_renderer_types.h"
#include "shared/interfaces/le_image_decoder_interface.h"

struct le_image_decoder_format_o {
	le::Format format;
};

struct le_image_decoder_o {
	// this is one way of defining an image decoder
	std::string image_path;

	int32_t image_width;
	int32_t image_height;
	int32_t image_depth;

	le::Format image_inferred_format;
	le::Format image_requested_format = le::Format::eUndefined; // requested format wins over inferred format
};

// ----------------------------------------------------------------------

static auto logger = LeLog( "le_pixels" );

// ----------------------------------------------------------------------
// Todo: rewrite this using codegen and vk.xml which includes a formats table
static void infer_data_info_from_le_format( le::Format const& format, int32_t* num_channels, le_num_type* num_type ) {
	switch ( format ) {
	case le::Format::eR8G8B8A8Uint: // deliberate fall-through
	case le::Format::eR8G8B8A8Unorm:
		*num_channels = 4;
		*num_type     = le_num_type::eUChar;
		return;
	case le::Format::eR8G8B8Uint: // deliberate fall-through
	case le::Format::eR8G8B8Unorm:
		*num_channels = 3;
		*num_type     = le_num_type::eUChar;
		return;
	case le::Format::eR8Unorm:
		*num_channels = 1;
		*num_type     = le_num_type::eUChar;
		return;
	case le::Format::eR32Sfloat:
		*num_channels = 1;
		*num_type     = le_num_type::eFloat;
		return;
	case le::Format::eR16G16B16Unorm:
		*num_channels = 3;
		*num_type     = le_num_type::eF16;
		return;
	case le::Format::eR32G32B32Sfloat:
		*num_channels = 3;
		*num_type     = le_num_type::eF32;
		return;
	case le::Format::eR16G16B16A16Unorm:
		*num_channels = 4;
		*num_type     = le_num_type::eF16;
		return;
	case le::Format::eR32G32B32A32Sfloat:
		*num_channels = 4;
		*num_type     = le_num_type::eF32;
		return;
	case le::Format::eUndefined: // deliberate fall-through
	default:
		logger.error( "Unhandled image format" );
		assert( false && "Unhandled image format." );
	}
}

// ----------------------------------------------------------------------
// load image file, and poke at file info; does not load file into memory
static le_image_decoder_o* le_image_decoder_create_image_decoder( char const* filepath ) {

	auto self = new le_image_decoder_o{};

	if ( filepath ) {

		int width;
		int height;
		int components;
		int result;

		result = stbi_info( filepath, &width, &height, &components );

		if ( result != 1 ) {
			delete self;
			logger.error( "Could not open file at '%s'", filepath );
			return nullptr;
		}

		self->image_path   = filepath;
		self->image_width  = uint32_t( width );
		self->image_height = uint32_t( height );
		self->image_depth  = 1;

		int is_16_bit = stbi_is_16_bit( filepath );
		int is_hdr    = stbi_is_hdr( filepath );

		// TODO accomodate for 16bit files - we can cross this bridge when we meet it.
		// Right now, we're assuming any file that is not 8bit or HDR will need to be
		// loaded as if it was encoded with 32bit floats.
		if ( components == 4 ) {
			if ( is_hdr || is_16_bit ) {
				self->image_inferred_format = le::Format::eR32G32B32A32Sfloat;
			} else {
				self->image_inferred_format = le::Format::eR8G8B8A8Unorm;
			}
		} else if ( components == 3 ) {
			if ( is_hdr || is_16_bit ) {
				self->image_inferred_format = le::Format::eR32G32B32Sfloat;
			} else {
				self->image_inferred_format = le::Format::eR8G8B8Unorm;
			}
		} else if ( components == 1 ) {
			if ( is_hdr || is_16_bit ) {
				self->image_inferred_format = le::Format::eR32Sfloat;
			} else {
				self->image_inferred_format = le::Format::eR8Unorm;
			}
		}
		logger.info( "Created image decoder for file '%s'", filepath );

		return self;
	} else {
		delete self;
		logger.error( "No filepath given for image decoder" );
		return nullptr;
	}
};

// ----------------------------------------------------------------------

static void le_image_decoder_destroy_image_decoder( le_image_decoder_o* self ) {
	delete self;
	logger.info( "Destroyed pixels image decoder" );
};

// ----------------------------------------------------------------------

static void le_image_decoder_get_image_data_description( le_image_decoder_o* self, le_image_decoder_format_o* p_format, uint32_t* w, uint32_t* h ) {
	if ( p_format ) {
		p_format->format = ( self->image_requested_format != le::Format::eUndefined ) ? self->image_requested_format : self->image_inferred_format;
	}
	if ( w ) {
		*w = self->image_width;
	}
	if ( h ) {
		*h = self->image_height;
	}
};

// ----------------------------------------------------------------------
// Read out pixels from file into given array of bytes.
// Uses pixel format and w,h, to figure out the size of p_pixels
static bool le_image_decoder_read_pixels( le_image_decoder_o* self, uint8_t* pixels, size_t pixels_byte_count ) {

	// TODO: read actual pixels
	auto format = ( self->image_requested_format != le::Format::eUndefined ) ? self->image_requested_format : self->image_inferred_format;

	int32_t     num_channels;
	le_num_type pixel_data_type;
	infer_data_info_from_le_format( format, &num_channels, &pixel_data_type );

	uint32_t num_bytes_per_channel = ( uint32_t( 1 ) << ( uint32_t( pixel_data_type ) & 0x3 ) );
	size_t   num_bytes             = num_bytes_per_channel * num_channels * ( self->image_width * self->image_height );

	if ( pixels_byte_count > num_bytes ) {
		logger.error( "Number of requested bytes is too great. Requested: %d != Is: %d\nNo pixels copied.", pixels_byte_count, num_bytes );
		return false;
	}
	// ----------| invariant: pixel count matches

	void* pixel_data = nullptr;
	if ( pixel_data_type == le_num_type::eU8 || pixel_data_type == le_num_type::eI8 ) {
		pixel_data = stbi_load( self->image_path.c_str(), &self->image_width, &self->image_height, &num_channels, num_channels );
	} else if ( pixel_data_type == le_num_type::eU16 || pixel_data_type == le_num_type::eI16 ) {
		pixel_data = stbi_load_16( self->image_path.c_str(), &self->image_width, &self->image_height, &num_channels, num_channels );
	} else if ( pixel_data_type == le_num_type::eF32 ) {
		pixel_data = stbi_loadf( self->image_path.c_str(), &self->image_width, &self->image_height, &num_channels, num_channels );
	}

	if ( pixel_data ) {
		memcpy( pixels, pixel_data, pixels_byte_count );
		stbi_image_free( pixel_data );
		return true;
	} else {
		logger.error( "Could not load image '%s'", self->image_path.c_str() );
		return false;
	}
};

// ----------------------------------------------------------------------

static void le_image_decoder_set_requested_format( le_image_decoder_o* self, le_image_decoder_format_o const* format ) {
	self->image_requested_format = format->format;
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_pixels, api ) {

	auto& le_image_decoder_i = static_cast<le_pixels_api*>( api )->le_pixels_image_decoder_i;

	if ( le_image_decoder_i == nullptr ) {
		le_image_decoder_i = new le_image_decoder_interface_t{};
	} else {
		// Interface already existed - we have been reloaded and only just need to update
		// function pointer addresses
		*le_image_decoder_i = le_image_decoder_interface_t();
	}

	le_image_decoder_i->create_image_decoder       = le_image_decoder_create_image_decoder;
	le_image_decoder_i->destroy_image_decoder      = le_image_decoder_destroy_image_decoder;
	le_image_decoder_i->read_pixels                = le_image_decoder_read_pixels;
	le_image_decoder_i->get_image_data_description = le_image_decoder_get_image_data_description;
	le_image_decoder_i->set_requested_format       = le_image_decoder_set_requested_format;
}

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

	int num_channels_in_file;

	le::Format image_inferred_format;
	le::Format image_requested_format = le::Format::eUndefined; // requested format wins over inferred format

	le::Format get_format() {
		return ( image_requested_format != le::Format::eUndefined ) ? image_requested_format : image_inferred_format;
	}
};

// ----------------------------------------------------------------------
// load image file, and poke at file info; does not load file into memory
static le_image_decoder_o* le_image_decoder_create_image_decoder( char const* filepath ) {
	static auto logger = LeLog( "le_pixels" );

	auto self = new le_image_decoder_o{};

	if ( filepath ) {

		int width;
		int height;
		int result;

		result = stbi_info( filepath, &width, &height, &self->num_channels_in_file );

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
		//
		// It makes currently no sense to have 3-channels images -
		// this means that 3-channel files will be decoded into 4-channel images.
		if ( self->num_channels_in_file == 4 || self->num_channels_in_file == 3 ) {
			if ( is_hdr || is_16_bit ) {
				self->image_inferred_format = le::Format::eR32G32B32A32Sfloat;
			} else {
				self->image_inferred_format = le::Format::eR8G8B8A8Unorm;
			}
		} else if ( self->num_channels_in_file == 1 ) {
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
	static auto logger = LeLog( "le_pixels" );
	delete self;
	logger.info( "Destroyed pixels image decoder" );
};

// ----------------------------------------------------------------------

static void le_image_decoder_get_image_data_description( le_image_decoder_o* self, le_image_decoder_format_o* p_format, uint32_t* w, uint32_t* h ) {
	if ( p_format ) {
		p_format->format = self->get_format();
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
	static auto logger = LeLog( "le_pixels" );

	// TODO: read actual pixels
	auto format = self->get_format();

	uint32_t    num_channels;
	le_num_type pixel_data_type;
	le_format_infer_channels_and_num_type( format, &num_channels, &pixel_data_type );

	size_t num_bytes = 0;
	le_format_get_image_data_size( format, &num_bytes, self->image_width, self->image_height, self->image_depth );

	if ( pixels_byte_count < num_bytes ) {
		logger.error( "Not enough space to read image data into. Available: %d != Required: %d\nNo pixels copied.", pixels_byte_count, num_bytes );
		return false;
	}

	// ----------| invariant: pixel count matches

	void* pixel_data = nullptr;

	if ( pixel_data_type == le_num_type::eU8 || pixel_data_type == le_num_type::eI8 ) {
		pixel_data = stbi_load( self->image_path.c_str(), &self->image_width, &self->image_height, &self->num_channels_in_file, num_channels );
	} else if ( pixel_data_type == le_num_type::eU16 || pixel_data_type == le_num_type::eI16 || pixel_data_type == le_num_type::eF16 ) {
		pixel_data = stbi_load_16( self->image_path.c_str(), &self->image_width, &self->image_height, &self->num_channels_in_file, num_channels );
	} else if ( pixel_data_type == le_num_type::eF32 ) {
		pixel_data = stbi_loadf( self->image_path.c_str(), &self->image_width, &self->image_height, &self->num_channels_in_file, num_channels );
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

#include "le_pixels.h"
#include "le_log.h"
#include "le_core.h"
#include "3rdparty/stb_image.h"
#include "assert.h"
#include <iostream>
#include <iomanip>
#include <cstring> // for memcpy

#include "private/le_renderer/le_vk_enums.inl"
#include "shared/interfaces/le_image_decoder_interface.h"

struct le_image_decoder_format_o {
	le::Format format;
};

struct le_pixels_o {
	// members
	void*          image_data = nullptr;
	le_pixels_info info{};
};

struct le_image_decoder_o {
	// this is one way of defining an image decoder
	le_pixels_o* pixels;
};
// ----------------------------------------------------------------------

struct image_source_info_t {

	enum class Type : uint32_t {
		eUndefined = 0,
		eBuffer    = 1,
		eFile      = 2,
	};

	union {
		struct {
			unsigned char const* buffer;
			size_t               buffer_num_bytes;
		} as_buffer;
		struct {
			char const* file_path;
		} as_file;
	} data;

	Type                 type;
	int                  requested_num_channels; // number of channels requested, 0 meaning don't change number of channels on load
	le_pixels_info::Type requested_pixel_type;   // requested pixel component type.
};

// ----------------------------------------------------------------------

static void le_pixels_destroy( le_pixels_o* self ) {

	if ( self && self->image_data ) {
		stbi_image_free( self->image_data );
		self->image_data = nullptr;
	}

	delete self;
}

// ----------------------------------------------------------------------

static inline uint32_t get_num_bytes_for_type( le_pixels_info::Type const& type ) {
	return ( 1 << ( type & 0b11 ) );
}

// ----------------------------------------------------------------------

static le_pixels_o* le_pixels_create( image_source_info_t const& info ) {
	auto self = new le_pixels_o{};

	int width;
	int height;
	int num_channels;
	int num_channels_in_file;

	if ( info.type == image_source_info_t::Type::eBuffer ) {

		switch ( info.requested_pixel_type ) {
		case le_pixels_info::Type::eUInt8:
			self->image_data = stbi_load_from_memory( info.data.as_buffer.buffer, info.data.as_buffer.buffer_num_bytes, &width, &height, &num_channels_in_file, info.requested_num_channels );
			break;
		case le_pixels_info::Type::eUInt16:
			self->image_data = stbi_load_16_from_memory( info.data.as_buffer.buffer, info.data.as_buffer.buffer_num_bytes, &width, &height, &num_channels_in_file, info.requested_num_channels );
			break;
		case le_pixels_info::Type::eFloat16: // deliberate fall-through
		case le_pixels_info::Type::eFloat32:
			self->image_data = stbi_loadf_from_memory( info.data.as_buffer.buffer, info.data.as_buffer.buffer_num_bytes, &width, &height, &num_channels_in_file, info.requested_num_channels );
			break;
		}

	} else if ( info.type == image_source_info_t::Type::eFile ) {

		switch ( info.requested_pixel_type ) {
		case le_pixels_info::Type::eUInt8:
			self->image_data = stbi_load( info.data.as_file.file_path, &width, &height, &num_channels_in_file, info.requested_num_channels );
			break;
		case le_pixels_info::Type::eUInt16:
			self->image_data = stbi_load_16( info.data.as_file.file_path, &width, &height, &num_channels_in_file, info.requested_num_channels );
			break;
		case le_pixels_info::Type::eFloat16: // deliberate fall-through
		case le_pixels_info::Type::eFloat32:
			self->image_data = stbi_loadf( info.data.as_file.file_path, &width, &height, &num_channels_in_file, info.requested_num_channels );
			break;
		}

	} else {
		assert( false ); // unreachable
	}

	if ( info.requested_num_channels == 0 ) {
		num_channels = num_channels_in_file;
	} else {
		num_channels = info.requested_num_channels;
	}

	static auto logger = LeLog( "le_pixels" );

	if ( !self->image_data ) {

		if ( info.type == image_source_info_t::Type::eFile ) {
			logger.warn( "Could not load image from file: '%s'", info.data.as_file.file_path );
		} else {
			logger.error( "Could not load image from buffer at address: %p", info.data.as_buffer.buffer );
		}

		// If we didn't manage to load an image, this object is invalid,
		// we must therefore free all memory which we had set aside for it
		// and return a null pointer.
		le_pixels_destroy( self );
		return nullptr;
	}

	assert( self->image_data );

	// ----------| invariant: load was successful

	self->info.bpp          = 8 * get_num_bytes_for_type( info.requested_pixel_type ) * uint32_t( num_channels ); // note * 8, since we're returning *bits* per pixel!
	self->info.width        = uint32_t( width );
	self->info.height       = uint32_t( height );
	self->info.depth        = 1;
	self->info.num_channels = uint32_t( num_channels );
	self->info.byte_count   = ( self->info.bpp / 8 ) * ( self->info.width * self->info.height * self->info.depth );

	return self;
}

// ----------------------------------------------------------------------

static le_pixels_o* le_pixels_create_from_file( char const* file_path, int num_channels_requested = 0, le_pixels_info::Type type = le_pixels_info::Type::eUInt8 ) {

	image_source_info_t info{};

	info.type                   = image_source_info_t::Type::eFile;
	info.data.as_file.file_path = file_path;
	info.requested_pixel_type   = type;
	info.requested_num_channels = num_channels_requested;

	return le_pixels_create( info );
}

// ----------------------------------------------------------------------

static le_pixels_o* le_pixels_create_from_memory( unsigned char const* buffer, size_t buffer_byte_count, int num_channels_requested = 0, le_pixels_info::Type type = le_pixels_info::Type::eUInt8 ) {

	image_source_info_t info{};

	info.type                            = image_source_info_t::Type::eBuffer;
	info.data.as_buffer.buffer           = buffer;
	info.data.as_buffer.buffer_num_bytes = buffer_byte_count;
	info.requested_pixel_type            = type;
	info.requested_num_channels          = num_channels_requested;

	return le_pixels_create( info );
}

// ----------------------------------------------------------------------

static le_pixels_info le_pixels_get_info( le_pixels_o* self ) {
	return self->info;
}

// ----------------------------------------------------------------------

static void* le_pixels_get_data( le_pixels_o* self ) {
	return self->image_data;
}

// ----------------------------------------------------------------------

static bool le_pixels_get_info_from_source( image_source_info_t const& source, le_pixels_info* info ) {

	if ( info == nullptr ) {
		return false;
	}

	if ( source.type == image_source_info_t::Type::eBuffer ) {
		if ( source.data.as_buffer.buffer == nullptr ||
		     source.data.as_buffer.buffer_num_bytes == 0 ) {
			return false;
		}
	} else if ( source.type == image_source_info_t::Type::eFile ) {
		if ( source.data.as_file.file_path == nullptr ) {
			return false;
		}
	} else {
		assert( false );
	}

	int width;
	int height;
	int components;
	int result;

	if ( source.type == image_source_info_t::Type::eBuffer ) {
		result = stbi_info_from_memory(
		    source.data.as_buffer.buffer,
		    int( source.data.as_buffer.buffer_num_bytes ),
		    &width,
		    &height,
		    &components );
	} else {
		result = stbi_info(
		    source.data.as_file.file_path,
		    &width,
		    &height,
		    &components );
	}

	if ( result != 1 ) {
		return false;
	}

	info->width        = uint32_t( width );
	info->height       = uint32_t( height );
	info->depth        = 1;
	info->num_channels = uint32_t( components );

	int is_16_bit;

	if ( source.type == image_source_info_t::Type::eBuffer ) {
		is_16_bit = stbi_is_16_bit_from_memory(
		    source.data.as_buffer.buffer,
		    int( source.data.as_buffer.buffer_num_bytes ) );
	} else {
		is_16_bit = stbi_is_16_bit( source.data.as_file.file_path );
	}

	if ( is_16_bit == 1 ) {
		info->type = le_pixels_info::Type::eUInt16;
	} else {
		// default type is 8 bit per pixel
		info->type = le_pixels_info::Type::eUInt8;
	}

	info->bpp        = 8 * get_num_bytes_for_type( info->type ) * uint32_t( info->num_channels ); // note * 8, since we're returning bits per pixels!
	info->byte_count = ( info->bpp / 8 ) * ( info->width * info->height * info->depth );

	return true;
}

// ----------------------------------------------------------------------

static bool le_pixels_get_info_from_file( char const* file_path, le_pixels_info* info ) {
	image_source_info_t source{};
	source.type                   = image_source_info_t::Type::eFile;
	source.data.as_file.file_path = file_path;
	return le_pixels_get_info_from_source( source, info );
}

// ----------------------------------------------------------------------

static bool le_pixels_get_info_from_memory( unsigned char const* buffer, size_t buffer_byte_count, le_pixels_info* info ) {
	image_source_info_t source{};
	source.type                            = image_source_info_t::Type::eBuffer;
	source.data.as_buffer.buffer           = buffer;
	source.data.as_buffer.buffer_num_bytes = buffer_byte_count;
	return le_pixels_get_info_from_source( source, info );
}

// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// load image file, and store pixels internally
static le_image_decoder_o* le_image_decoder_create_image_decoder( char const* filepath ) {

	auto self = new le_image_decoder_o{};

	le::Log( "le_pixels" ).info( "Created image decoder for file '%s'", filepath );

	if ( filepath ) {

		// Note that this will resample an image if it came to have more bits per pixel.

		if ( stbi_is_hdr( filepath ) ) {
			self->pixels = le_pixels_create_from_file( filepath, 4, le_pixels_info::eFloat32 );
		} else {

			self->pixels = le_pixels_create_from_file( filepath, 4, le_pixels_info::eUInt8 );
		};

		if ( nullptr == self->pixels ) {
			delete self;
			le::Log( "le_pixels" ).error( "Could not create le_pixels for %s", filepath );
			return nullptr;
		}

		return self;
	} else {
		delete self;
		le::Log( "le_pixels" ).error( "No filepath given for image decoder" );
		return nullptr;
	}
};
// ----------------------------------------------------------------------

static void le_image_decoder_destroy_image_decoder( le_image_decoder_o* image_decoder_o ) {

	if ( image_decoder_o ) {
		le_pixels_destroy( image_decoder_o->pixels );
		image_decoder_o->pixels = nullptr;
	}
	delete image_decoder_o;

	le::Log( "le_pixels" ).info( "Destroyed pixels image decoder" );
};

// ----------------------------------------------------------------------

static void le_image_decoder_get_image_data_description( le_image_decoder_o* self, le_image_decoder_format_o* p_format, uint32_t* w, uint32_t* h ) {
	if ( p_format ) {

		switch ( self->pixels->info.type ) {
		case ( le_pixels_info::eFloat32 ):
			p_format->format = le::Format::eR32G32B32A32Sfloat;
			break;
		case ( le_pixels_info::eUInt8 ):
			p_format->format = le::Format::eR8G8B8A8Unorm;
			break;
		default:
			p_format->format = le::Format::eUndefined;
			le::Log( "le_pixels" ).info( "Cound not figure out pixel format" );
		}
	}
	if ( w ) {
		*w = self->pixels->info.width;
	}
	if ( h ) {
		*h = self->pixels->info.height;
	}
};

// ----------------------------------------------------------------------
// read out pixels into given array of bytes.
// uses pixel format and w,h, to figure out the size of p_pixels
bool le_image_decoder_read_pixels( le_image_decoder_o* self, uint8_t* pixels, size_t pixels_byte_count ) {

	if ( pixels_byte_count != self->pixels->info.byte_count ) {
		le::Log( "le_pixels" ).error( "Byte count for pixels does not match. Requested: %d != Is: %d\n No pixels copied.", pixels_byte_count, self->pixels->info.byte_count );
		return false;
	}

	// ----------| invariant: pixel count matches

	memcpy( pixels, ( char* )self->pixels->image_data, pixels_byte_count );

	return true;
};

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
}

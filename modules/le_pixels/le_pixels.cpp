#include "le_pixels.h"
#include "pal_api_loader/ApiRegistry.hpp"
#include "3rdparty/stb_image.h"
#include "assert.h"
#include <iostream>
#include <iomanip>

struct le_pixels_o {
	// members
	void *         image_data = nullptr;
	le_pixels_info info{};
};

// ----------------------------------------------------------------------

static void le_pixels_destroy( le_pixels_o *self ) {

	if ( self->image_data ) {
		stbi_image_free( self->image_data );
		self->image_data = nullptr;
	}

	delete self;
}

// ----------------------------------------------------------------------

static inline uint32_t get_num_bytes_for_type( le_pixels_info::TYPE const &type ) {
	return ( 1 << ( type & 0b11 ) );
}

// ----------------------------------------------------------------------

static le_pixels_o *le_pixels_create( char const *file_path, int num_channels_requested = 0, le_pixels_info::TYPE type = le_pixels_info::TYPE::eUInt8 ) {
	auto self = new le_pixels_o{};

	int width;
	int height;
	int num_channels = 0;
	int num_channels_in_file;

	switch ( type ) {
	case le_pixels_info::TYPE::eUInt8:
		self->image_data = stbi_load( file_path, &width, &height, &num_channels_in_file, num_channels_requested );
		break;
	case le_pixels_info::TYPE::eUInt16:
		self->image_data = stbi_load_16( file_path, &width, &height, &num_channels_in_file, num_channels_requested );
		break;
	case le_pixels_info::TYPE::eFloat32:
		self->image_data = stbi_loadf( file_path, &width, &height, &num_channels_in_file, num_channels_requested );
		break;
	}

	if ( num_channels_requested == 0 ) {
		num_channels = num_channels_in_file;
	} else {
		num_channels = num_channels_requested;
	}

	if ( !self->image_data ) {

		// If we didn't manage to load an image, this object is invalid,
		// we must therefore free all memory which we had set aside for it
		// and return a null pointer.
		std::cerr << "ERROR: Could not load image: " << file_path << std::endl
				  << std::flush;
		le_pixels_destroy( self );
		return nullptr;
	}

	assert( self->image_data );

	// ----------| invariant: load was successful

	self->info.bpp          = 8 * get_num_bytes_for_type( type ) * uint32_t( num_channels ); // note * 8, since we're returning bytes per pixels!
	self->info.width        = uint32_t( width );
	self->info.height       = uint32_t( height );
	self->info.depth        = 1;
	self->info.num_channels = uint32_t( num_channels );
	self->info.byte_count   = ( self->info.bpp / 8 ) * ( self->info.width * self->info.height * self->info.depth );

	return self;
}

// ----------------------------------------------------------------------

static le_pixels_info le_pixels_get_info( le_pixels_o *self ) {
	return self->info;
}

// ----------------------------------------------------------------------

static void *le_pixels_get_data( le_pixels_o *self ) {
	return self->image_data;
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_pixels_api( void *api ) {
	auto &le_pixels_i = static_cast<le_pixels_api *>( api )->le_pixels_i;

	le_pixels_i.create   = le_pixels_create;
	le_pixels_i.destroy  = le_pixels_destroy;
	le_pixels_i.get_data = le_pixels_get_data;
	le_pixels_i.get_info = le_pixels_get_info;
}

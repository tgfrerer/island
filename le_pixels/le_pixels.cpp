#include "le_pixels.h"
#include "pal_api_loader/ApiRegistry.hpp"
#include "3rdparty/stb_image.h"
#include "assert.h"

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

static le_pixels_o *le_pixels_create( char const *file_path, int num_channels_requested = 0 ) {
	auto self = new le_pixels_o{};

	int width;
	int height;
	int num_channels = 0;
	int num_channels_in_file;

	self->image_data = stbi_load( file_path, &width, &height, &num_channels_in_file, num_channels_requested );

	if ( num_channels_requested == 0 ) {
		num_channels = num_channels_in_file;
	} else {
		num_channels = num_channels_requested;
	}

	assert( self->image_data );

	if ( !self->image_data ) {

		// If we didn't manage to load an image, this object is invalid,
		// we must therefore free all memory which we had set aside for it
		// and return a null pointer.

		le_pixels_destroy( self );
		return nullptr;
	}

	// ----------| invariant: load was successful

	self->info.bpp          = 8 * uint32_t( num_channels ); // must be, since we didnt load the file using 16 or float
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

#pragma once

/*

  Forward declaration for the abstract interface that any image encoders
  must implement.

  That way, clients of image encoders can stick to this abstract interface
  when using any client encoders.

  //

  For image encoders there are a lot of format-specific settings that need
  to be set - as in how should the image be encoded, the encoding quality,
  the number of channels to use for encoding etc.

  We need to provide the encoder with a method to exchange these settings
  with whoever uses this api.


  Maybe we should pass image file encoding parameters as structured data
  (json/jsmn!?) so that we can keep the interface versatile?

*/

#include <cstddef>
#include <stdint.h>

struct le_image_encoder_o;

struct le_image_encoder_format_o; // struct wrapper around le::Format

// ----------------------------------------------------------------------
// clang-format off

struct le_image_encoder_interface_t {

	// This gets re-set automatically on api reload - because of
	// `new le_image_encoder_interface_t{}`
	uint64_t ( *get_api_version )() = []() -> uint64_t {
		static constexpr uint64_t API_VERSION = 0ull << 48 | 0ull << 32 | 1ull << 16 | 0ull << 0;
		return API_VERSION;
	};

	le_image_encoder_o* ( *create_image_encoder )( char const* file_name, uint32_t width, uint32_t height );

	void ( *destroy_image_encoder      )( le_image_encoder_o* image_encoder_o );
	void ( *set_encode_parameters ) (le_image_encoder_o* image_encoder_o, void * params);


	uint64_t (*get_encoder_version)(le_image_encoder_o* encoder);
	// load image data from file, and read it into a pre-allocated byte array at p_pixels
	bool ( *write_pixels                )( le_image_encoder_o* image_encoder_o, uint8_t const * p_pixel_data, size_t pixel_data_byte_count, le_image_encoder_format_o* pixel_data_format );


	// TODO: we need a way to allow the user of this api to choose the parameters for encoding the image
};

// clang-format on

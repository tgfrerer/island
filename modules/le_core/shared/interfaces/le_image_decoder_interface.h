#pragma once

/*

  Forward declaration for the abstract interface that any image decoders
  must implement.

  That way, clients of image decoders can stick to this abstract interface
  when using any client decoders.

*/

#include <cstddef>
#include <stdint.h>

struct le_image_decoder_o;

struct le_image_decoder_format_o; // struct wrapper around le::Format

// ----------------------------------------------------------------------

struct le_image_decoder_interface_t {

	// This gets re-set automatically on api reload - because of
	// `new le_image_decoder_interface_t{}`
	uint64_t ( *get_api_version )() = []() -> uint64_t {
		static constexpr uint64_t API_VERSION = 0ull << 48 | 0ull << 32 | 1ull << 16 | 0ull << 0;
		return API_VERSION;
	}; // static method - we should provide this so that we can make sure that implementers use compatible apis

	le_image_decoder_o* ( *create_image_decoder )( char const* file_name );
	void ( *destroy_image_decoder )( le_image_decoder_o* image_decoder_o );

	// load image data from file, and read it into a pre-allocated byte array at p_pixels
	bool ( *read_pixels )( le_image_decoder_o* image_decoder_o, uint8_t* p_pixels, size_t pixels_byte_count );

	void ( *get_image_data_description )( le_image_decoder_o* image_decoder_o, le_image_decoder_format_o* p_format, uint32_t* w, uint32_t* h );
};

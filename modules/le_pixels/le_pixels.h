#ifndef GUARD_le_pixels_H
#define GUARD_le_pixels_H

#include "le_core.h"

// struct le_pixels_o;

struct le_pixels_info {
	// Note that we store the log2 of the number of Bytes needed to store values of a type
	// in the least significant two bits, so that we can say: numBytes =  1 << (type & 0x03);
	enum Type : uint32_t {
		eUInt8   = ( 0 << 2 ) | 0,
		eUInt16  = ( 1 << 2 ) | 1,
		eFloat16 = ( 2 << 2 ) | 1, // 16 bit float type
		eFloat32 = ( 3 << 2 ) | 2, // 32 bit float type
	};
	uint32_t width;        //
	uint32_t height;       //
	uint32_t depth;        // 1 by default
	uint32_t bpp;          // bits per pixel
	uint32_t num_channels; // number of channels
	uint32_t byte_count;   // total number of bytes
	Type     type;
};
// generic image decoder interface

struct le_image_decoder_interface_t;

// clang-format off
struct le_pixels_api {

	struct le_pixels_interface_t {

// 		bool (* get_info_from_memory ) ( unsigned char const * buffer, size_t buffer_byte_count, le_pixels_info * info);
// 		bool (* get_info_from_file   ) ( char const * file_name, le_pixels_info * info);
//
// 		le_pixels_o *    ( * create_from_memory )( unsigned char const * buffer, size_t buffer_byte_count, int num_channels_requested, le_pixels_info::Type type);
// 		le_pixels_o *    ( * create   ) ( char const * file_path, int num_channels_requested, le_pixels_info::Type type);
// 		void             ( * destroy  ) ( le_pixels_o* self );
//
// 		le_pixels_info   ( * get_info ) ( le_pixels_o* self );
// 		void *           ( * get_data ) ( le_pixels_o* self );
	};

	le_pixels_interface_t       le_pixels_i;
	le_image_decoder_interface_t * le_pixels_image_decoder_i = nullptr; // abstract image decoder interface -- this is an alternative interface and can be used to interact with pixels in a generic way
};
// clang-format on
LE_MODULE( le_pixels );
LE_MODULE_LOAD_DEFAULT( le_pixels );

#ifdef __cplusplus

namespace le_pixels {
static const auto& api         = le_pixels_api_i;
static const auto& le_pixels_i = api->le_pixels_i;

} // namespace le_pixels

namespace le {

// class Pixels : NoCopy, NoMove {
//
// 	le_pixels_o* self;
//
//   public:
// 	Pixels( char const* path, int const& numChannelsRequested = 0, le_pixels_info::Type const& type = le_pixels_info::eUInt8 )
// 	    : self( le_pixels::le_pixels_i.create( path, numChannelsRequested, type ) ) {
// 	}
//
// 	~Pixels() {
// 		le_pixels::le_pixels_i.destroy( self );
// 	}
//
// 	auto getData() noexcept {
// 		return le_pixels::le_pixels_i.get_data( self );
// 	}
//
// 	auto getInfo() noexcept {
// 		return le_pixels::le_pixels_i.get_info( self );
// 	}
//
// 	bool isValid() noexcept {
// 		return ( self );
// 	}
// };
} // end namespace le

#endif // __cplusplus

#endif

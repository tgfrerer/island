#ifndef GUARD_le_pixels_H
#define GUARD_le_pixels_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#ifdef __cplusplus
extern "C" {
#endif

struct le_pixels_o;

void register_le_pixels_api( void *api );

struct le_pixels_info {
	// Note that we store the log2 of the number of Bytes needed to store values of a type
	// in the least significant two bits, so that we can say: numBytes =  1 << (type & 0x03);
	enum TYPE : int {
		eUInt8   = ( 0 << 2 ) | 0,
		eUInt16  = ( 1 << 2 ) | 1,
		eFloat32 = ( 2 << 2 ) | 2, // 32 bit float type
	};
	uint32_t width;        //
	uint32_t height;       //
	uint32_t depth;        // 1 by default
	uint32_t bpp;          // bits per pixel
	uint32_t num_channels; // number of channels
	uint32_t byte_count;   // total number of bytes
	TYPE     type;
};

// clang-format off
struct le_pixels_api {
	static constexpr auto id      = "le_pixels";
	static constexpr auto pRegFun = register_le_pixels_api;

	struct le_pixels_interface_t {

		le_pixels_o *    ( * create   ) ( char const * path, int num_channels_requested, le_pixels_info::TYPE type);
		void             ( * destroy  ) ( le_pixels_o* self );

		le_pixels_info   ( * get_info ) ( le_pixels_o* self );
		void *           ( * get_data ) ( le_pixels_o* self );
	};

	le_pixels_interface_t       le_pixels_i;
};
// clang-format on

#ifdef __cplusplus
} // extern c

namespace le_pixels {
#	ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<le_pixels_api>( true );
#	else
const auto api = Registry::addApiStatic<le_pixels_api>();
#	endif

static const auto &le_pixels_i = api -> le_pixels_i;

} // namespace le_pixels

class LePixels : NoCopy, NoMove {

	le_pixels_o *self;

  public:
	LePixels( char const *path, int const &numChannelsRequested = 0, le_pixels_info::TYPE const &type = le_pixels_info::eUInt8 )
	    : self( le_pixels::le_pixels_i.create( path, numChannelsRequested, type ) ) {
	}

	~LePixels() {
		le_pixels::le_pixels_i.destroy( self );
	}

	auto getData() noexcept {
		return le_pixels::le_pixels_i.get_data( self );
	}

	auto getInfo() noexcept {
		return le_pixels::le_pixels_i.get_info( self );
	}
};

#endif // __cplusplus

#endif

#ifndef GUARD_le_pixels_H
#define GUARD_le_pixels_H

#include "le_core.h"

// This interface is rarely used direcly. You are probably better off using
// `le_resource_manager`.
//
// If you really want to use this interface directly, then you must include
// `shared/interfaces/le_image_decoder_interface.h`, which declares the abstract
// interface that all image decoders (such as this one) promise to implement:
//
// #include "shared/interfaces/le_image_decoder_interface.h"

// Generic image decoder interface - the .cpp file will import the definition
// via `shared/intefaces/le_image_decoder_inferface.h`

struct le_image_decoder_interface_t;

// clang-format off
struct le_pixels_api {
	le_image_decoder_interface_t * le_pixels_image_decoder_i = nullptr; // abstract image decoder interface -- this is an alternative interface and can be used to interact with pixels in a generic way
};
// clang-format on
LE_MODULE( le_pixels );
LE_MODULE_LOAD_DEFAULT( le_pixels );

#ifdef __cplusplus

namespace le_pixels {
static const auto& api = le_pixels_api_i;
} // namespace le_pixels

#endif // __cplusplus

#endif

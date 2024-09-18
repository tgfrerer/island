#pragma once

// this file assumes that renderer types have been included already
#include "le_swapchain_vk.h"
// #include "private/le_renderer/le_renderer_types.h"

struct le_swapchain_img_settings_t {
	le_swapchain_settings_t base = {
	    .type            = le_swapchain_settings_t::Type::LE_IMG_SWAPCHAIN,
	    .imagecount_hint = 3,
	    .p_next          = nullptr,
	}; // we must do this so that we can fake inheritance
	uint32_t                             width_hint               = 640;
	uint32_t                             height_hint              = 480;
	le::Format                           format_hint              = le::Format::eB8G8R8A8Unorm; // preferred surface format
	struct le_image_encoder_interface_t* image_encoder_i          = nullptr;                    // ffdecl. declared in shared/interfaces/le_image_encoder_interface.h
	void*                                image_encoder_parameters = nullptr;                    // non-owning
	char const*                          image_filename_template  = nullptr;                    // a format string, must contain %d for current image number.
	char const*                          pipe_cmd                 = nullptr;                    // command used to save images - will receive stream of images via stdin
	size_t                               frame_number_offset      = 0;                          // optional; used to calculate output frame number name -- this offset is added to the running frame number number

	operator le_swapchain_settings_t*() {
		return &base;
	};
};

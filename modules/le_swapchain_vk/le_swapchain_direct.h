#pragma once

// this file assumes that renderer types have been included already
#include "le_swapchain_vk.h"
// #include "private/le_renderer/le_renderer_types.h"

struct le_swapchain_direct_settings_t {
	le_swapchain_settings_t base = {
	    .type            = le_swapchain_settings_t::Type::LE_DIRECT_SWAPCHAIN,
	    .imagecount_hint = 3,
	    .p_next          = nullptr,
	}; // we must do this so that we can fake inheritance

	enum class Presentmode : uint32_t {
		eImmediate = 0,
		eMailbox,
		eFifo,
		eDefault = eFifo,
		eFifoRelaxed,
		eSharedDemandRefresh,
		eSharedContinuousRefresh,
	};
	uint32_t width_hint  = 640;
	uint32_t height_hint = 480;

	le::Format  format_hint      = le::Format::eB8G8R8A8Unorm; // preferred surface format
	Presentmode presentmode_hint = Presentmode::eDefault;
	char const* display_name     = nullptr; // Will be matched against display name

	operator le_swapchain_settings_t*() {
		return &base;
	};
};

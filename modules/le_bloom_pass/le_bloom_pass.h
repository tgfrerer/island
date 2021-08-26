#ifndef GUARD_le_bloom_pass_H
#define GUARD_le_bloom_pass_H

#include <stdint.h>
#include "le_core.h"

struct le_render_module_o;
LE_OPAQUE_HANDLE( le_img_resource_handle );
struct le_renderer_o;

// clang-format off
struct le_bloom_pass_api {

	struct params_t {
		struct bloom_params_t {
			float strength{1};
			float radius{1};
		} bloom;
		struct luma_threshold_params_t {
			float defaultColor[ 3 ]{0.f, 0.f, 0.f}; // vec3(0)
			float defaultOpacity{0.7};              // 0
			float luminosityThreshold{0.75f};       // 1.f
			float smoothWidth{0.01f};               // 1.0
		} luma_threshold;
	};

	struct le_bloom_pass_interface_t {
		void (* le_render_module_add_bloom_pass)(le_render_module_o* module, le_img_resource_handle const & input, le_img_resource_handle const & output, uint32_t const & width, uint32_t const & height, params_t* params);
		void (* le_render_module_add_blit_pass)(le_render_module_o* module, le_img_resource_handle const & input, le_img_resource_handle const & output);	
	};

	le_bloom_pass_interface_t       le_bloom_pass_i;
};
// clang-format on

#ifdef __cplusplus

LE_MODULE( le_bloom_pass );
LE_MODULE_LOAD_DEFAULT( le_bloom_pass );

namespace le_bloom_pass {
static const auto &api             = le_bloom_pass_api_i;
static const auto &le_bloom_pass_i = api -> le_bloom_pass_i;
} // namespace le_bloom_pass

#endif // __cplusplus

#endif

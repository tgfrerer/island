#ifndef GUARD_le_bloom_pass_H
#define GUARD_le_bloom_pass_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#ifdef __cplusplus
extern "C" {
#endif

struct le_render_module_o;
struct le_resource_handle_t;
struct le_renderer_o;

void register_le_bloom_pass_api( void *api );

// clang-format off
struct le_bloom_pass_api {
	static constexpr auto id      = "le_bloom_pass";
	static constexpr auto pRegFun = register_le_bloom_pass_api;

	struct le_bloom_pass_interface_t {
		void (* le_render_module_add_bloom_pass)(le_render_module_o* module, le_resource_handle_t const & input, le_resource_handle_t const & output, uint32_t const & width, uint32_t const & height);
	};

	le_bloom_pass_interface_t       le_bloom_pass_i;
};
// clang-format on

#ifdef __cplusplus
} // extern c

namespace le_bloom_pass {
#	ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<le_bloom_pass_api>( true );
#	else
const auto api = Registry::addApiStatic<le_bloom_pass_api>();
#	endif

static const auto &le_bloom_pass_i = api -> le_bloom_pass_i;

} // namespace le_bloom_pass

#endif // __cplusplus

#endif

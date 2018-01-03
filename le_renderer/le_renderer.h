#ifndef GUARD_LE_RENDERER_H
#define GUARD_LE_RENDERER_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

// depends on le_backend_vk. le_backend_vk must be loaded before this class is used.

#ifdef __cplusplus
extern "C" {
#endif

void register_le_renderer_api( void *api );

struct le_backend_vk_device_o;
struct le_renderer_o;

struct le_renderer_api {
	static constexpr auto id       = "le_renderer";
	static constexpr auto pRegFun  = register_le_renderer_api;

	struct renderer_interface_t {
		le_renderer_o* ( *create  ) (le_backend_vk_device_o* device);
		void           ( *destroy ) (le_renderer_o* obj);
	};

	renderer_interface_t le_renderer_i;
};

#ifdef __cplusplus
} // extern "C"


namespace le {

class Renderer  {
	const le_renderer_api &                      rendererApiI = *Registry::getApi<le_renderer_api>();
	const le_renderer_api::renderer_interface_t &rendererI    = rendererApiI.le_renderer_i;

	le_renderer_o* self;

  public:
	Renderer( le_backend_vk_device_o* device )
	    : self( rendererI.create(device)) {
	}

	~Renderer() {
		rendererI.destroy(self);
	}


};

} // namespace le
#endif // __cplusplus
#endif // GUARD_LE_RENDERER_H

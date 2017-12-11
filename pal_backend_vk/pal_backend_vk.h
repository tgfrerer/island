#ifndef GUARD_PAL_BACKEND_VK_H
#define GUARD_PAL_BACKEND_VK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void register_pal_backend_vk_api( void *api );

struct pal_backend_vk_instance_o;
struct pal_backend_vk_api;

struct pal_backend_vk_api {
	static constexpr auto id       = "pal_backend_vk";
	static constexpr auto pRegFun  = register_pal_backend_vk_api;

	struct instance_interface_t {
		pal_backend_vk_instance_o * ( *create )           ( pal_backend_vk_api * );
		void                        ( *destroy )          ( pal_backend_vk_instance_o * );
		void                        ( *post_reload_hook ) ( pal_backend_vk_instance_o * );
	};

	instance_interface_t       instance_i;
	pal_backend_vk_instance_o *cUniqueInstance = nullptr;
};

#ifdef __cplusplus
} // extern "C"

#include "pal_api_loader/ApiRegistry.hpp"

namespace pal {
namespace vk {

class Instance {
	pal_backend_vk_api::instance_interface_t &mInstanceI;
	pal_backend_vk_instance_o *               self;

  public:
	Instance()
	    : mInstanceI( Registry::getApi<pal_backend_vk_api>()->instance_i )
	    , self( mInstanceI.create( Registry::getApi<pal_backend_vk_api>() ) ) {
	}

	~Instance() {
		mInstanceI.destroy( self );
	}
};

} // namespace vk
} // namespace pal
#endif // __cplusplus
#endif // GUARD_PAL_BACKEND_VK_H

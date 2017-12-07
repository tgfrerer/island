#ifndef GUARD_PAL_BACKEND_VK_H
#define GUARD_PAL_BACKEND_VK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void register_pal_backend_vk_api( void *api );

struct pal_backend_o;
struct pal_backend_vk_api;

struct pal_backend_vk_api {
	static constexpr auto id      = "pal_backend_vk";
	static constexpr auto pRegFun = register_pal_backend_vk_api;

	pal_backend_o *cBackend = nullptr;

	pal_backend_o * ( *create )          ( pal_backend_vk_api * );
	void            ( *destroy )         ( pal_backend_o * );

	void            ( *post_reload_hook )( pal_backend_o * );
};

#ifdef __cplusplus
} // extern "C"

#include "pal_api_loader/ApiRegistry.hpp"

namespace pal {

class Backend {
	pal_backend_vk_api &mBackend;
	pal_backend_o *     instance;

  public:
	Backend()
	    : mBackend( *Registry::getApi<pal_backend_vk_api>() )
	    , instance( mBackend.create(&mBackend) ) {
	}

	~Backend() {
		mBackend.destroy( instance );
	}
};
} // namespace pal
#endif // __cplusplus
#endif // GUARD_PAL_BACKEND_VK_H

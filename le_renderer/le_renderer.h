#ifndef GUARD_LE_RENDERER_H
#define GUARD_LE_RENDERER_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#ifdef __cplusplus
extern "C" {
#endif

void register_le_renderer_api( void *api );

struct le_renderer_o;

struct le_renderer_api {
	static constexpr auto id       = "le_renderer";
	static constexpr auto pRegFun  = register_le_renderer_api;

	struct renderer_interface_t {
		le_renderer_o* ( *create  ) ();
		void           ( *destroy ) (le_renderer_o* obj);
	};

	renderer_interface_t le_renderer_i;
};

#ifdef __cplusplus
} // extern "C"


namespace le {

//class Renderer  {
//	const le_renderer_api &                      backendApiI = *Registry::getApi<le_renderer_api>();
//	const le_renderer_api::renderer_interface_t &rendererI   = backendApiI.rendererI;
//	le_renderer_o *                              self        = ;

//  public:
//	Renderer(  )
//	    : mInstance( instanceI.create( &backendApiI, extensionsArray_, numExtensions_ ) )
//	    , mDevice( deviceI.create( mInstance ) ) {
//	}

//	~Renderer() {
//		deviceI.destroy( mDevice );
//		instanceI.destroy( mInstance );
//	}


//};

} // namespace le
#endif // __cplusplus
#endif // GUARD_LE_RENDERER_H

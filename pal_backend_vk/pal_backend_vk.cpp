#include "pal_backend_vk/pal_backend_vk.h"
#include <iostream>
#include <iomanip>

struct pal_backend_o;

extern pal_backend_o *create();                   // defined in instance_vk.cpp
extern void           destroy( pal_backend_o * ); // defined in instance_vk.cpp
extern void           update( pal_backend_o * );



// ----------------------------------------------------------------------

void register_pal_backend_vk_api( void *api_ ) {
	auto pal_backend_vk     = static_cast<pal_backend_vk_api *>( api_ );
	pal_backend_vk->create  = create;
	pal_backend_vk->destroy = destroy;
	pal_backend_vk->update  = update;
}

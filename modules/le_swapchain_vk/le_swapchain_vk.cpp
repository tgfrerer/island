#include "le_swapchain_vk.h"
#include "private/le_swapchain_vk/le_swapchain_vk_common.inl"
#include "private/le_renderer/le_renderer_types.h" // for swapchain_settings
#include "le_backend_vk.h"
#include "assert.h"
// ----------------------------------------------------------------------
#ifdef PLUGINS_DYNAMIC
#	define VOLK_IMPLEMENTATION
#endif
#include "util/volk/volk.h"

static void swapchain_reset( le_swapchain_o* self, const le_swapchain_settings_t* settings ) {
	self->vtable.reset( self, settings );
}

void post_reload_hook( le_backend_o* backend ) {
#ifdef PLUGINS_DYNAMIC
	if ( backend ) {
		VkResult result = volkInitialize();
		assert( result == VK_SUCCESS && "must successfully initialize the vulkan loader in case we're loading this module as a library" );
		auto       le_instance = le_backend_vk::private_backend_vk_i.get_instance( backend );
		VkInstance instance    = le_backend_vk::vk_instance_i.get_vk_instance( le_instance );
		volkLoadInstance( instance );
		auto device = le_backend_vk::private_backend_vk_i.get_vk_device( backend );
		volkLoadDevice( device );
	}
#endif
}

static void swapchain_inc_ref( le_swapchain_o* base ); // ffdecl.

// ----------------------------------------------------------------------

static le_swapchain_o* swapchain_create( le_swapchain_vk_api::swapchain_interface_t const& interface, le_backend_o* backend, const le_swapchain_settings_t* settings ) {

	post_reload_hook( backend );

#ifdef PLUGINS_DYNAMIC
	// store the pointer to our current backend.
	*le_core_produce_dictionary_entry( hash_64_fnv1a_const( "le_backend_o" ) ) = backend;
#endif

	auto obj = interface.create( interface, backend, settings );
	swapchain_inc_ref( obj );
	return obj;
}

// ----------------------------------------------------------------------

static bool swapchain_acquire_next_image( le_swapchain_o* self, VkSemaphore_T* semaphorePresentComplete_, uint32_t* imageIndex_ ) {
	return self->vtable.acquire_next_image( self, semaphorePresentComplete_, imageIndex_ );
}

// ----------------------------------------------------------------------

static VkImage_T* swapchain_get_image( le_swapchain_o* self, uint32_t index ) {
	return self->vtable.get_image( self, index );
}

// ----------------------------------------------------------------------

static VkSurfaceFormatKHR* swapchain_get_surface_format( le_swapchain_o* self ) {
	return self->vtable.get_surface_format( self );
}

// ----------------------------------------------------------------------

static uint32_t swapchain_get_image_width( le_swapchain_o* self ) {
	return self->vtable.get_image_width( self );
}

// ----------------------------------------------------------------------

static uint32_t swapchain_get_image_height( le_swapchain_o* self ) {
	return self->vtable.get_image_height( self );
}

// ----------------------------------------------------------------------

static size_t swapchain_get_swapchain_images_count( le_swapchain_o* self ) {
	return self->vtable.get_images_count( self );
}

// ----------------------------------------------------------------------

static void swapchain_destroy( le_swapchain_o* self ) {
	self->vtable.destroy( self );
}

// ----------------------------------------------------------------------

static bool swapchain_present( le_swapchain_o* self, VkQueue_T* queue, VkSemaphore_T* renderCompleteSemaphore, uint32_t* pImageIndex ) {
	return self->vtable.present( self, queue, renderCompleteSemaphore, pImageIndex );
};

// ----------------------------------------------------------------------

static inline le_swapchain_vk_api::swapchain_interface_t const* fetch_interface( le_swapchain_settings_t::Type const& type ) {
	le_swapchain_vk_api::swapchain_interface_t const* interface = nullptr;

	switch ( type ) {
	case le_swapchain_settings_t::LE_KHR_SWAPCHAIN:
		interface = &le_swapchain_vk::api->swapchain_khr_i;
		return interface;
	case le_swapchain_settings_t::LE_DIRECT_SWAPCHAIN:
		interface = &le_swapchain_vk::api->swapchain_direct_i;
		return interface;
	case le_swapchain_settings_t::LE_IMG_SWAPCHAIN:
		interface = &le_swapchain_vk::api->swapchain_img_i;
		return interface;
	}

	assert( false ); // unreachable
	return nullptr;
}

// ----------------------------------------------------------------------

static void swapchain_get_required_vk_instance_extensions( const le_swapchain_settings_t* settings ) {
	auto interface = fetch_interface( settings->type );
	interface->get_required_vk_instance_extensions( settings );
}

// ----------------------------------------------------------------------

static void swapchain_get_required_vk_device_extensions( const le_swapchain_settings_t* settings ) {
	auto interface = fetch_interface( settings->type );
	interface->get_required_vk_device_extensions( settings );
}

// ----------------------------------------------------------------------

static void swapchain_inc_ref( le_swapchain_o* base ) {
	++base->referenceCount;
}

static void swapchain_dec_ref( le_swapchain_o* base ) {
	if ( --base->referenceCount == 0 ) {
		swapchain_destroy( base );
	}
}
// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_swapchain_vk, api_ ) {
	auto  api         = static_cast<le_swapchain_vk_api*>( api_ );
	auto& swapchain_i = api->swapchain_i;

	swapchain_i.create                              = swapchain_create;
	swapchain_i.destroy                             = swapchain_destroy;
	swapchain_i.reset                               = swapchain_reset;
	swapchain_i.acquire_next_image                  = swapchain_acquire_next_image;
	swapchain_i.get_image                           = swapchain_get_image;
	swapchain_i.get_image_width                     = swapchain_get_image_width;
	swapchain_i.get_image_height                    = swapchain_get_image_height;
	swapchain_i.get_surface_format                  = swapchain_get_surface_format;
	swapchain_i.get_images_count                    = swapchain_get_swapchain_images_count;
	swapchain_i.present                             = swapchain_present;
	swapchain_i.get_required_vk_instance_extensions = swapchain_get_required_vk_instance_extensions;
	swapchain_i.get_required_vk_device_extensions   = swapchain_get_required_vk_device_extensions;

	auto& swapchain_ref_i   = api->swapchain_ref_i;
	swapchain_ref_i.dec_ref = swapchain_dec_ref;
	swapchain_ref_i.inc_ref = swapchain_inc_ref;

	register_le_swapchain_khr_api( api );
	register_le_swapchain_img_api( api );
	register_le_swapchain_direct_api( api );

#ifdef PLUGINS_DYNAMIC
	// in case we're running this as a dynamic module, we must patch all vulkan methods as soon as the module gets reloaded

	auto backend_o = *le_core_produce_dictionary_entry( hash_64_fnv1a_const( "le_backend_o" ) );
	post_reload_hook( static_cast<le_backend_o*>( backend_o ) );
#endif

	le_core_load_library_persistently( "libX11.so" );
}

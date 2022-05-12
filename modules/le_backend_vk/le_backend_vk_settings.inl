#include "le_backend_vk.h"
#include "le_log.h"
#include <atomic>
#include <set>
#include <vector>
#include <string>

struct le_backend_vk_settings_o {
	std::set<std::string> required_instance_extensions_set; // we use set to give us permanent addresses for char*, and to ensure uniqueness of requested extensions
	std::set<std::string> required_device_extensions_set;   // we use set to give us permanent addresses for char*, and to ensure uniqueness of requested extensions
	                                                        //
	// we keep the sets in sync with the following two vectors, which point into the set contents for their char const *
	//
	std::vector<char const*> required_instance_extensions;
	std::vector<char const*> required_device_extensions;

	std::vector<le_swapchain_settings_t> swapchain_settings;
	uint32_t                             concurrency_count = 1; // number of potential worker threads
	std::atomic_bool                     readonly          = false;
};

// ----------------------------------------------------------------------

static le_backend_vk_settings_o* le_backend_vk_settings_create() {
	return new le_backend_vk_settings_o{};
};

// ----------------------------------------------------------------------

static void le_backend_vk_settings_destroy( le_backend_vk_settings_o* self ) {
	delete self;
}

// ----------------------------------------------------------------------

static bool le_backend_vk_settings_add_required_instance_extension( char const* ext ) {
	le_backend_vk_settings_o* self = le_backend_vk::api->backend_settings_singleton;
	if ( self->readonly == false ) {
		auto result = self->required_instance_extensions_set.emplace( ext );
		if ( result.second ) {
			// only add to the vector if item was actually added. this is how we enforce that
			// elements in vector are unique.
			self->required_instance_extensions.push_back( result.first->c_str() );
		}
		return result.second;
	} else {
		static auto logger = LeLog( "le_backend_vk_settings" );
		logger.error( "Cannot add required instance extension '%s'", ext );
		return false;
	}
}

// ----------------------------------------------------------------------

static bool le_backend_vk_settings_add_required_device_extension( char const* ext ) {
	le_backend_vk_settings_o* self = le_backend_vk::api->backend_settings_singleton;
	if ( self->readonly == false ) {

		auto result = self->required_device_extensions_set.emplace( ext );
		if ( result.second ) {
			// only add to the vector if item was actually added. this is how we enforce that
			// elements in vector are unique.
			self->required_device_extensions.push_back( result.first->c_str() );
		}

		return result.second;
	} else {
		static auto logger = LeLog( "le_backend_vk_settings" );
		logger.error( "Cannot add required device extension '%s'", ext );
		return false;
	}
}

// ----------------------------------------------------------------------

static bool le_backend_vk_settings_add_swapchain_setting( le_swapchain_settings_t const* settings ) {
	le_backend_vk_settings_o* self = le_backend_vk::api->backend_settings_singleton;
	if ( self->readonly == false ) {
		self->swapchain_settings.emplace_back( *settings );
		return true;
	} else {
		static auto logger = LeLog( "le_backend_vk_settings" );
		logger.error( "Cannot add Swapchain setting" );
		return false;
	}
}

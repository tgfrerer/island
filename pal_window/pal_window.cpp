#include "pal_window/pal_window.h"
#include "assert.h"

#include <iostream>

#include "le_backend_vk/le_backend_vk.h"

#define GLFW_INCLUDE_VULKAN
#include "GLFW/glfw3.h"

struct pal_window_o {
	GLFWwindow * window;
	VkSurfaceKHR mSurface;
};

// ----------------------------------------------------------------------

static pal_window_o *window_create() {
	auto obj = new pal_window_o();
	glfwWindowHint( GLFW_CLIENT_API, GLFW_NO_API );
	obj->window = glfwCreateWindow( 200, 200, "hello world", nullptr, nullptr );
	return obj;
}

// ----------------------------------------------------------------------

static void window_destroy( pal_window_o *self ) {
	glfwDestroyWindow( self->window );
	delete self;
}

// ----------------------------------------------------------------------

static void window_draw( pal_window_o *self ) {
}

// ----------------------------------------------------------------------

static void window_update( pal_window_o *self ) {
}

// ----------------------------------------------------------------------

static bool window_should_close( pal_window_o *self ) {
	return glfwWindowShouldClose( self->window );
}

// ----------------------------------------------------------------------

static bool window_create_surface( pal_window_o *self, le_backend_vk_instance_o *instance_o ) {
	static auto instance_api = Registry::getApi<le_backend_vk_api>()->instance_i;
	VkInstance  vkInstance   = instance_api.get_VkInstance( instance_o );
	auto        result       = glfwCreateWindowSurface( vkInstance, self->window, nullptr, &self->mSurface );
	if ( result == VK_SUCCESS ) {
		std::cout << "Created surface" << std::endl;
	} else {
		std::cerr << "Error creating surface" << std::endl;
	}
	return true;
}

// ----------------------------------------------------------------------

VkSurfaceKHR window_get_vk_surface_khr(pal_window_o* self){
	return self->mSurface;
}

// ----------------------------------------------------------------------
// note: this is the only function for which we need to link this lib against vulkan!
static void window_destroy_surface(pal_window_o* self, le_backend_vk_instance_o * instance_o){
	static auto instance_api = Registry::getApi<le_backend_vk_api>()->instance_i;
	VkInstance  vkInstance   = instance_api.get_VkInstance( instance_o );
	PFN_vkDestroySurfaceKHR destroySurfaceFun = (PFN_vkDestroySurfaceKHR)vkGetInstanceProcAddr(vkInstance,"vkDestroySurfaceKHR");
	destroySurfaceFun(vkInstance,self->mSurface,nullptr);
	std::cout << "Surface destroyed" << std::endl;
}

// ----------------------------------------------------------------------

static int init() {
	auto result = glfwInit();
	assert( result == GLFW_TRUE );

	if ( glfwVulkanSupported() ) {
		std::cout << "Vulkan supported." << std::endl;
	} else {
		std::cout << "Vulkan not supported." << std::endl;
	}

	return result;
}

// ----------------------------------------------------------------------

static const char** get_required_vk_instance_extensions(uint32_t *count){
	return glfwGetRequiredInstanceExtensions(count);
}

// ----------------------------------------------------------------------

static void pollEvents() {
	glfwPollEvents();
}

static void terminate() {
	glfwTerminate();
	std::cout << "Glfw was terminated." << std::endl;
}

void register_pal_window_api( void *api ) {

	auto windowApi = static_cast<pal_window_api *>( api );

	windowApi->init                       = init;
	windowApi->terminate                  = terminate;
	windowApi->pollEvents                 = pollEvents;
	windowApi->get_required_vk_extensions = get_required_vk_instance_extensions;

	auto &window_interface              = windowApi->window_i;
	window_interface.create             = window_create;
	window_interface.destroy            = window_destroy;
	window_interface.should_close       = window_should_close;
	window_interface.update             = window_update;
	window_interface.draw               = window_draw;
	window_interface.create_surface     = window_create_surface;
	window_interface.destroy_surface    = window_destroy_surface;
	window_interface.get_vk_surface_khr = window_get_vk_surface_khr;

	Registry::loadLibraryPersistently( "libglfw.so" );

}


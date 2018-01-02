#include "pal_window/pal_window.h"
#include "assert.h"

#include <iostream>

#define GLFW_INCLUDE_VULKAN
#include "GLFW/glfw3.h"

struct pal_window_settings_o {
	int          width   = 640;
	int          height  = 480;
	std::string  title   = "default window title";
	GLFWmonitor *monitor = nullptr;
};

struct pal_window_o {
	GLFWwindow *          window   = nullptr;
	VkSurfaceKHR          mSurface = nullptr;
	VkExtent2D            mSurfaceExtent;
	pal_window_settings_o mSettings;
};

// ----------------------------------------------------------------------

static pal_window_settings_o* window_settings_create(){
	pal_window_settings_o* obj = new(pal_window_settings_o);
	return obj;
}

// ----------------------------------------------------------------------

static void window_settings_set_title(pal_window_settings_o* self_, const char* title_){
	self_->title = std::string(title_);
}

// ----------------------------------------------------------------------

static void window_settings_set_width(pal_window_settings_o* self_, int width_){
	self_->width = width_;
}

// ----------------------------------------------------------------------

static void window_settings_set_height(pal_window_settings_o* self_, int height_){
	self_->height = height_;
}

// ----------------------------------------------------------------------

static void window_settings_destroy(pal_window_settings_o* self_){
	delete self_;
}

// ----------------------------------------------------------------------

static void glfw_framebuffer_resize_callback(GLFWwindow* window_, int width_px_, int height_px_){
	auto self = static_cast<pal_window_o*> (glfwGetWindowUserPointer(window_));
	glfwGetWindowSize( window_, &self->mSettings.width, &self->mSettings.height );
	self->mSurfaceExtent.width  = uint32_t(width_px_ );
	self->mSurfaceExtent.height = uint32_t(height_px_);
	std::cout << "Framebuffer resized callback: " << width_px_ << ", "  << height_px_ << std::endl;
};

// ----------------------------------------------------------------------

static pal_window_o *window_create(const pal_window_settings_o* settings_) {
	auto obj = new pal_window_o();

	if (settings_){
		obj->mSettings = *settings_;
	}

	// TODO: implement GLFW window hints, based on settings.
	// See: http://www.glfw.org/docs/latest/window_guide.html#window_hints

	glfwWindowHint( GLFW_CLIENT_API, GLFW_NO_API );

	obj->window = glfwCreateWindow( obj->mSettings.width, obj->mSettings.height, obj->mSettings.title.c_str(), obj->mSettings.monitor, nullptr );

	// Set the user pointer so callbacks know which window they belong to
	glfwSetWindowUserPointer(obj->window,obj);

	// FIXME: Callback function address target may have changed after library hot-reload
	// Problem -- the address of the callback function may have changed
	// after the library was reloaded, and we would have to go through
	// all windows to patch the callback function.
	//
	// We could make sure that there is a forwarder which has a constant
	// address, and which calls, in turn, a method which we can patch during
	// reload.
	//
	// The forwarder function would have to live somewhere permanent.
	// Could this be a function object inside registry?
	//
	// This problem arises mostly because each window within GLFW may have its own
	// callbacks - which means, each GLFW window would have to be patched after hot-reloading.
	glfwSetFramebufferSizeCallback( obj->window, glfw_framebuffer_resize_callback );

	return obj;
}

// ----------------------------------------------------------------------

static void window_destroy( pal_window_o *self ) {
	glfwDestroyWindow( self->window );
	delete self;
}

// ----------------------------------------------------------------------

static bool window_should_close( pal_window_o *self ) {
	return glfwWindowShouldClose( self->window );
}

// ----------------------------------------------------------------------

static bool window_create_surface( pal_window_o *self, VkInstance vkInstance) {
	auto result = glfwCreateWindowSurface( vkInstance, self->window, nullptr, &self->mSurface );
	if ( result == VK_SUCCESS ) {
		int tmp_w = 0;
		int tmp_h = 0;
		glfwGetFramebufferSize( self->window, &tmp_w, &tmp_h );
		self->mSurfaceExtent.height = uint32_t( tmp_h );
		self->mSurfaceExtent.width  = uint32_t( tmp_w );
		std::cout << "Created surface" << std::endl;
	} else {
		std::cerr << "Error creating surface" << std::endl;
		return false;
	}
	return true;
}

// ----------------------------------------------------------------------

static uint32_t window_get_surface_width(pal_window_o* self){
	if (self->mSurface){
		return self->mSurfaceExtent.width;
	}
	return 0;
}

// ----------------------------------------------------------------------

static uint32_t window_get_surface_height(pal_window_o* self){
	if (self->mSurface){
		return self->mSurfaceExtent.height;
	}
	return 0;
}

// ----------------------------------------------------------------------

static VkSurfaceKHR window_get_vk_surface_khr(pal_window_o* self){
	return self->mSurface;
}

// ----------------------------------------------------------------------
// note: this is the only function for which we need to link this lib against vulkan!
static void window_destroy_surface(pal_window_o* self, VkInstance vkInstance){
	PFN_vkDestroySurfaceKHR destroySurfaceFun = reinterpret_cast<PFN_vkDestroySurfaceKHR>(vkGetInstanceProcAddr(vkInstance,"vkDestroySurfaceKHR"));
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

// ----------------------------------------------------------------------

static void terminate() {
	glfwTerminate();
	std::cout << "Glfw was terminated." << std::endl;
}

// ----------------------------------------------------------------------

void register_pal_window_api( void *api ) {

	auto windowApi = static_cast<pal_window_api *>( api );

	windowApi->init                       = init;
	windowApi->terminate                  = terminate;
	windowApi->pollEvents                 = pollEvents;
	windowApi->get_required_vk_extensions = get_required_vk_instance_extensions;

	auto &window_i              = windowApi->window_i;
	window_i.create             = window_create;
	window_i.destroy            = window_destroy;
	window_i.should_close       = window_should_close;
	window_i.get_surface_width  = window_get_surface_width;
	window_i.get_surface_height = window_get_surface_height;
	window_i.create_surface     = window_create_surface;
	window_i.destroy_surface    = window_destroy_surface;
	window_i.get_vk_surface_khr = window_get_vk_surface_khr;

	auto &window_settings_i      = windowApi->window_settings_i;
	window_settings_i.create     = window_settings_create;
	window_settings_i.destroy    = window_settings_destroy;
	window_settings_i.set_title  = window_settings_set_title;
	window_settings_i.set_width  = window_settings_set_width;
	window_settings_i.set_height = window_settings_set_height;

	Registry::loadLibraryPersistently( "libglfw.so" );

}


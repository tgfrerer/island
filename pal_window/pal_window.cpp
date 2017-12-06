#include "pal_window/pal_window.h"
#include "assert.h"

#include <iostream>

#define GLFW_INCLUDE_VULKAN
#include "GLFW/glfw3.h"

struct pal_window_o {
	GLFWwindow *window;
	void *      placeholder[ 31 ];
};

static pal_window_o *create() {
	auto obj = new pal_window_o();
	glfwWindowHint( GLFW_CLIENT_API, GLFW_NO_API );
	obj->window = glfwCreateWindow( 200, 200, "hello world", nullptr, nullptr );
	return obj;
}

static void destroy( pal_window_o *self ) {
	glfwDestroyWindow( self->window );
	delete self;
}

static void draw( pal_window_o *self ) {
}

static void update( pal_window_o *self ) {
//	std::cout << ".";
}

static bool should_close( pal_window_o *self ) {
	return glfwWindowShouldClose( self->window );
}

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

static void pollEvents() {
	glfwPollEvents();
}

static void terminate() {
	glfwTerminate();
	std::cout << "Glfw terminated." << std::endl;
}

void register_pal_window_api( void *api ) {

	auto windowApi = static_cast<pal_window_api *>( api );

	windowApi->init       = init;
	windowApi->terminate  = terminate;
	windowApi->pollEvents = pollEvents;

	auto &window_interface        = windowApi->window_i;
	window_interface.create       = create;
	window_interface.destroy      = destroy;
	window_interface.should_close = should_close;
	window_interface.update       = update;
	window_interface.draw         = draw;
}

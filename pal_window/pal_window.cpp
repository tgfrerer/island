#include "pal_window/pal_window.h"
#include "traffic_light/traffic_light.h"

#include "assert.h"
#include "GL/glew.h"
#include "GLFW/glfw3.h"

struct pal_window_o {
	GLFWwindow *       window;
	pal::TrafficLight *tl;
	void *             placeholder[ 31 ];
};

static pal_window_o *create() {
	auto obj    = new pal_window_o();
	obj->tl     = new pal::TrafficLight();
	obj->window = glfwCreateWindow( 200, 200, "hello world", nullptr, nullptr );
	return obj;
}

static void destroy( pal_window_o *instance ) {
	glfwDestroyWindow( instance->window );
	delete instance;
}

static void draw( pal_window_o *instance ) {
	glfwMakeContextCurrent( instance->window );

	switch ( instance->tl->getState() ) {
	case State::eInitial:
	    break;
	case State::eGreen:
		glClearColor( 0.f, 1.f, 0.f, 0.0f );
	    break;
	case State::eBlink:
		glClearColor( 1.f, 1.f, 1.f, 0.0f );
	    break;
	case State::eRed:
		glClearColor( 1.f, 0.f, 0.f, 0.0f );
	    break;
	case State::eYellow:
		glClearColor( 1.f, 1.f, 0.f, 0.0f );
	    break;
	}

	glClear( GL_COLOR_BUFFER_BIT );
	glfwSwapBuffers( instance->window );
}

static void update( pal_window_o *instance ) {
	glfwMakeContextCurrent( instance->window );
	static uint64_t frameCounter = 0;

	if ( frameCounter % 60 == 0 ) {
		instance->tl->step();
	}

	++frameCounter;

	glfwPollEvents();
}

static bool should_close( pal_window_o *instance ) {
	return glfwWindowShouldClose( instance->window );
}

static int initializeGLFW() {
	return glfwInit();
}

void register_pal_window_api( void *api ) {

	auto  typedApi                = static_cast<pal_window_api *>( api );
	auto &window_interface        = typedApi->window_i;
	window_interface.create       = create;
	window_interface.destroy      = destroy;
	window_interface.should_close = should_close;
	window_interface.update       = update;
	window_interface.draw         = draw;

	int result = initializeGLFW();
	assert( result == GLFW_TRUE );
}

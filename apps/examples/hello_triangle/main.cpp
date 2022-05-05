#include "hello_triangle_app/hello_triangle_app.h"

/*

Not much to see here - main.cpp works as a stub.

Its main purpose is to load the main application module, and to
sustain the main `update()` loop.

For each iteration of the main loop, we're calling `update()` on the
application. This goes on until the application's `update()` method
returns `false`.

If hot-reloading is activated (via the `PLUGINS_DYNAMIC` compiler
flag) we're additionally checking if any modules need to be reloaded.

*/

// ----------------------------------------------------------------------

int main( int argc, char const* argv[] ) {

	HelloTriangleApp::initialize();

	{
		// We instantiate HelloTriangleApp in its own scope - so that
		// it will be destroyed before HelloTriangleApp::terminate
		// is called.

		HelloTriangleApp HelloTriangleApp{};

		for ( ;; ) {

#ifdef PLUGINS_DYNAMIC
			le_core_poll_for_module_reloads();
#endif
			auto result = HelloTriangleApp.update();

			if ( !result ) {
				break;
			}
		}
	}

	// Must only be called once last HelloTriangleApp is destroyed
	HelloTriangleApp::terminate();

	return 0;
}

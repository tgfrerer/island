#include "hello_triangle_app/hello_triangle_app.h"

// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

	HelloTriangleApp::initialize();

	{
		// We instantiate HelloTriangleApp in its own scope - so that
		// it will be destroyed before HelloTriangleApp::terminate
		// is called.

		HelloTriangleApp HelloTriangleApp{};

		for ( ;; ) {

#ifdef PLUGINS_DYNAMIC
			Registry::pollForDynamicReload();
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

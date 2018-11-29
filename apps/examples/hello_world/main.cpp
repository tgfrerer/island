#include "hello_world_app/hello_world_app.h"

// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

	HelloWorldApp::initialize();

	{
		// We instantiate HelloWorldApp in its own scope - so that
		// it will be destroyed before HelloWorldApp::terminate
		// is called.

		HelloWorldApp HelloWorldApp{};

		for ( ;; ) {

#ifdef PLUGINS_DYNAMIC
			Registry::pollForDynamicReload();
#endif
			auto result = HelloWorldApp.update();

			if ( !result ) {
				break;
			}
		}
	}

	// Must only be called once last HelloWorldApp is destroyed
	HelloWorldApp::terminate();

	return 0;
}

#include "test_app/test_app.h"

// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

	TestApp::initialize();

	{
		// We instantiate TestApp in its own scope - so that
		// it will be destroyed before TestApp::terminate
		// is called.

		TestApp testApp{};

		for ( ;; ) {
			Registry::pollForDynamicReload();

			auto result = testApp.update();

			if ( !result ) {
				break;
			}
		}
	}

	// Must only be called once last TestApp is destroyed
	TestApp::terminate();

	return 0;
}

#include "test_mipmaps_app/test_mipmaps_app.h"

// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

	TestMipmapsApp::initialize();

	{
		// We instantiate TestMipmapsApp in its own scope - so that
		// it will be destroyed before TestMipmapsApp::terminate
		// is called.

		TestMipmapsApp TestMipmapsApp{};

		for ( ;; ) {

#ifdef PLUGINS_DYNAMIC
			Registry::pollForDynamicReload();
#endif
			auto result = TestMipmapsApp.update();

			if ( !result ) {
				break;
			}
		}
	}

	// Must only be called once last TestMipmapsApp is destroyed
	TestMipmapsApp::terminate();

	return 0;
}

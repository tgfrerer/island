#include "lensflare_app/lensflare_app.h"

// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

	LensflareApp::initialize();

	{
		// We instantiate LensflareApp in its own scope - so that
		// it will be destroyed before LensflareApp::terminate
		// is called.

		LensflareApp LensflareApp{};

		for ( ;; ) {

#ifdef PLUGINS_DYNAMIC
			Registry::pollForDynamicReload();
#endif
			auto result = LensflareApp.update();

			if ( !result ) {
				break;
			}
		}
	}

	// Must only be called once last LensflareApp is destroyed
	LensflareApp::terminate();

	return 0;
}

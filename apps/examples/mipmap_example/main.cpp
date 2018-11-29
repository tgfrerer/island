#include "mipmap_example_app/mipmap_example_app.h"

// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

	MipmapExampleApp::initialize();

	{
		// We instantiate MipmapExampleApp in its own scope - so that
		// it will be destroyed before MipmapExampleApp::terminate
		// is called.

		MipmapExampleApp MipmapExampleApp{};

		for ( ;; ) {

#ifdef PLUGINS_DYNAMIC
			Registry::pollForDynamicReload();
#endif
			auto result = MipmapExampleApp.update();

			if ( !result ) {
				break;
			}
		}
	}

	// Must only be called once last MipmapExampleApp is destroyed
	MipmapExampleApp::terminate();

	return 0;
}

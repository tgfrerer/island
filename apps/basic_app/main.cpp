#include "basic_app/basic_app.h"

// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

	BasicApp::initialize();

	{
		// We instantiate TestApp in its own scope - so that
		// it will be destroyed before TestApp::terminate
		// is called.

		BasicApp basicApp{};

		for ( ;; ) {

#ifdef PLUGINS_DYNAMIC
			Registry::pollForDynamicReload();
#endif
			auto result = basicApp.update();

			if ( !result ) {
				break;
			}
		}
	}

	// Must only be called once last BasicApp is destroyed
	BasicApp::terminate();

	return 0;
}

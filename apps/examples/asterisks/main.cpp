#include "asterisks_app/asterisks_app.h"

// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

	AsterisksApp::initialize();

	{
		// We instantiate AsterisksApp in its own scope - so that
		// it will be destroyed before AsterisksApp::terminate
		// is called.

		AsterisksApp AsterisksApp{};

		for ( ;; ) {

#ifdef PLUGINS_DYNAMIC
			le_core_poll_for_module_reloads();
#endif
			auto result = AsterisksApp.update();

			if ( !result ) {
				break;
			}
		}
	}

	// Must only be called once last AsterisksApp is destroyed
	AsterisksApp::terminate();

	return 0;
}

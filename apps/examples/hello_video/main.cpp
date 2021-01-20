#include "hello_video_app/hello_video_app.h"

// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

	HelloVideoApp::initialize();

	{
		// We instantiate HelloVideoApp in its own scope - so that
		// it will be destroyed before HelloVideoApp::terminate
		// is called.

		HelloVideoApp HelloVideoApp{};

		for ( ;; ) {

#ifdef PLUGINS_DYNAMIC
			le_core_poll_for_module_reloads();
#endif
			auto result = HelloVideoApp.update();

			if ( !result ) {
				break;
			}
		}
	}

	// Must only be called once last HelloVideoApp is destroyed
	HelloVideoApp::terminate();

	return 0;
}

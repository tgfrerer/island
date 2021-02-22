#include "test_file_watch_app/test_file_watch_app.h"

// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

	TestFileWatchApp::initialize();

	{
		// We instantiate TestFileWatchApp in its own scope - so that
		// it will be destroyed before TestFileWatchApp::terminate
		// is called.

		TestFileWatchApp TestFileWatchApp{};

		for ( ;; ) {

#ifdef PLUGINS_DYNAMIC
			le_core_poll_for_module_reloads();
#endif
			auto result = TestFileWatchApp.update();

			if ( !result ) {
				break;
			}
		}
	}

	// Must only be called once last TestFileWatchApp is destroyed
	TestFileWatchApp::terminate();

	return 0;
}

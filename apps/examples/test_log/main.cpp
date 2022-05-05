#include "test_log_app/test_log_app.h"

// ----------------------------------------------------------------------

int main( int argc, char const* argv[] ) {

	TestLogApp::initialize();

	{
		// We instantiate TestLogApp in its own scope - so that
		// it will be destroyed before TestLogApp::terminate
		// is called.

		TestLogApp TestLogApp{};

		for ( ;; ) {

#ifdef PLUGINS_DYNAMIC
			le_core_poll_for_module_reloads();
#endif
			auto result = TestLogApp.update();

			if ( !result ) {
				break;
			}
		}
	}

	// Must only be called once last TestLogApp is destroyed
	TestLogApp::terminate();

	return 0;
}

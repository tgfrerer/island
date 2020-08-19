#include "multi_window_example_app/multi_window_example_app.h"

// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

	MultiWindowExampleApp::initialize();

	{
		// We instantiate MultiWindowExampleApp in its own scope - so that
		// it will be destroyed before MultiWindowExampleApp::terminate
		// is called.

		MultiWindowExampleApp MultiWindowExampleApp{};

		for ( ;; ) {

#ifdef PLUGINS_DYNAMIC
			le_core_poll_for_module_reloads();
#endif
			auto result = MultiWindowExampleApp.update();

			if ( !result ) {
				break;
			}
		}
	}

	// Must only be called once last MultiWindowExampleApp is destroyed
	MultiWindowExampleApp::terminate();

	return 0;
}

#include "compute_example_app/compute_example_app.h"

// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

	ComputeExampleApp::initialize();

	{
		// We instantiate ComputeExampleApp in its own scope - so that
		// it will be destroyed before ComputeExampleApp::terminate
		// is called.

		ComputeExampleApp ComputeExampleApp{};

		for ( ;; ) {

#ifdef PLUGINS_DYNAMIC
			le_core_poll_for_module_reloads();
#endif
			auto result = ComputeExampleApp.update();

			if ( !result ) {
				break;
			}
		}
	}

	// Must only be called once last ComputeExampleApp is destroyed
	ComputeExampleApp::terminate();

	return 0;
}

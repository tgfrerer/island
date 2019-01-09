#include "test_compute_app/test_compute_app.h"

// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

	TestComputeApp::initialize();

	{
		// We instantiate TestComputeApp in its own scope - so that
		// it will be destroyed before TestComputeApp::terminate
		// is called.

		TestComputeApp TestComputeApp{};

		for ( ;; ) {

#ifdef PLUGINS_DYNAMIC
			Registry::pollForDynamicReload();
#endif
			auto result = TestComputeApp.update();

			if ( !result ) {
				break;
			}
		}
	}

	// Must only be called once last TestComputeApp is destroyed
	TestComputeApp::terminate();

	return 0;
}

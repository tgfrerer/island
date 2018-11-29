#include "workbench_app/workbench_app.h"

// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

	WorkbenchApp::initialize();

	{
		// We instantiate WorkbenchApp in its own scope - so that
		// it will be destroyed before WorkbenchApp::terminate
		// is called.

		WorkbenchApp workbenchApp{};

		for ( ;; ) {

#ifdef PLUGINS_DYNAMIC
			Registry::pollForDynamicReload();
#endif
			auto result = workbenchApp.update();

			if ( !result ) {
				break;
			}
		}
	}

	// Must only be called once last WorkbenchApp is destroyed
	WorkbenchApp::terminate();

	return 0;
}

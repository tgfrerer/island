#include "lut_grading_example_app/lut_grading_example_app.h"

// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

	LutGradingExampleApp::initialize();

	{
		// We instantiate LutGradingExampleApp in its own scope - so that
		// it will be destroyed before LutGradingExampleApp::terminate
		// is called.

		LutGradingExampleApp LutGradingExampleApp{};

		for ( ;; ) {

#ifdef PLUGINS_DYNAMIC
			le_core_poll_for_module_reloads();
#endif
			auto result = LutGradingExampleApp.update();

			if ( !result ) {
				break;
			}
		}
	}

	// Must only be called once last LutGradingExampleApp is destroyed
	LutGradingExampleApp::terminate();

	return 0;
}

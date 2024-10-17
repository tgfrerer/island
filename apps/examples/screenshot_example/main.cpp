#include "screenshot_example_app/screenshot_example_app.h"

// ----------------------------------------------------------------------

int main( int argc, char const* argv[] ) {

	ScreenshotExampleApp::initialize();

	{
		// We instantiate ScreenshotExampleApp in its own scope - so that
		// it will be destroyed before ScreenshotExampleApp::terminate
		// is called.

		ScreenshotExampleApp ScreenshotExampleApp{};

		for ( ;; ) {

#ifdef PLUGINS_DYNAMIC
			le_core_poll_for_module_reloads();
#endif
			auto result = ScreenshotExampleApp.update();

			if ( !result ) {
				break;
			}
		}
	}

	// Must only be called once last ScreenshotExampleApp is destroyed
	ScreenshotExampleApp::terminate();

	return 0;
}

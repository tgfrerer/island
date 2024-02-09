#include "exr_decode_example_app/exr_decode_example_app.h"

// ----------------------------------------------------------------------

int main( int argc, char const* argv[] ) {

	ExrDecodeExampleApp::initialize();

	{
		// We instantiate ExrDecodeExampleApp in its own scope - so that
		// it will be destroyed before ExrDecodeExampleApp::terminate
		// is called.

		ExrDecodeExampleApp ExrDecodeExampleApp{};

		for ( ;; ) {

#ifdef PLUGINS_DYNAMIC
			le_core_poll_for_module_reloads();
#endif
			auto result = ExrDecodeExampleApp.update();

			if ( !result ) {
				break;
			}
		}
	}

	// Must only be called once last ExrDecodeExampleApp is destroyed
	ExrDecodeExampleApp::terminate();

	return 0;
}

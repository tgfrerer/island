#include "video_player_example_app/video_player_example_app.h"

// ----------------------------------------------------------------------

int main( int argc, char const* argv[] ) {

	VideoPlayerExampleApp::initialize();

	{
		// We instantiate VideoPlayerExampleApp in its own scope - so that
		// it will be destroyed before VideoPlayerExampleApp::terminate
		// is called.

		VideoPlayerExampleApp VideoPlayerExampleApp{};

		for ( ;; ) {

#ifdef PLUGINS_DYNAMIC
			le_core_poll_for_module_reloads();
#endif
			auto result = VideoPlayerExampleApp.update();

			if ( !result ) {
				break;
			}
		}
	}

	// Must only be called once last VideoPlayerExampleApp is destroyed
	VideoPlayerExampleApp::terminate();

	return 0;
}

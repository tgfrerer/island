#include "imgui_example_app/imgui_example_app.h"

// ----------------------------------------------------------------------

int main( int argc, char const* argv[] ) {

	ImguiExampleApp::initialize();

	{
		// We instantiate ImguiExampleApp in its own scope - so that
		// it will be destroyed before ImguiExampleApp::terminate
		// is called.

		ImguiExampleApp ImguiExampleApp{};

		for ( ;; ) {

#ifdef PLUGINS_DYNAMIC
			le_core_poll_for_module_reloads();
#endif
			auto result = ImguiExampleApp.update();

			if ( !result ) {
				break;
			}
		}
	}

	// Must only be called once last ImguiExampleApp is destroyed
	ImguiExampleApp::terminate();

	return 0;
}

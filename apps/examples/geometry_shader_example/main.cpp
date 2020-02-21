#include "geometry_shader_example_app/geometry_shader_example_app.h"

// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

	GeometryShaderExampleApp::initialize();

	{
		// We instantiate GeometryShaderExampleApp in its own scope - so that
		// it will be destroyed before GeometryShaderExampleApp::terminate
		// is called.

		GeometryShaderExampleApp GeometryShaderExampleApp{};

		for ( ;; ) {

#ifdef PLUGINS_DYNAMIC
			le_core_poll_for_module_reloads();
#endif
			auto result = GeometryShaderExampleApp.update();

			if ( !result ) {
				break;
			}
		}
	}

	// Must only be called once last GeometryShaderExampleApp is destroyed
	GeometryShaderExampleApp::terminate();

	return 0;
}

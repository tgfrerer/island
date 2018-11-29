#include "mesh_generator_example_app/mesh_generator_example_app.h"

// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

	MeshGeneratorExampleApp::initialize();

	{
		// We instantiate MeshGeneratorExampleApp in its own scope - so that
		// it will be destroyed before MeshGeneratorExampleApp::terminate
		// is called.

		MeshGeneratorExampleApp MeshGeneratorExampleApp{};

		for ( ;; ) {

#ifdef PLUGINS_DYNAMIC
			Registry::pollForDynamicReload();
#endif
			auto result = MeshGeneratorExampleApp.update();

			if ( !result ) {
				break;
			}
		}
	}

	// Must only be called once last MeshGeneratorExampleApp is destroyed
	MeshGeneratorExampleApp::terminate();

	return 0;
}

#include "test_mesh_generator_app/test_mesh_generator_app.h"

// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

	TestMeshGeneratorApp::initialize();

	{
		// We instantiate TestMeshGeneratorApp in its own scope - so that
		// it will be destroyed before TestMeshGeneratorApp::terminate
		// is called.

		TestMeshGeneratorApp TestMeshGeneratorApp{};

		for ( ;; ) {

#ifdef PLUGINS_DYNAMIC
			Registry::pollForDynamicReload();
#endif
			auto result = TestMeshGeneratorApp.update();

			if ( !result ) {
				break;
			}
		}
	}

	// Must only be called once last TestMeshGeneratorApp is destroyed
	TestMeshGeneratorApp::terminate();

	return 0;
}

#include "triangle_app/triangle_app.h"

// ----------------------------------------------------------------------

int main( int argc, char const* argv[] ) {

	TriangleApp::initialize();

	{
		// We instantiate TriangleApp in its own scope - so that
		// it will be destroyed before TriangleApp::terminate
		// is called.

		TriangleApp TriangleApp{};

		for ( ;; ) {

#ifdef PLUGINS_DYNAMIC
			le_core_poll_for_module_reloads();
#endif
			auto result = TriangleApp.update();

			if ( !result ) {
				break;
			}
		}
	}

	// Must only be called once last TriangleApp is destroyed
	TriangleApp::terminate();

	return 0;
}

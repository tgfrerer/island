#include "quad_bezier_app/quad_bezier_app.h"

// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

	QuadBezierApp::initialize();

	{
		// We instantiate QuadBezierApp in its own scope - so that
		// it will be destroyed before QuadBezierApp::terminate
		// is called.

		QuadBezierApp QuadBezierApp{};

		for ( ;; ) {

#ifdef PLUGINS_DYNAMIC
			Registry::pollForDynamicReload();
#endif
			auto result = QuadBezierApp.update();

			if ( !result ) {
				break;
			}
		}
	}

	// Must only be called once last QuadBezierApp is destroyed
	QuadBezierApp::terminate();

	return 0;
}

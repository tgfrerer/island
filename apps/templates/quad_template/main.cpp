#include "quad_template_app/quad_template_app.h"

// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

	QuadTemplateApp::initialize();

	{
		// We instantiate QuadTemplateApp in its own scope - so that
		// it will be destroyed before QuadTemplateApp::terminate
		// is called.

		QuadTemplateApp QuadTemplateApp{};

		for ( ;; ) {

#ifdef PLUGINS_DYNAMIC
			Registry::pollForDynamicReload();
#endif
			auto result = QuadTemplateApp.update();

			if ( !result ) {
				break;
			}
		}
	}

	// Must only be called once last QuadTemplateApp is destroyed
	QuadTemplateApp::terminate();

	return 0;
}

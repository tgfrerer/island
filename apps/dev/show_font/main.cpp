#include "show_font_app/show_font_app.h"

// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

	ShowFontApp::initialize();

	{
		// We instantiate ShowFontApp in its own scope - so that
		// it will be destroyed before ShowFontApp::terminate
		// is called.

		ShowFontApp ShowFontApp{};

		for ( ;; ) {

#ifdef PLUGINS_DYNAMIC
			Registry::pollForDynamicReload();
#endif
			auto result = ShowFontApp.update();

			if ( !result ) {
				break;
			}
		}
	}

	// Must only be called once last ShowFontApp is destroyed
	ShowFontApp::terminate();

	return 0;
}

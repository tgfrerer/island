#include "test_img_swapchain_app/test_img_swapchain_app.h"

// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

	TestImgSwapchainApp::initialize();

	{
		// We instantiate TestImgSwapchainApp in its own scope - so that
		// it will be destroyed before TestImgSwapchainApp::terminate
		// is called.

		TestImgSwapchainApp TestImgSwapchainApp{};

		for ( ;; ) {

#ifdef PLUGINS_DYNAMIC
			Registry::pollForDynamicReload();
#endif
			auto result = TestImgSwapchainApp.update();

			if ( !result ) {
				break;
			}
		}
	}

	// Must only be called once last TestImgSwapchainApp is destroyed
	TestImgSwapchainApp::terminate();

	return 0;
}

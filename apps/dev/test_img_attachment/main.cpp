#include "test_img_attachment_app/test_img_attachment_app.h"

// ----------------------------------------------------------------------

int main( int argc, char const *argv[] ) {

	TestImgAttachmentApp::initialize();

	{
		// We instantiate TestImgAttachmentApp in its own scope - so that
		// it will be destroyed before TestImgAttachmentApp::terminate
		// is called.

		TestImgAttachmentApp TestImgAttachmentApp{};

		for ( ;; ) {

#ifdef PLUGINS_DYNAMIC
			Registry::pollForDynamicReload();
#endif
			auto result = TestImgAttachmentApp.update();

			if ( !result ) {
				break;
			}
		}
	}

	// Must only be called once last TestImgAttachmentApp is destroyed
	TestImgAttachmentApp::terminate();

	return 0;
}

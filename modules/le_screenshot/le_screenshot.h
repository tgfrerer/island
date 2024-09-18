#ifndef GUARD_le_screenshot_H
#define GUARD_le_screenshot_H

/*

LE_SCREENSHOT MODULE
-------------------

This module allows you to quickly pull screenshots.

USAGE
-----

Call:

    le::Screenshot::init();

in your app's init() method. This is so that `le_screenshot` can request
backend capabilities which it depends on.

Once the renderer has been set up, you can create `le_screenshot_o` objects
from the renderer. You must destroy these before you destroy the renderer.

    app->screenshot = le_screenshot_api_i->le_screenshot_i.create( app->renderer );

Generally, you only need one `le_screenshot_o` - it will internally add
an image swapchain to the renderer while it is active, and will remove this
swapchain again, once the screen recording has completed.


RECORDING SCREENSHOTS
---------------------

Record by calling `record` on the screenshot object near or where you build
the rendergraph in your app's update method.

The `record` method saves a given src image (typically the swapchain image) to file.

        le_screenshot_api_i->le_screenshot_i.record( self->screenshot, rg, nullptr, nullptr, nullptr );

You can record a sequence of frames by feeding the `record` method a uint32_t*
pointing to the count of frames that you want to record. `record` will decrement
the pointed-to uint32_t after every recorded frame until it reaches 0, at
which point recording stops.

        static uint32_t num_screenshots = 7;
        le_screenshot_api_i->le_screenshot_i.record( self->screenshot, rg, self->swapchain_image, &num_screenshots, &settings );

Note: Do not point to a stack-value for `num_screenshots`, as this will mean
that recording never end. Make it static, or something heap-allocated.

If you don't provide settings, default settings will be used - width and heigth
extents will be set to the extents of the oldest swapchain.

If you don't provide a `src_image`, the image of the first swapchain that is found with
the renderer will get used.


Hint:
-----

* If you want super super fast png encoding, and you are on Linux, you can
  switch on the fpnge encoder: Add this option setting to your app's topmost
  CMakeLists.txt file:

    set(LE_PNG_ENABLE_FPNGE ON)



 */

#include "le_core.h"

struct le_screenshot_o;
struct le_renderer_o;
struct le_rendergraph_o;
struct le_swapchain_img_settings_t;
typedef class le_image_resource_handle_t* le_image_resource_handle;

// clang-format off
struct le_screenshot_api {

	struct le_screenshot_interface_t {
		bool              ( * init    ) ( );
		le_screenshot_o * ( * create  ) ( le_renderer_o* renderer);
		void              ( * destroy ) ( le_screenshot_o* self );
		bool              ( * record  ) ( le_screenshot_o* self, le_rendergraph_o* rg, le_image_resource_handle src_image, uint32_t* num_images, le_swapchain_img_settings_t const * p_settings );
	};

	le_screenshot_interface_t       le_screenshot_i;
};
// clang-format on

LE_MODULE( le_screenshot );
LE_MODULE_LOAD_DEFAULT( le_screenshot );

#ifdef __cplusplus

namespace le_screenshot {
static const auto& api             = le_screenshot_api_i;
static const auto& le_screenshot_i = api->le_screenshot_i;
} // namespace le_screenshot

class LeScreenshot : NoCopy, NoMove {

  public:
    static bool init() {
        return le_screenshot::le_screenshot_i.init();
    }
};

namespace le {
using Screenshot = LeScreenshot;
}

#endif // __cplusplus

#endif

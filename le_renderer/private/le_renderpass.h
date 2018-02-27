#ifndef GUARD_LE_RENDERPASS_H
#define GUARD_LE_RENDERPASS_H

#include "le_renderer/le_renderer.h"
#include <vector>
// ----------------------------------------------------------------------

#define LE_RENDERPASS_MARKER_EXTERNAL "rp-external"

#ifdef __cplusplus
extern "C" {
#endif

void register_le_renderpass_api( void *api_ );

#ifdef __cplusplus
}
#endif

struct le_renderpass_o {

	uint64_t                                              id;
	uint64_t                                              execution_order = 0;
	std::vector<le_renderer_api::image_attachment_info_o> imageAttachments;

	le_renderer_api::pfn_renderpass_setup_t  callbackSetup             = nullptr;
	le_renderer_api::pfn_renderpass_render_t callbackRender            = nullptr;
	void *                                   render_callback_user_data = nullptr;

	char debugName[ 32 ];
};

#endif //GUARD_LE_RENDERPASS_H

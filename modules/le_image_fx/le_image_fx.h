#ifndef GUARD_le_image_fx_H
#define GUARD_le_image_fx_H

//
// any used instance of an effect must stay alife until the rendergraph
// has been updated. After rendergraph update, you are allowed to
// delete any effect instances as none of their data will still be
// referenced.
//

#include "le_core.h"
#include "private/le_renderer/le_renderer_types.h"

struct le_image_fx_blur_o;
struct le_image_fx_blit_o;

struct le_renderer_o;
struct le_resource_info_t;
struct le_rendergraph_o;
struct le_image_resource_handle_t;

// clang-format off
struct le_image_fx_api {

	// blend style presets for blit
	enum BlitBlendPreset : uint32_t {
		BLIT_BLEND_COPY = 0,
		BLIT_BLEND_ALPHA_PREMUL,
		BLIT_BLEND_ADD,
		BLIT_BLEND_MULTIPLY,
	};

	struct blur_preset {
		uint32_t kernel_radius = 11;  ///< gaussian blur influence radius -- use odd number since fast blur will sample two pixels at a time
		float sigma = 3;    		  ///< width of one standard deviation - given in pixels; 1/3 of kernel radius gives good results usually 
	};

	struct le_image_fx_interface_t {

		le_image_fx_blur_o * ( * create_blur    ) ( le_renderer_o* renderer , blur_preset preset);
		void            	 ( * destroy_blur   ) ( le_image_fx_blur_o* self );
		void                 ( * blur_apply     ) ( le_image_fx_blur_o* self, le_rendergraph_o* rg, le_image_resource_handle_t* image_a, le_resource_info_t const* img_info , blur_preset const * preset);

		le_image_fx_blit_o * ( * create_blit    ) ( le_renderer_o* renderer , BlitBlendPreset blend_preset);
		void            	 ( * destroy_blit   ) ( le_image_fx_blit_o* self );
		void                 ( * blit_apply     ) ( le_image_fx_blit_o* self, le_rendergraph_o* rg, le_image_resource_handle_t* image_src, le_image_resource_handle_t* image_dst);

	};

	le_image_fx_interface_t       le_image_fx_i;
};
// clang-format on

LE_MODULE( le_image_fx );
LE_MODULE_LOAD_DEFAULT( le_image_fx );

#ifdef __cplusplus

namespace le_image_fx {
static const auto  api           = le_image_fx_api_i;
static const auto& le_image_fx_i = api->le_image_fx_i;

using BlitBlendPreset = le_image_fx_api::BlitBlendPreset;

// ----------------------------------------------------------------------

class Blur : NoCopy, NoMove {

    le_image_fx_blur_o* self;

  public:
	Blur( le_renderer_o* renderer, le_image_fx_api::blur_preset preset = le_image_fx_api::blur_preset() )
	    : self( le_image_fx::le_image_fx_i.create_blur( renderer, preset ) ) {
	}

	~Blur() {
		le_image_fx::le_image_fx_i.destroy_blur( self );
	}

	void apply( le_rendergraph_o* rg, le_image_resource_handle_t* image_a, le_resource_info_t const* img_info, le_image_fx_api::blur_preset const* preset = nullptr ) {
		le_image_fx::le_image_fx_i.blur_apply( self, rg, image_a, img_info, preset );
	}

	// sugar so that we can directly call Blur()
	void operator()( le_rendergraph_o* rg, le_image_resource_handle_t* image_a, le_resource_info_t const* img_info, le_image_fx_api::blur_preset const* preset = nullptr ) {
		apply( rg, image_a, img_info, preset );
	}

	operator auto() {
		return self;
	}
};

// ----------------------------------------------------------------------

class Blit : NoCopy, NoMove {

    le_image_fx_blit_o* self;

  public:
	Blit( le_renderer_o* renderer, le_image_fx::BlitBlendPreset const& blend_preset = BlitBlendPreset::BLIT_BLEND_COPY )
	    : self( le_image_fx::le_image_fx_i.create_blit( renderer, blend_preset ) ) {
	}

	~Blit() {
		le_image_fx::le_image_fx_i.destroy_blit( self );
	}

	void apply( le_rendergraph_o* rg, le_image_resource_handle_t* image_src, le_image_resource_handle_t* image_dst ) {
		le_image_fx::le_image_fx_i.blit_apply( self, rg, image_src, image_dst );
	}

	// sugar so that we can directly call blit()
	void operator()( le_rendergraph_o* rg, le_image_resource_handle_t* image_src, le_image_resource_handle_t* image_dst ) {
		apply( rg, image_src, image_dst );
	}

	operator auto() {
		return self;
	}
};
} // namespace le_image_fx

#endif // __cplusplus

#endif

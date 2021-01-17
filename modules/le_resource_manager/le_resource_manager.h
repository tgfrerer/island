#ifndef GUARD_le_resource_manager_H
#define GUARD_le_resource_manager_H

/*

# ResourceManager

Helper module for dealing with image resources. ResourceManager automatically
loads image resources from file, and uploads image resources, and declares
image resources to a rendermodule.

Once an image was uploaded, it will not be transferred again, ResourceManager
keeps track of uploaded images.

## Usage

    // In app definition:

    struct app_o {
        ...
        LeResourceManager resource_manager;
        ...
    }

    // In app.setup():

        auto image_handle = LE_IMG_RESOURCE( "test_image" );
        char const * path = "testimage.png";
        auto image_info =
            le::ImageInfoBuilder()
                .setImageType( le::ImageType::e2D )
                .setExtent( 1024, 1024 )
                .build();

        app->resource_manager.add_item(image_handle, image_info, &path);


    // In app.update():

    self->resource_manager.update(render_module);

Call update with the rendermodule you want to use the resources with.

* * * 
    
If you want to upload multiple layers for images - for cubemap images for example -
you can specify multiple paths. NOTE you must specify the number of image array layers
when you specify the image info for the resource
    
    // In app.setup():

        auto cube_image = LE_IMG_RESOURCE( "cube_image" );
        auto image_info =
            le::ImageInfoBuilder()
                .setImageType( le::ImageType::e2D )
                .setExtent( 1024, 1024 )
                .setCreateFlags( LE_IMAGE_CREATE_CUBE_COMPATIBLE_BIT )
                .setArrayLayers( 6 )
                .build();
    
        char const *paths[] = {
            "./local_resources/cubemap/0.png",
            "./local_resources/cubemap/1.png",
            "./local_resources/cubemap/2.png",
            "./local_resources/cubemap/3.png",
            "./local_resources/cubemap/4.png",
            "./local_resources/cubemap/5.png",
        };
        
        app->resource_manager.add_item( cube_image, image_info, paths );

*/

#include "le_core/le_core.h"

struct le_resource_manager_o;
struct le_resource_item_t;
struct le_render_module_o;   // ffdecl. (from le_renderer)
struct le_resource_handle_t; // ffdecl. (from le_renderer)
struct le_resource_info_t;   // ffdecl. (from le_renderer)
struct le_pixels_o;          // ffdelc. (from le_pixels)

// clang-format off
struct le_resource_manager_api {

	struct le_resource_manager_interface_t {

		le_resource_manager_o *  ( * create    ) ( );
		void                     ( * destroy   ) ( le_resource_manager_o* self );
		void                     ( * update    ) ( le_resource_manager_o* self, le_render_module_o* module );
        le_resource_item_t*      ( * add_item_filepaths  ) ( le_resource_manager_o* self, le_resource_handle_t const * image_handle, le_resource_info_t const * image_info, char const * const * arr_image_paths);
        le_resource_item_t*      ( * add_item_pixels  ) ( le_resource_manager_o* self, le_resource_handle_t const * image_handle, le_resource_info_t const * image_info, le_pixels_o**, bool take_ownership);
        void                     ( * update_pixels  ) ( le_resource_manager_o* self, le_resource_item_t* image_handle, le_pixels_o** pixels);
	};

	le_resource_manager_interface_t       le_resource_manager_i;
};
// clang-format on

LE_MODULE( le_resource_manager );
LE_MODULE_LOAD_DEFAULT( le_resource_manager );

#ifdef __cplusplus

namespace le_resource_manager {
static const auto &api                   = le_resource_manager_api_i;
static const auto &le_resource_manager_i = api -> le_resource_manager_i;
} // namespace le_resource_manager

class LeResourceManager : NoCopy, NoMove {

	le_resource_manager_o *self;

  public:
	LeResourceManager()
	    : self( le_resource_manager::le_resource_manager_i.create() ) {
	}

	~LeResourceManager() {
		le_resource_manager::le_resource_manager_i.destroy( self );
	}

	void update( le_render_module_o *module ) {
		le_resource_manager::le_resource_manager_i.update( self, module );
	}

    le_resource_item_t *add_item( le_resource_handle_t const &image_handle, le_resource_info_t const &image_info, char const *const *arr_image_paths ) {
		le_resource_manager::le_resource_manager_i.add_item_filepaths( self, &image_handle, &image_info, arr_image_paths );
	}

    le_resource_item_t *add_item( le_resource_handle_t const &image_handle, le_resource_info_t const &image_info, le_pixels_o **pixels, bool take_ownership = false ) {
		le_resource_manager::le_resource_manager_i.add_item_pixels( self, &image_handle, &image_info, pixels, take_ownership );
	}

	void update_pixels( le_resource_item_t * image_layer, le_pixels_o **pixels = nullptr ) {
		le_resource_manager::le_resource_manager_i.update_pixels( self, image_layer, pixels );
	}

	operator auto() {
		return self;
	}
};

#endif // __cplusplus

#endif

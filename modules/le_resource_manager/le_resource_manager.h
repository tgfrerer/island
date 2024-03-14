#ifndef GUARD_le_resource_manager_H
#define GUARD_le_resource_manager_H

/*

# ResourceManager

Helper module for dealing with image resources. ResourceManager automatically
loads image resources from file, and uploads image resources, and declares
image resources to a render graph. It can optionally watch source files for
change - and update resources on change.

Once an image was uploaded, it will generally not be transferred again, as
ResourceManager  keeps track of uploaded images.

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
                .setFormat( le::Format::eR8G8B8A8Unorm ) // default setting - use different format to request conversion into this format if possible
                .setImageType( le::ImageType::e2D )
                .setExtent( 1024, 1024 )
                .build();

        app->resource_manager.add_item(image_handle, image_info, &path);


    // In app.update():

    self->resource_manager.update( rendergraph );

Call update() with the rendergraph that you want to use the resources with.
If you set the parameter `should_watch` to true when you add an item, then the
source file path for this item will get watched and the resource will get hot-
reloaded if any change in the source file is detected.

* * *

Format conversion:

Note that most decoders will allow you to convert pixel data into target image formats.

You can specify the target image format using the `image_info` in `.add_item` -- note
that if you don't specify a target image format, the resource manager will assume a target
image format of `le::Format::eR8G8B8A8Unorm`.

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
                .setCreateFlags( le::ImageCreateFlagBits::eCubeCompatible )
                .setArrayLayers( 6 )
                .build();

        char const *paths[] = {
            "./local_resources/cubemap/0.png", // +x
            "./local_resources/cubemap/1.png", // -x,
            "./local_resources/cubemap/2.png", // +y,
            "./local_resources/cubemap/3.png", // -y,
            "./local_resources/cubemap/4.png", // +z,
            "./local_resources/cubemap/5.png", // -z
        };

        app->resource_manager.add_item( cube_image, image_info, paths );

* * *

If you want to load a 3D image, say, for a LUT, you can do the following:

    auto lut_image = LE_IMG_RESOURCE( "lut_image" );

    char const* src_image_path =
        "./local_resources/images/hald_8_identity.png";

    // Provide additional information for 3D LUT Image:
    // ImageType, Dimensions need to be explicit.
    auto image_info_color_lut_texture =
        le::ImageInfoBuilder()
            .setImageType( le::ImageType::e3D )
            .setExtent( 64, 64, 64 )
            .build();

    // Instruct resource manager to load data for images from given path
    app->resource_manager.add_item( lut_image, image_info_color_lut_texture, &src_image_path );

*/

#include "le_core.h"

struct le_resource_manager_o;
struct le_rendergraph_o;             // ffdecl. (from le_renderer)
struct le_resource_info_t;           // ffdecl. (from le_renderer)
struct le_image_decoder_interface_t; // ffdecl (from le_core/shared/interfaces/le_image_decoder_interface.h)

LE_OPAQUE_HANDLE( le_img_resource_handle ); // declared in le_renderer.h

// clang-format off
struct le_resource_manager_api {

	struct le_resource_manager_interface_t {

		le_resource_manager_o *  ( * create    ) ( );
		void                     ( * destroy   ) ( le_resource_manager_o* self );
		void                     ( * update    ) ( le_resource_manager_o* self, le_rendergraph_o* rendergraph);
		void                     ( * add_item  ) ( le_resource_manager_o* self, le_img_resource_handle const * image_handle, le_resource_info_t const * image_info, char const ** arr_image_paths, bool should_watch);
		bool 					 ( * remove_item  ) ( le_resource_manager_o* self, le_img_resource_handle const * image_handle);

		void (*set_decoder_interface_for_filetype)(le_resource_manager_o* self, const char* file_extension, le_image_decoder_interface_t* decoder_interface);
	};

    struct le_resource_manager_private_interface_t {
        void ( *file_watcher_callback )( char const * path, void*user_data );
    };

	le_resource_manager_interface_t       le_resource_manager_i;
	le_resource_manager_private_interface_t le_resource_manager_private_i;
};
// clang-format on

LE_MODULE( le_resource_manager );
LE_MODULE_LOAD_DEFAULT( le_resource_manager );

#ifdef __cplusplus

namespace le_resource_manager {
static const auto& api                   = le_resource_manager_api_i;
static const auto& le_resource_manager_i = api->le_resource_manager_i;
} // namespace le_resource_manager

class LeResourceManager : NoCopy, NoMove {

	le_resource_manager_o* self;

  public:
	LeResourceManager()
	    : self( le_resource_manager::le_resource_manager_i.create() ) {
	}

	~LeResourceManager() {
		le_resource_manager::le_resource_manager_i.destroy( self );
	}

	void update( le_rendergraph_o* rendergraph ) {
		le_resource_manager::le_resource_manager_i.update( self, rendergraph );
	}

	void add_item( le_img_resource_handle const& image_handle, le_resource_info_t const& image_info, char const** arr_image_paths, bool should_watch = false ) {
		le_resource_manager::le_resource_manager_i.add_item( self, &image_handle, &image_info, arr_image_paths, should_watch );
	}

	bool remove_item( le_img_resource_handle const& image_handle ) {
		return le_resource_manager::le_resource_manager_i.remove_item( self, &image_handle );
	}
	void set_decoder_interface_for_filetype( const char* file_extension, le_image_decoder_interface_t* decoder_interface ) {
		le_resource_manager::le_resource_manager_i.set_decoder_interface_for_filetype( self, file_extension, decoder_interface );
	}

	operator auto() {
		return self;
	}
};

#endif // __cplusplus

#endif

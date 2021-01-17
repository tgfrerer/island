#include <vlc/vlc.h>
#include <iostream>
#include <filesystem>

#include <le_renderer/le_renderer.h>
#include <le_pixels/le_pixels.h>
#include <le_resource_manager/le_resource_manager.h>

#include "le_video.h"
#include "le_core/le_core.h"

struct le_video_o {
	libvlc_instance_t *         libvlc = nullptr;
	le_resource_manager_o *     resource_manager;
	const le_resource_handle_t *image_handle;
	libvlc_media_player_t *     player = nullptr;
	le_video_load_params        load_params{};
	le_pixels_o *               pixels;
	le_resource_item_t *        resource_handle;
	uint64_t                    duration = 0;
};

//struct le_video_item_t {
//	le_resource_handle_t   handle;
//	libvlc_media_player_t *player;
//};

static libvlc_instance_t *libvlc;

static int init() {
	libvlc = libvlc_new( 0, nullptr );
	std::cout << "VLC instance created." << std::endl;
	return libvlc != nullptr;
}

static void le_terminate() {
	libvlc_release( libvlc );
	std::cout << "VLC instance terminated." << std::endl;
}

// ----------------------------------------------------------------------

static le_video_o *le_video_create() {
	auto self = new le_video_o();
	return self;
}

static bool le_video_setup( le_video_o *self, le_resource_manager_o *resource_manager, le_resource_handle_t const *image_handle ) {

	self->libvlc           = libvlc;
	self->resource_manager = resource_manager;
	self->image_handle     = image_handle;

	if ( !self->libvlc ) {
		std::cerr << "Error no VLC context set" << std::endl;
		return false;
	}

	return true;
}

// ----------------------------------------------------------------------

static void le_video_destroy( le_video_o *self ) {
	le_pixels::le_pixels_i.destroy( self->pixels );
	delete self;
}

// ------------------------------------------------------- CALLBACKS

static le_video_o *to_video( void *ptr ) {
	return static_cast<le_video_o *>( ptr );
}

static void *cb_lock( void *opaque, void **planes ) {
	auto video = to_video( opaque );
	le_pixels::le_pixels_i.lock( video->pixels );
	*planes = le_pixels::le_pixels_i.get_data( video->pixels );
	return video->pixels;
}

static void cb_unlock( void *opaque, void *picture, void *const *planes ) {
	auto video = to_video( opaque );
	le_pixels::le_pixels_i.unlock( video->pixels );
}

static void cb_display( void *opaque, void *picture ) {
	auto video = to_video( opaque );
	le_resource_manager::le_resource_manager_i.update_pixels( video->resource_manager, video->resource_handle, nullptr );
}

// ----------------------------------------------------------------------

static void le_video_update( le_video_o *self ) {
	// do something with self
}

static bool le_video_load( le_video_o *self, const le_video_load_params &params ) {
	if ( !std::filesystem::exists( params.file_path ) ) {
		std::cerr << "Video does not exist '" << params.file_path << "'" << std::endl;
		return false;
	}

	const char *chroma;
	unsigned    num_channels = 0;

	switch ( params.output_format ) {
		//    case le::Format::eR8G8Uint:
		//        chroma       = "RV32";
		//        num_channels = 2;
		//		break;
	case le::Format::eR8G8B8Unorm:
		chroma       = "RV32";
		num_channels = 3;
		break;
	case le::Format::eR8G8B8A8Unorm:
		chroma       = "RGBA";
		num_channels = 4;
		break;
	default:
		// TODO more formats
		std::cerr << "[le_video] Only eR8G8B8A8Uint or eR8G8B8A8Uint video output format is supported" << std::endl;
		return false;
	}

	auto media = libvlc_media_new_path( self->libvlc, params.file_path );

	// libvlc_media_add_option(media, ":avcodec-hw=none");
	libvlc_media_parse( media );
	//	libvlc_media_parse_with_options(media, )
	self->duration = libvlc_media_get_duration( media );

	self->player = libvlc_media_player_new_from_media( media );

	unsigned width, height;
	libvlc_video_get_size( self->player, 0, &width, &height );

	self->pixels = le_pixels::le_pixels_i.create( int( width ), int( height ), 4, le_pixels_info::eUInt8 );

	std::cout << "[VIDEO INFO] '" << params.file_path << " '" << width << "x" << height << " " << self->duration << "ms" << std::endl;

	// set callbacks

	libvlc_video_set_callbacks( self->player, cb_lock, cb_unlock, cb_display, self );

	//VLC_CODEC_RGBA
	// "RV32"
	libvlc_video_set_format( self->player, chroma, width, height, width * 4 );
	//	libvlc_video_set_format_callbacks( self->player, cb_format, cb_cleanup );

	libvlc_media_player_play( self->player );

	auto image_info =
	    le::ImageInfoBuilder()
	        .setImageType( le::ImageType::e2D )
	        .setExtent( width, height )
	        .build();

	self->resource_handle = le_resource_manager::le_resource_manager_i.add_item_pixels( self->resource_manager, self->image_handle, &image_info, &self->pixels, false );

	libvlc_media_release( media );

	return true;
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_video, api ) {
	auto videoApi  = static_cast<le_video_api *>( api );
	videoApi->init = init;

	auto &le_video_i   = videoApi->le_video_i;
	le_video_i.create  = le_video_create;
	le_video_i.destroy = le_video_destroy;
	le_video_i.setup   = le_video_setup;
	le_video_i.update  = le_video_update;
	le_video_i.load    = le_video_load;
	//	le_video_i.add_item = le_video_add_item;
}

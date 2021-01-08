#include <vlc/vlc.h>
#include <iostream>
#include <filesystem>

#include <le_renderer/le_renderer.h>
#include <le_pixels/le_pixels.h>
#include <cstring>

#include "le_video.h"
#include "le_core/le_core.h"

struct le_video_o {
	libvlc_instance_t *    libvlc = nullptr;
	libvlc_media_player_t *player = nullptr;
	le_video_load_params   load_params{};
	le_pixels_o *          pixels;
	// 	unsigned               width    = 0;
	//	unsigned               height   = 0;
	//	unsigned               bpp      = 0;
	uint64_t duration = 0;
	//    char* buffer
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

	self->libvlc = libvlc;

	if ( !self->libvlc ) {
		std::cerr << "Error no VLC context set" << std::endl;
		return self;
	}

	return self;
}

// ----------------------------------------------------------------------

static void le_video_destroy( le_video_o *self ) {
	delete self->pixels;
	delete self;
}

// ------------------------------------------------------- CALLBACKS

static le_video_o *to_video( void *ptr ) {
	return static_cast<le_video_o *>( ptr );
}

//unsigned cb_format( void **opaque, char *chroma,
//                    unsigned *width, unsigned *height,
//                    unsigned *pitches,
//                    unsigned *lines ) {
//
//	//	chroma = "RGBA";
//	auto player = to_video( *opaque );
//
//	unsigned w = *width;
//	unsigned h = *height;
//
//	if ( w != player->width || h != player->height ) {
//		// allocate pixels
////        size_t pixels =
//	}
//
//	std::cout << chroma << " - " << w << " - " << h << std::endl;
//
//	//	char* target_format = "RGBA";
//	//	chroma = target_format;
//
//	strcpy( chroma, "RGBA" );
//
//	return 1;
//}
//
//void cb_cleanup( void *opaque ) {
//}

static void *cb_lock( void *opaque, void **planes ) {
	auto video = to_video( opaque );
	*planes    = le_pixels_api_i->le_pixels_i.get_data(video->pixels);
	return video->pixels;
}

// get the argb image and save it to a file
static void cb_unlock( void *opaque, void *picture, void *const *planes ) {
	auto video = to_video( opaque );
	//	struct context *ctx        = ( context * )opaque;
	//	unsigned char * data       = ( unsigned char * )*planes;
	//	static int      frameCount = 1;
	//
	//	QImage image( data, VIDEO_WIDTH, VIDEO_HEIGHT, QImage::Format_ARGB32 );
	//	image.save( QString( "frame_%1.png" ).arg( frameCount++ ) );
	//
	//	ctx->mutex.unlock();
}

static void cb_display( void *opaque, void *picture ) {
	auto video = to_video( opaque );
}

// ----------------------------------------------------------------------

static void le_video_update( le_video_o *self ) {
	// do something with self
}

static bool le_video_load( le_video_o *self, const le_video_load_params &params ) {
	// TODO convert formats
	if ( params.output_format != le::Format::eR8G8B8Uint ) {
		std::cerr << "Only eR8G8B8A8Uint video output format is supported" << std::endl;
		return false;
	}

	if ( !std::filesystem::exists( params.file_path ) ) {
		std::cerr << "Video does not exist '" << params.file_path << "'" << std::endl;
		return false;
	}

	auto media = libvlc_media_new_path( self->libvlc, params.file_path );

	//
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
	libvlc_video_set_format( self->player, "RGBA", width, height, width * 4 );
	//	libvlc_video_set_format_callbacks( self->player, cb_format, cb_cleanup );

	libvlc_media_player_play( self->player );

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
	le_video_i.update  = le_video_update;
	le_video_i.load    = le_video_load;
	//	le_video_i.add_item = le_video_add_item;
}

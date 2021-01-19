#include <vlc/vlc.h>
#include <iostream>
#include <filesystem>
#include <atomic>
#include <thread>

#include <le_renderer/le_renderer.h>
#include <le_pixels/le_pixels.h>
#include <le_resource_manager/le_resource_manager.h>

#include "le_video.h"
#include "le_core/le_core.h"
#include "le_log/le_log.h"

struct le_video_o {
	libvlc_instance_t *         libvlc           = nullptr;
	le_log_module_o *           log              = nullptr;
	le_resource_manager_o *     resource_manager = nullptr;
	libvlc_media_player_t *     player           = nullptr;
	le_pixels_o *               pixels           = nullptr;
	uint64_t                    duration         = 0;
	std::atomic_bool            loop             = false;
	le_video_load_params        load_params;
	le_resource_handle_t        image_handle;
};

//struct le_video_item_t {
//	le_resource_handle_t   handle;
//	libvlc_media_player_t *player;
//};

static libvlc_instance_t *libvlc;
//static le_log_module_o *  log;

static int init() {
//	log    = le::get_module( "le_video" );
	libvlc = libvlc_new( 0, nullptr );
//	if ( !libvlc ) {
//		le_log::error( log, "could not initialize" );
//	} else {
//		le_log::info( log, "initialized" );
//	}
	return libvlc != nullptr;
}

static void le_terminate() {
	libvlc_release( libvlc );
//	le_log::info( log, "terminated" );
}

// ----------------------------------------------------------------------

static le_video_o *le_video_create() {
	auto self = new le_video_o();
	return self;
}

static bool le_video_setup( le_video_o *self, le_resource_manager_o *resource_manager, le_resource_handle_t const *image_handle ) {

	self->libvlc           = libvlc;
	self->resource_manager = resource_manager;
	self->image_handle     = *image_handle;

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

// ------------------------------------------------------- CALLBACKS (all these happen on their own thread)

inline le_video_o *to_video( void *ptr ) {
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
	le_resource_manager::le_resource_manager_i.update_pixels( video->resource_manager, &video->image_handle, nullptr );
}

// forward declare this one because we need some core video player functions
static void cb_evt( const libvlc_event_t *event, void *opaque );

// ----------------------------------------------------------------------

static void le_video_update( le_video_o *self ) {
	// do something with self
}

static bool le_video_load( le_video_o *self, const le_video_load_params &params ) {
	if ( !std::filesystem::exists( params.file_path ) ) {
//		le_log::error( log, "Videofile does not exist '%s'", params.file_path );
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
//		le_log::error( log, "Only eR8G8B8A8Uint or eR8G8B8A8Uint video output format is supported" );
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

//	le_log::info( log, "loaded '%s' %dx%d - %dms", params.file_path, width, height, self->duration );

	// set callbacks
	libvlc_video_set_callbacks( self->player, cb_lock, cb_unlock, cb_display, self );

	auto                  event_manager = libvlc_media_player_event_manager( self->player );
	static libvlc_event_e event_types[] = { libvlc_MediaPlayerEncounteredError, libvlc_MediaPlayerPositionChanged,
	                                        libvlc_MediaPlayerEndReached, libvlc_MediaPlayerLengthChanged,
	                                        libvlc_MediaPlayerSeekableChanged, libvlc_MediaPlayerStopped };
	for ( auto event : event_types ) {
		libvlc_event_attach( event_manager, event, cb_evt, self );
	}

	libvlc_video_set_format( self->player, chroma, width, height, width * 4 );

	auto image_info =
	    le::ImageInfoBuilder()
	        .setImageType( le::ImageType::e2D )
	        .setExtent( width, height )
	        .build();

	le_resource_manager::le_resource_manager_i.add_item_pixels( self->resource_manager, &self->image_handle, &image_info, &self->pixels, false );

	libvlc_media_release( media );

	return true;
}

// ----------------------------------------------------------------------

static void le_video_play( le_video_o *self ) {
	libvlc_media_player_play( self->player );
}

// ----------------------------------------------------------------------

static void le_video_pause( le_video_o *self ) {
	libvlc_media_player_pause( self->player );
}

// ----------------------------------------------------------------------

static void le_video_set_loop( le_video_o *self, bool state ) {
	self->loop = state;
}

// ----------------------------------------------------------------------

static void le_video_set_position( le_video_o *self, int64_t position ) {
	auto percent = position / float( self->duration );
	libvlc_media_player_set_position( self->player, percent );
}

// ----------------------------------------------------------------------

static void cb_evt( const libvlc_event_t *event, void *opaque ) {
	auto video = to_video( opaque );
	switch ( event->type ) {
	case libvlc_MediaPlayerEndReached:
		//		if ( video->loop ) {
		//			le_video_play( video );
		//			le_video_set_position( video, 0 );
		//		}
		break;
	case libvlc_MediaPlayerPositionChanged:
		break;
		//le_log::info( log, "%d", event->u.media_player_time_changed.new_time );
	}
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_video, api ) {
	auto videoApi  = static_cast<le_video_api *>( api );
	videoApi->init = init;

	auto &le_video_i        = videoApi->le_video_i;
	le_video_i.create       = le_video_create;
	le_video_i.destroy      = le_video_destroy;
	le_video_i.setup        = le_video_setup;
	le_video_i.update       = le_video_update;
	le_video_i.load         = le_video_load;
	le_video_i.play         = le_video_play;
	le_video_i.pause        = le_video_pause;
	le_video_i.set_position = le_video_set_position;
	le_video_i.set_loop     = le_video_set_loop;
}

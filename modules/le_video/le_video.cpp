#include <vlc/vlc.h>
#include <iostream>

#include <le_renderer/le_renderer.h>

#include "le_video.h"
#include "le_core/le_core.h"

struct le_video_o {
	libvlc_instance_t *libvlc;
};

struct le_video_item_t {
	le_resource_handle_t   handle;
	libvlc_media_player_t *player;
};

// ----------------------------------------------------------------------

static le_video_o *le_video_create() {
	auto self = new le_video_o();

	self->libvlc = libvlc_new( 0, nullptr );
	if ( !self->libvlc ) {
		std::cerr << "Error creating VLC context" << std::endl;
		return self;
	}

	return self;
}

// ----------------------------------------------------------------------

static void le_video_destroy( le_video_o *self ) {
	libvlc_release( self->libvlc );
	delete self;
}

// ----------------------------------------------------------------------

static void le_video_update( le_video_o *self ) {
	// do something with self
}

static le_resource_handle_t le_video_add_item( le_video_o *self, const le_video_create_params &params ) {
	auto path = libvlc_media_new_path( self->libvlc, params.file_path );

	le_video_item_t item{};
	item.player = libvlc_media_player_new_from_media( path );
	libvlc_media_release( path );

    libvlc_video_set_callbacks(self->libvlc, )

	return item.handle;
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_video, api ) {
	auto &le_video_i = static_cast<le_video_api *>( api )->le_video_i;

	le_video_i.create   = le_video_create;
	le_video_i.destroy  = le_video_destroy;
	le_video_i.update   = le_video_update;
	le_video_i.add_item = le_video_add_item;
}

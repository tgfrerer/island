#ifndef GUARD_video_player_example_app_H
#define GUARD_video_player_example_app_H
#endif

#include "le_core.h"

struct video_player_example_app_o;

// clang-format off
struct video_player_example_app_api {

	struct video_player_example_app_interface_t {
		void               ( *initialize  )();
		void               ( *terminate   )();

		video_player_example_app_o * ( *create      )();
		void               ( *destroy     )( video_player_example_app_o *self );

		bool               ( *update      )( video_player_example_app_o *self );

		void (*on_video_playback_complete)(struct le_video_decoder_o* decoder, void* user_data);
	};

	video_player_example_app_interface_t video_player_example_app_i;
};
// clang-format on

LE_MODULE( video_player_example_app );
LE_MODULE_LOAD_DEFAULT( video_player_example_app );

#ifdef __cplusplus

namespace video_player_example_app {
static const auto& api              = video_player_example_app_api_i;
static const auto& video_player_example_app_i = api->video_player_example_app_i;
} // namespace video_player_example_app

class VideoPlayerExampleApp : NoCopy, NoMove {

	video_player_example_app_o* self;

  public:
	VideoPlayerExampleApp()
	    : self( video_player_example_app::video_player_example_app_i.create() ) {
	}

	bool update() {
		return video_player_example_app::video_player_example_app_i.update( self );
	}

	~VideoPlayerExampleApp() {
		video_player_example_app::video_player_example_app_i.destroy( self );
	}

	static void initialize() {
		video_player_example_app::video_player_example_app_i.initialize();
	}

	static void terminate() {
		video_player_example_app::video_player_example_app_i.terminate();
	}
};

#endif

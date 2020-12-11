#ifndef GUARD_le_video_H
#define GUARD_le_video_H

#include "le_core/le_core.h"

struct le_video_o;

// clang-format off
struct le_video_create_params {
    char const *handle_name;
    char const *file_path;
};

struct le_video_api {

	struct le_video_interface_t {

		le_video_o *         ( * create                   ) ( );
		void                 ( * destroy                  ) ( le_video_o* self );
		void                 ( * update                   ) ( le_video_o* self );
        le_resource_handle_t ( * add_item                 ) ( le_video_o* self, const le_video_create_params& params );

	};

	le_video_interface_t       le_video_i;
};
// clang-format on

LE_MODULE( le_video );
LE_MODULE_LOAD_DEFAULT( le_video );

#ifdef __cplusplus

namespace le_video {
static const auto &api        = le_video_api_i;
static const auto &le_video_i = api -> le_video_i;
} // namespace le_video

class LeVideo : NoCopy, NoMove {

	le_video_o *self;

  public:
	LeVideo()
	    : self( le_video::le_video_i.create() ) {
	}

	~LeVideo() {
		le_video::le_video_i.destroy( self );
	}

	void update() {
		le_video::le_video_i.update( self );
	}

	le_resource_handle_t add_item( char const *handle_name, char const *file_path ) {
		return le_video::le_video_i.add_item( self, { handle_name, file_path } );
	}

	operator auto() {
		return self;
	}
};

#endif // __cplusplus

#endif

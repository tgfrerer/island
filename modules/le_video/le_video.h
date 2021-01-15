#ifndef GUARD_le_video_H
#define GUARD_le_video_H

#include <string>

#include "le_core/le_core.h"

struct le_video_o;

// clang-format off
struct le_video_load_params {
    char const *file_path{};
    le::Format output_format{le::Format::eR8G8B8Uint};
};

// forward declaration
struct le_video_item_t;
struct le_resource_manager_o;


struct le_video_api {

	struct le_video_interface_t {
        le_video_o *         ( * create                   ) (  );
		bool                 ( * setup                    ) ( le_video_o* self, le_resource_manager_o* resource_manager );
		void                 ( * destroy                  ) ( le_video_o* self );
		void                 ( * update                   ) ( le_video_o* self );
        bool                 ( * load                     ) ( le_video_o* self, const le_video_load_params &params );
        void                 ( * play                     ) ( le_video_o* self );
	};

    int           ( *init                       ) ();

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

namespace le {

class Video : NoCopy, NoMove {

	le_video_o *self;

  public:
	static int init() {
		return le_video::api->init();
	}

	Video()
	    : self( le_video::le_video_i.create() ) {
	}

	~Video() {
		le_video::le_video_i.destroy( self );
	}

	bool setup( le_resource_manager_o *resource_manager ) {
		return le_video::le_video_i.setup( self, resource_manager );
	}

	void update() {
		le_video::le_video_i.update( self );
	}

	bool load( const std::string &path ) {
		return le_video::le_video_i.load( self, { path.c_str() } );
	}

	operator auto() {
		return self;
	}
};

} // namespace le

#endif // __cplusplus

#endif

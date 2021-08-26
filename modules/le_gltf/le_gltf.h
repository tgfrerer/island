#ifndef GUARD_le_gltf_H
#define GUARD_le_gltf_H

#include <stdint.h>
#include "le_core.h"

struct le_gltf_o;
struct le_stage_o;

// clang-format off
struct le_gltf_api {
	
	struct le_gltf_interface_t {

		le_gltf_o *     ( *create  ) ( char const * file_path );
		void            ( *destroy ) ( le_gltf_o *self );
		bool            ( *import  ) ( le_gltf_o* self, le_stage_o* stage);

	};

	le_gltf_interface_t le_gltf_i;
};
// clang-format on

LE_MODULE( le_gltf );
LE_MODULE_LOAD_DEFAULT( le_gltf );

#ifdef __cplusplus

namespace le_gltf {
static const auto &api       = le_gltf_api_i;
static const auto &le_gltf_i = api -> le_gltf_i;
} // namespace le_gltf

class LeGltf : NoCopy, NoMove {

	le_gltf_o *self;

  public:
	LeGltf( char const *path )
	    : self( le_gltf::le_gltf_i.create( path ) ) {
	}

	bool import( le_stage_o *stage ) {
		return le_gltf::le_gltf_i.import( self, stage );
	}

	~LeGltf() {
		le_gltf::le_gltf_i.destroy( self );
	}

	operator auto() {
		return self;
	}
};

#endif // __cplusplus

#endif

#ifndef GUARD_le_gltf_H
#define GUARD_le_gltf_H

#include <stdint.h>
#include "le_core/le_core.hpp"

#ifdef __cplusplus
extern "C" {
#endif

struct le_gltf_o;
struct le_stage_o;

void register_le_gltf_api( void *api );

// clang-format off
struct le_gltf_api {
	static constexpr auto id      = "le_gltf";
	static constexpr auto pRegFun = register_le_gltf_api;

	struct le_gltf_interface_t {

		le_gltf_o *( *create ) ( char const * file_path );
		void       ( *destroy )( le_gltf_o *self );

		bool (*import)(le_gltf_o* self, le_stage_o* stage);

	};

	le_gltf_interface_t le_gltf_i;
};
// clang-format on

#ifdef __cplusplus
} // extern c

namespace le_gltf {
#	ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<le_gltf_api>( true );
#	else
const auto api = Registry::addApiStatic<le_gltf_api>();
#	endif

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

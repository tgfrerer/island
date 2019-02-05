#ifndef GUARD_le_font_H
#define GUARD_le_font_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#ifdef __cplusplus
extern "C" {
#endif

struct le_font_o;

void register_le_font_api( void *api );

// clang-format off
struct le_font_api {
	static constexpr auto id      = "le_font";
	static constexpr auto pRegFun = register_le_font_api;

	struct le_font_interface_t {

		le_font_o *			 ( * create                   ) ( );
		void                 ( * destroy                  ) ( le_font_o* self );
	};

	le_font_interface_t       le_font_i;
};
// clang-format on

#ifdef __cplusplus
} // extern c

namespace le_font {
#	ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<le_font_api>( true );
#	else
const auto api = Registry::addApiStatic<le_font_api>();
#	endif

static const auto &le_font_i = api -> le_font_i;

} // namespace le_font

class LeFont : NoCopy, NoMove {

	le_font_o *self;

  public:
	LeFont()
	    : self( le_font::le_font_i.create() ) {
	}

	~LeFont() {
		le_font::le_font_i.destroy( self );
	}
};

#endif // __cplusplus

#endif

#ifndef GUARD_le_tessellator_H
#define GUARD_le_tessellator_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#define ISL_ALLOW_GLM_TYPES
// Life is terrible without 3d type primitives, so let's include some glm forward declarations
#ifdef ISL_ALLOW_GLM_TYPES
#	include <glm/fwd.hpp>
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct le_tessellator_o;

void register_le_tessellator_api( void *api );

// clang-format off
struct le_tessellator_api {
	static constexpr auto id      = "le_tessellator";
	static constexpr auto pRegFun = register_le_tessellator_api;

	struct le_tessellator_interface_t {

		typedef uint16_t IndexType;
		typedef glm::vec2 VertexType;

		le_tessellator_o *   ( * create                   ) ( );
		void                 ( * destroy                  ) ( le_tessellator_o* self );

		void                 ( * add_polyline             ) ( le_tessellator_o* self, VertexType const * const pPoints, size_t const& pointCount );
		bool                 ( * tessellate               ) ( le_tessellator_o* self );
		void                 ( * get_indices              ) ( le_tessellator_o* self, IndexType const ** pIndices, size_t * indexCount );
		void                 ( * reset                    ) ( le_tessellator_o* self );

	};

	le_tessellator_interface_t       le_tessellator_i;
};
// clang-format on

#ifdef __cplusplus
} // extern c

namespace le_tessellator {
#	ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<le_tessellator_api>( true );
#	else
const auto api = Registry::addApiStatic<le_tessellator_api>();
#	endif

static const auto &le_tessellator_i = api -> le_tessellator_i;

} // namespace le_tessellator

class LeTessellator : NoCopy, NoMove {

	le_tessellator_o *self;

  public:
	LeTessellator()
	    : self( le_tessellator::le_tessellator_i.create() ) {
	}

	~LeTessellator() {
		le_tessellator::le_tessellator_i.destroy( self );
	}

	operator auto() {
		return self;
	}
};

#endif // __cplusplus

#endif

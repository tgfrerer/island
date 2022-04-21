#ifndef GUARD_le_tessellator_H
#define GUARD_le_tessellator_H

#include "le_core.h"

#ifdef __cplusplus
#	include <glm/fwd.hpp>
#endif

struct le_tessellator_o;

// clang-format off
struct le_tessellator_api {

	typedef uint16_t IndexType;

	struct le_tessellator_interface_t {

		static constexpr auto OptionsWindingsOffset = 3;

		enum Options : uint64_t {
			// Flip one or more bits for options.
			bitUseEarcutTessellator             = 1 << 0, // use earcut over libtess, libtess being default
			bitConstrainedDelaunayTriangulation = 1 << 1, /* ignored if tessellator not libtess */
			bitReverseContours                  = 1 << 2, /* ignored if tessellator not libtess */
			// Pick *one* of the following winding modes;
			// For a description of winding modes, see: <http://www.glprogramming.com/red/chapter11.html>
			eWindingOdd                         = 0 << OptionsWindingsOffset, /* ignored if tessellator not libtess */
			eWindingNonzero                     = 1 << OptionsWindingsOffset, /* ignored if tessellator not libtess */
			eWindingPositive                    = 3 << OptionsWindingsOffset, /* ignored if tessellator not libtess */
			eWindingNegative                    = 4 << OptionsWindingsOffset, /* ignored if tessellator not libtess */
			eWindingAbsGeqTwo                   = 5 << OptionsWindingsOffset, /* ignored if tessellator not libtess */
		};


		le_tessellator_o *   ( * create                   ) ( );
		void                 ( * destroy                  ) ( le_tessellator_o* self );

		void                 ( * set_options              ) ( le_tessellator_o* self, uint64_t options);
		void                 ( * add_polyline             ) ( le_tessellator_o* self, glm::vec2 const * const pPoints, size_t const& pointCount );
		void                 ( * get_indices              ) ( le_tessellator_o* self, IndexType const ** pIndices, size_t * indexCount );
		void                 ( * get_vertices             ) ( le_tessellator_o* self, glm::vec2 const ** pVertices, size_t * vertexCount );

		bool                 ( * tessellate               ) ( le_tessellator_o* self );

		void                 ( * reset                    ) ( le_tessellator_o* self );

	};

	le_tessellator_interface_t       le_tessellator_i;
};
// clang-format on

LE_MODULE( le_tessellator );
LE_MODULE_LOAD_DEFAULT( le_tessellator );

#ifdef __cplusplus

namespace le_tessellator {
static const auto& api              = le_tessellator_api_i;
static const auto& le_tessellator_i = api -> le_tessellator_i;
using Options                       = le_tessellator_api::le_tessellator_interface_t::Options;
} // namespace le_tessellator

class LeTessellator : NoCopy, NoMove {

	le_tessellator_o* self;

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

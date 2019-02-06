#include "le_tessellator.h"
#include "pal_api_loader/ApiRegistry.hpp"

#include "./3rdparty/earcut.hpp/include/mapbox/earcut.hpp"

#include <glm/vec2.hpp>

using Point     = le_tessellator_api::le_tessellator_interface_t::VertexType;
using IndexType = le_tessellator_api::le_tessellator_interface_t::IndexType;

namespace mapbox {
namespace util {

template <>
struct nth<0, Point> {
	inline static auto get( const Point &p ) noexcept {
		return p.x;
	}
};
template <>
struct nth<1, Point> {
	inline static auto get( const Point &p ) noexcept {
		return p.y;
	}
};

} // namespace util
} // namespace mapbox

struct le_tessellator_o {
	std::vector<std::vector<Point>> polygon;
	std::vector<IndexType>          indices;
};

// ----------------------------------------------------------------------

static le_tessellator_o *le_tessellator_create() {
	auto self = new le_tessellator_o();
	return self;
}

// ----------------------------------------------------------------------

static void le_tessellator_destroy( le_tessellator_o *self ) {
	delete self;
}

// ----------------------------------------------------------------------

static void le_tessellator_add_polyline( le_tessellator_o *self, Point const *const pPoints, size_t const &pointCount ) {
	self->polygon.insert( self->polygon.end(), {pPoints, pPoints + pointCount} );
}
static bool le_tessellator_tessellate( le_tessellator_o *self ) {

	// Run tessellation

	self->indices = mapbox::earcut<IndexType>( self->polygon );

	return true;
}

static void le_tessellator_get_indices( le_tessellator_o *self, IndexType const **pIndices, size_t *indexCount ) {
	*pIndices   = self->indices.data();
	*indexCount = self->indices.size();
}

static void le_tessellator_reset( le_tessellator_o *self ) {
	self->polygon.clear();
	self->indices.clear();
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_tessellator_api( void *api ) {
	auto &le_tessellator_i = static_cast<le_tessellator_api *>( api )->le_tessellator_i;

	le_tessellator_i.create       = le_tessellator_create;
	le_tessellator_i.destroy      = le_tessellator_destroy;
	le_tessellator_i.add_polyline = le_tessellator_add_polyline;
	le_tessellator_i.tessellate   = le_tessellator_tessellate;
	le_tessellator_i.get_indices  = le_tessellator_get_indices;
	le_tessellator_i.reset        = le_tessellator_reset;
}

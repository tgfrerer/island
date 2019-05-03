#include "le_tessellator.h"
#include "pal_api_loader/ApiRegistry.hpp"

#include "./3rdparty/earcut.hpp/include/mapbox/earcut.hpp"
#include "tesselator.h"

#include <string.h> // memcpy
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
	std::vector<std::vector<Point>> contours;
	std::vector<IndexType>          indices;
	std::vector<Point>              vertices;
	uint64_t                        options;
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
	// Add new contour
	self->contours.insert( self->contours.end(), {pPoints, pPoints + pointCount} );

	// append to vertices
	self->vertices.insert( self->vertices.end(), pPoints, pPoints + pointCount );
}

// ----------------------------------------------------------------------

static bool le_tessellator_tessellate( le_tessellator_o *self ) {

	// Run tessellation
	if ( self->options & le_tessellator::Options::bitUseEarcutTessellator ) {
		// Use earcut tessellator
		self->indices = mapbox::earcut<IndexType>( self->contours );
	} else {
		// Use libtess
		TESStesselator *tess;
		tess = tessNewTess( nullptr );

		tessSetOption( tess, TessOption::TESS_CONSTRAINED_DELAUNAY_TRIANGULATION,
		               self->options & le_tessellator::Options::bitConstrainedDelaunayTriangulation );

		tessSetOption( tess, TessOption::TESS_REVERSE_CONTOURS,
		               self->options & le_tessellator::Options::bitReverseContours );

		for ( auto const &contour : self->contours ) {
			tessAddContour( tess, Point::type::length(), contour.data(), sizeof( Point ), int( contour.size() ) );
		}

		tessTesselate( tess,
		               int( self->options >> le_tessellator_api::le_tessellator_interface_t::OptionsWindingsOffset ),
		               TessElementType::TESS_POLYGONS,
		               3, // max number of vertices per polygon - we want triangles.
		               Point::length(),
		               nullptr );

		self->indices.clear();
		self->vertices.clear();

		size_t numVertices = size_t( tessGetVertexCount( tess ) );
		auto   pVertices   = tessGetVertices( tess );
		self->vertices.resize( numVertices );
		memcpy( self->vertices.data(), pVertices, sizeof( Point ) * numVertices );

		size_t numIndices = size_t( tessGetElementCount( tess ) ) * 3; // each element has 3 vertices, as we requested triangles when tessellating
		self->indices.reserve( numIndices );

		TESSindex const *      pIndex     = tessGetElements( tess );
		TESSindex const *const pIndex_end = pIndex + numIndices;

		// we must copy manually since indices are int, but we want uint16_t

		for ( auto idx = pIndex; idx != pIndex_end; idx++ ) {
			self->indices.emplace_back( *idx );
		}

		tessDeleteTess( tess );
	}

	return true;
}

// ----------------------------------------------------------------------

static void le_tessellator_get_indices( le_tessellator_o *self, IndexType const **pIndices, size_t *indexCount ) {
	*pIndices   = self->indices.data();
	*indexCount = self->indices.size();
}

// ----------------------------------------------------------------------

static void le_tessellator_get_vertices( le_tessellator_o *self, Point const **pVertices, size_t *vertexCount ) {
	*pVertices   = self->vertices.data();
	*vertexCount = self->vertices.size();
}

// ----------------------------------------------------------------------

static void le_tessellator_reset( le_tessellator_o *self ) {
	self->contours.clear();
	self->indices.clear();
	self->vertices.clear();
}

// ----------------------------------------------------------------------

static void le_tessellator_set_options( le_tessellator_o *self, uint64_t options ) {
	self->options = options;
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_tessellator_api( void *api ) {
	auto &le_tessellator_i = static_cast<le_tessellator_api *>( api )->le_tessellator_i;

	le_tessellator_i.create       = le_tessellator_create;
	le_tessellator_i.destroy      = le_tessellator_destroy;
	le_tessellator_i.add_polyline = le_tessellator_add_polyline;
	le_tessellator_i.tessellate   = le_tessellator_tessellate;
	le_tessellator_i.get_indices  = le_tessellator_get_indices;
	le_tessellator_i.get_vertices = le_tessellator_get_vertices;
	le_tessellator_i.reset        = le_tessellator_reset;
	le_tessellator_i.set_options  = le_tessellator_set_options;
}

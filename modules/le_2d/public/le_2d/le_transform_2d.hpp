#pragma once
#include "glm/glm.hpp"

/// Structure representing Affine Transform
///
/// Think of this as "first translate, then rotate(transform)"
///
struct LeTransform2D {

	glm::mat2 transform = {
	    { 1, 0 }, // x axis
	    { 0, 1 }, // y axis
	}; // 2x2 matrix, column major

	glm::vec2 translation = { 0, 0 };

	// ----------

	inline static LeTransform2D make_transform_rotation_rad( float angle_rad ) {
		float       cosa  = cosf( angle_rad );
		float       sina  = sinf( angle_rad );
		return {
		    .transform = {
		        { cosa, sina },
		        { -sina, cosa },
		    },
		    .translation = { 0, 0 },
		};
	};

	/// pre-multiplication operator
	inline LeTransform2D operator*( LeTransform2D const& rhs ) const {
		// Note: this has been checked against vello to
		// make sure that we're using the same conventions.
		auto const& t = this->transform;
		auto const& o = rhs.transform;
		return {
		    t * o,
		    t * rhs.translation + this->translation };
	}

	/// apply transform to a vector
	inline glm::vec2 operator*( glm::vec2 const& rhs ) const {
		// Note: this has been checked against vello to
		// make sure that we're using the same conventions.
		auto const& t = this->transform;
		return { t * rhs + this->translation };
	}

	inline LeTransform2D inverse() const {

		float inv_det = 1.f / glm::determinant( this->transform );

		assert( inv_det == inv_det ); // test for NaN

		auto result = LeTransform2D{
		    .transform{
		        // calculate inverse of transform
		        inv_det * transform[ 1 ][ 1 ],
		        -inv_det * transform[ 0 ][ 1 ],
		        -inv_det * transform[ 1 ][ 0 ],
		        inv_det * transform[ 0 ][ 0 ],
		    },
		    .translation{
		        // reverse translation
		        inv_det * ( transform[ 1 ][ 0 ] * translation[ 1 ] - transform[ 1 ][ 1 ] * translation[ 0 ] ),
		        inv_det * ( transform[ 0 ][ 1 ] * translation[ 0 ] - transform[ 0 ][ 0 ] * translation[ 1 ] ),
		    },
		};

		return result;
	};

	const bool operator==( LeTransform2D const& rhs ) const {
		return ( this->transform == rhs.transform && this->translation == rhs.translation );
	}

	const bool operator!=( LeTransform2D const& rhs ) const {
		return !( *this == rhs );
	}
};

namespace le {
using Transform2D = LeTransform2D;
}

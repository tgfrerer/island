#pragma once
#include "glm/glm.hpp"

// Think of this as "first translate, then rotate(transform)"
struct LeTransform2D {
	glm::mat2 transform   = { 1, 0, 0, 1 }; // 2x2 matrix, column major
	glm::vec2 translation = { 0, 0 };

	inline static LeTransform2D make_rotation_rad( float angle_rad ) {
		float       cosa  = cosf( angle_rad );
		float       sina  = sinf( angle_rad );
		LeTransform2D rot_m = { .transform = { cosa, sina, -sina, cosa }, .translation = { 0, 0 } };
		return rot_m;
	};

	inline LeTransform2D operator*( LeTransform2D const& rhs ) const {
		// Note: this has been checked against vello to
		// make sure that we're using the same conventions.
		auto const& t = this->transform;
		auto const& o = rhs.transform;
		return {
		    {
		        t * o
		        // transform
		        // t[ 0 ][ 0 ] * o[ 0 ][ 0 ] + t[ 1 ][ 0 ] * o[ 0 ][ 1 ],
		        // t[ 0 ][ 1 ] * o[ 0 ][ 0 ] + t[ 1 ][ 1 ] * o[ 0 ][ 1 ],
		        // t[ 0 ][ 0 ] * o[ 1 ][ 0 ] + t[ 1 ][ 0 ] * o[ 1 ][ 1 ],
		        // t[ 0 ][ 1 ] * o[ 1 ][ 0 ] + t[ 1 ][ 2 ] * o[ 1 ][ 1 ],
		    },
		    {
		        t * rhs.translation + this->translation
		        // translation
		        // t[ 0 ][ 0 ] * rhs.translation[ 0 ] + t[ 1 ][ 0 ] * rhs.translation[ 1 ] + this->translation[ 0 ],
		        // t[ 0 ][ 1 ] * rhs.translation[ 0 ] + t[ 1 ][ 1 ] * rhs.translation[ 1 ] + this->translation[ 1 ],
		    },
		};
	}

	// inline float determinant() const {
	// 	return transform[ 0 ][ 0 ] * transform[ 1 ][ 1 ] - transform[ 0 ][ 1 ] * transform[ 1 ][ 0 ];
	// }

	inline LeTransform2D inverse() const {

		float inv_det = 1.f / glm::determinant( this->transform );

		// float inv_det = 1.0 / determinant();
		assert( inv_det == inv_det ); // test for NaN

		auto result = LeTransform2D{
		    .transform{
		        inv_det * transform[ 1 ][ 1 ],
		        -inv_det * transform[ 0 ][ 1 ],
		        -inv_det * transform[ 1 ][ 0 ],
		        inv_det * transform[ 0 ][ 0 ],
		    },
		    .translation{
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

#ifndef GUARD_LE_HASH_UTIL_H
#define GUARD_LE_HASH_UTIL_H

#include <stdint.h>

// TODO: const_char_hash64 and fnv_hash64 must return the same hash value
// for the same string - currently, this is not the case, most probably,
// because the recursive function does hashing in the opposite direction

// ----------------------------------------------------------------------
// FNV hash using constexpr recursion over char string length (may execute at compile time)
inline uint64_t constexpr const_char_hash64( const char *input ) noexcept {
	return *input ? ( 0x100000001b3 * const_char_hash64( input + 1 ) ) ^ static_cast<uint64_t>( *input ) : 0xcbf29ce484222325;
}

//template <typename T>
//inline uint64_t fnv_hash64( const T &input_, uint64_t num_bytes_ ) noexcept {
//	const char *input = reinterpret_cast<const char *>( &input_ );
//	uint64_t    hash  = 0xcbf29ce484222325;
//	// note that we iterate backwards - this is so that the hash matches
//	// the constexpr version which uses recursion.
//	for ( const char *p = input + num_bytes_; p != input; ) {
//		hash = ( 0x100000001b3 * hash ) ^ static_cast<uint64_t>( *( --p ) );
//	}
//	return hash;
//}

inline uint32_t constexpr const_char_hash32( const char *input ) noexcept {
	return *input ? ( 0x1000193 * const_char_hash32( input + 1 ) ) ^ static_cast<uint32_t>( *input ) : 0x811c9dc5;
}

struct IdentityHash {
	auto operator()( const uint64_t &key_ ) const noexcept {
		return key_;
	}
};

//#define RESOURCE_IMAGE_ID( x ) \
//	const_char_hash64( "resource-image-" x )

//#define RESOURCE_TEXTURE_ID( x ) \
//	const_char_hash64( "resource-texture-" x )

//#define RESOURCE_IMAGE_VIEW_ID( x ) \
//	const_char_hash64( "resource-imageview-" x )

//#define RESOURCE_BUFFER_ID( x ) \
//	const_char_hash64( "resource-buffer-" x )

#endif

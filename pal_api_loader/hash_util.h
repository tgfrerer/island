#ifndef GUARD_LE_HASH_UTIL_H
#define GUARD_LE_HASH_UTIL_H

#include <stdint.h>

constexpr uint32_t FNV1A_VAL_32_CONST   = 0x811c9dc5;
constexpr uint32_t FNV1A_PRIME_32_CONST = 0x1000193;
constexpr uint64_t FNV1A_VAL_64_CONST   = 0xcbf29ce484222325;
constexpr uint64_t FNV1A_PRIME_64_CONST = 0x100000001b3;

// adapted from: https://notes.underscorediscovery.com/constexpr-fnv1a/
inline constexpr uint64_t hash_64_fnv1a_const( const char *const str, const uint64_t value = FNV1A_VAL_64_CONST ) noexcept {
	return ( *str ) ? hash_64_fnv1a_const( str + 1, ( value ^ uint64_t( *str ) ) * FNV1A_PRIME_64_CONST ) : value;
}

// adapted from: https://notes.underscorediscovery.com/constexpr-fnv1a/
inline constexpr uint32_t hash_32_fnv1a_const( const char *const str, const uint32_t value = FNV1A_VAL_32_CONST ) noexcept {
	return ( *str ) ? hash_32_fnv1a_const( str + 1, ( value ^ uint32_t( *str ) ) * FNV1A_PRIME_32_CONST ) : value;
}

// ----------------------------------------------------------------------

// adapted from: https://notes.underscorediscovery.com/constexpr-fnv1a/
inline uint64_t hash_64_fnv1a( char const *const input ) noexcept {

	uint64_t           hash  = FNV1A_VAL_64_CONST;
	constexpr uint64_t prime = FNV1A_PRIME_64_CONST;

	for ( char const *i = input; *i != 0; ++i ) {
		uint8_t value = static_cast<const uint8_t &>( *i );
		hash          = hash ^ value;
		hash          = hash * prime;
	}

	return hash;

} //hash_64_fnv1a

// ----------------------------------------------------------------------
// adapted from: https://notes.underscorediscovery.com/constexpr-fnv1a/
inline uint32_t hash_32_fnv1a( char const *const input ) noexcept {

	uint32_t           hash  = FNV1A_VAL_32_CONST;
	constexpr uint32_t prime = FNV1A_PRIME_32_CONST;

	for ( char const *i = input; *i != 0; ++i ) {
		uint8_t value = static_cast<const uint8_t &>( *i );
		hash          = hash ^ value;
		hash          = hash * prime;
	}

	return hash;

} //hash_32_fnv1a

// ----------------------------------------------------------------------
struct IdentityHash {
	auto operator()( const uint64_t &key_ ) const noexcept {
		return key_;
	}
};

#endif

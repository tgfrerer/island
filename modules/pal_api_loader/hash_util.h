#ifndef GUARD_LE_HASH_UTIL_H
#define GUARD_LE_HASH_UTIL_H

#include <stdint.h>

constexpr uint32_t FNV1A_VAL_32_CONST   = 0x811c9dc5;
constexpr uint32_t FNV1A_PRIME_32_CONST = 0x1000193;
constexpr uint64_t FNV1A_VAL_64_CONST   = 0xcbf29ce484222325;
constexpr uint64_t FNV1A_PRIME_64_CONST = 0x100000001b3;

// Returns a compile-time calculated 64 bit fnv hash for a given constant string.
// Adapted from: https://notes.underscorediscovery.com/constexpr-fnv1a/
inline constexpr uint64_t hash_64_fnv1a_const( const char *const str, const uint64_t value = FNV1A_VAL_64_CONST ) noexcept {
	return ( *str ) ? hash_64_fnv1a_const( str + 1, ( value ^ uint64_t( *str ) ) * FNV1A_PRIME_64_CONST ) : value;
}

// Returns a compile-time calculated 32 bit fnv hash for a given constant string.
// Adapted from: https://notes.underscorediscovery.com/constexpr-fnv1a/
inline constexpr uint32_t hash_32_fnv1a_const( const char *const str, const uint32_t value = FNV1A_VAL_32_CONST ) noexcept {
	return ( *str ) ? hash_32_fnv1a_const( str + 1, ( value ^ uint32_t( *str ) ) * FNV1A_PRIME_32_CONST ) : value;
}

// ----------------------------------------------------------------------

// Adapted from: https://notes.underscorediscovery.com/constexpr-fnv1a/
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
// Adapted from: https://notes.underscorediscovery.com/constexpr-fnv1a/
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

#ifndef NDEBUG

ISL_API_ATTR void update_argument_name_table( const char *source, uint64_t value );

// Shader argument names are internally stored / looked up as their hashes.
// We define alias method for shader argument name so that we may decide to
// point it to a different hashing algorithm at a later time.
#	define LE_ARGUMENT_NAME( x ) []() -> uint64_t {									\
	static uint64_t hash_value = 0;													\
	if (hash_value != 0) return hash_value;											\
	hash_value = hash_64_fnv1a( x );												\
	update_argument_name_table(x, hash_value);										\
	return hash_value; }()

#else

// Shader argument names are internally stored / looked up as their hashes.
// We define alias method for shader argument name so that we may decide to
// point it to a different hashing algorithm at a later time.
#	define LE_ARGUMENT_NAME( x ) hash_64_fnv1a_const( x )

#endif

// ----------------------------------------------------------------------
// Returns itself value of key as hash value; useful if you
// want to enforce key and hash value to be identical.
struct IdentityHash {
	auto const &operator()( const uint64_t &key_ ) const noexcept {
		return key_;
	}
};

#endif

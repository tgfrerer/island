#ifndef LE_RESOURCE_HANDLE_T_INL
#define LE_RESOURCE_HANDLE_T_INL

#include <stdint.h>
#include <string>
#include <le_renderer/le_renderer.h>

struct le_buf_resource_usage_flags_t {
	enum FlagBits : uint8_t {
		eIsUnset   = 0,
		eIsVirtual = 1u << 0,
		eIsStaging = 1u << 1,
	};
};

struct le_img_resource_usage_flags_t {
	enum FlagBits : uint8_t {
		eIsUnset = 0,
		eIsRoot  = 1u << 0, // whether image, when used as a render target, is flagged as a root resource to the rendergraph
	};
};

struct le_resource_handle_data_t {
	LeResourceType        type;                        // type controls which of the following fields are used.
	uint8_t               num_samples      = 0;        // number of samples log 2 if image
	uint8_t               flags            = 0;        // bitfield of either buffer - or img_resource_useage_flags;
	uint16_t              index            = 0;        // allocator index if virtual buffer
	le_resource_handle_t *reference_handle = nullptr;  // if auto-generated from another handle, we keep a reference to the parent.
	char                  debug_name[ 48 ] = { '\0' }; // space for 47 chars + \0

	bool operator==( le_resource_handle_data_t const &rhs ) const noexcept {

		for ( char const *c = debug_name, *d = rhs.debug_name; *c != 0; c++, d++ ) {
			if ( *c != *d ) {
				return false;
			}
		}

		return type == rhs.type &&
		       num_samples == rhs.num_samples &&
		       flags == rhs.flags &&
		       index == rhs.index &&
		       reference_handle == rhs.reference_handle;
	}
	bool operator!=( le_resource_handle_data_t const &rhs ) const noexcept {
		return !operator==( rhs );
	}
};

// FIXME: we need an equality operator for this.

// todo: use fnvhash

struct le_resource_handle_data_hash {

	//	static constexpr uint64_t FNV1A_PRIME_64_CONST = 0x100000001b3;

	inline uint64_t operator()( le_resource_handle_data_t const &key ) const noexcept {
		uint64_t hash = FNV1A_VAL_64_CONST;

		uint8_t value = 0;
		for ( int i = 0; i != 8; i++ ) {
			value = ( ( uint64_t )key.reference_handle >> ( i * 8 ) ) & 0xff;
			hash  = hash ^ value;
			hash  = hash * FNV1A_PRIME_64_CONST;
		}

		value = key.num_samples;
		hash  = hash ^ value;
		hash  = hash * FNV1A_PRIME_64_CONST;

		value = key.flags;
		hash  = hash ^ value;
		hash  = hash * FNV1A_PRIME_64_CONST;

		value = ( key.index >> 8 ) & 0xff;
		hash  = hash ^ value;
		hash  = hash * FNV1A_PRIME_64_CONST;

		value = key.index & 0xff;
		hash  = hash ^ value;
		hash  = hash * FNV1A_PRIME_64_CONST;

		for ( char const *i = key.debug_name; *i != 0; ++i ) {
			uint8_t value = static_cast<const uint8_t &>( *i );
			hash          = hash ^ value;
			hash          = hash * FNV1A_PRIME_64_CONST;
		}
		return hash;
	}
};

#endif
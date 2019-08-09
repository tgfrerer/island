#ifndef GUARD_le_ecs_H
#define GUARD_le_ecs_H

#include <stdint.h>
#include "pal_api_loader/ApiRegistry.hpp"

#ifdef __cplusplus
extern "C" {
#endif

struct le_ecs_o;
typedef struct EntityId_T *EntityId;
typedef struct SystemId_T *LeEcsSystemId;

void register_le_ecs_api( void *api );

// clang-format off
struct le_ecs_api {
	static constexpr auto id      = "le_ecs";
	static constexpr auto pRegFun = register_le_ecs_api;

	struct ComponentType {
		uint64_t type_hash;   // we want a unique id per type.
		const char* type_id;
		uint32_t num_bytes; // number of bytes as in sizeof(), this includes padding.
	};

	typedef void ( *system_fn )( void **read_params, void **write_params, void* user_data );

	struct le_ecs_interface_t {

		le_ecs_o * ( * create            ) ( );
		void       ( * destroy           ) ( le_ecs_o* self );
		EntityId   ( * entity_create     ) ( le_ecs_o *self );
		
		// Returns pointer to data allocated for component.
		// Store data to ecs using this pointer.
		// This may re-allocate component storage, and invalidate pointers and iterators to components held inside the ecs.
		void* (*entity_add_component)( le_ecs_o *self, EntityId entity_id, ComponentType const & component_type );
		
		LeEcsSystemId  (*system_create)( le_ecs_o *self );

		void (*system_set_method)(le_ecs_o*self, LeEcsSystemId system_id, system_fn fn, void * user_data);
		bool (*system_add_write_component)( le_ecs_o *self, LeEcsSystemId system_id, ComponentType const &component_type );
		bool (*system_add_read_component)( le_ecs_o *self, LeEcsSystemId system_id, ComponentType const &component_type );

		void (*execute_system)( le_ecs_o *self, LeEcsSystemId system_id ) ;

	};

	le_ecs_interface_t       le_ecs_i;
};
// clang-format on

#ifdef __cplusplus
} // extern c

namespace le_ecs {
#	ifdef PLUGINS_DYNAMIC
const auto api = Registry::addApiDynamic<le_ecs_api>( true );
#	else
const auto api = Registry::addApiStatic<le_ecs_api>();
#	endif

static const auto &le_ecs_i = api -> le_ecs_i;

} // namespace le_ecs

class LeEcs : NoCopy, NoMove {

	le_ecs_o *self;

  public:
	LeEcs()
	    : self( le_ecs::le_ecs_i.create() ) {
	}

	~LeEcs() {
		le_ecs::le_ecs_i.destroy( self );
	}

	EntityId createEntity() {
		return le_ecs::le_ecs_i.entity_create( self );
	}

	template <typename T>
	void entity_add_component( EntityId entity_id, const T &component ) {
		le_ecs_api::ComponentType ct;
		ct.type_id   = T::type_id;
		ct.type_hash = hash_64_fnv1a_const( T::type_id );
		ct.num_bytes = sizeof( T );
		// Placement new component into memory allocated by ecs.
		void *mem = le_ecs::le_ecs_i.entity_add_component( self, entity_id, ct );
		if ( mem ) {
			new ( mem )( T ){component}; // placement new, then copy
		} else {
			// ERROR
			assert( false );
		}
	}

	LeEcsSystemId create_system() {
		return le_ecs::le_ecs_i.system_create( self );
	}

	void system_set_method( LeEcsSystemId system_id, le_ecs_api::system_fn fn, void *user_data ) {
		le_ecs::le_ecs_i.system_set_method( self, system_id, fn, user_data );
	}

	template <typename T>
	bool system_add_read_component( LeEcsSystemId system_id ) {
		constexpr le_ecs_api::ComponentType ct{
		    hash_64_fnv1a_const( T::type_id ),
		    T::type_id,
		    sizeof( T )};
		return le_ecs::le_ecs_i.system_add_read_component( self, system_id, ct );
	}

	template <typename R, typename S, typename... T>
	bool system_add_read_component( LeEcsSystemId system_id ) {
		bool result = true;
		result &= system_add_read_component<R>( system_id );
		result &= system_add_read_component<S, T...>( system_id );
		return result;
	}

	template <typename T>
	bool system_add_write_component( LeEcsSystemId system_id ) {
		constexpr le_ecs_api::ComponentType ct{
		    hash_64_fnv1a_const( T::type_id ),
		    T::type_id,
		    sizeof( T )};
		return le_ecs::le_ecs_i.system_add_write_component( self, system_id, ct );
	}

	template <typename R, typename S, typename... T>
	bool system_add_write_component( LeEcsSystemId system_id ) {
		bool result = true;
		result &= system_add_write_component<R>( system_id );
		result &= system_add_write_component<S, T...>( system_id );
		return result;
	}

	void update_system( LeEcsSystemId system_id ) {
		le_ecs::le_ecs_i.execute_system( self, system_id );
	}

	operator auto() {
		return self;
	}
};

#endif // __cplusplus

#endif

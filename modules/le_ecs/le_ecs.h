#ifndef GUARD_le_ecs_H
#define GUARD_le_ecs_H

#include "le_core.h"
#include "assert.h" // FIXME: we shouldn't include this here.

struct le_ecs_o;
typedef struct EntityId_T* EntityId;
typedef struct SystemId_T* LeEcsSystemId;

// clang-format off
struct le_ecs_api {
	
    struct ComponentType {
		uint64_t type_hash;   // we want a unique id per type.
		char const * type_id;
		uint32_t num_bytes; // number of bytes as in sizeof(), this includes padding.
	};

	typedef void ( *system_fn )( EntityId entity, void const **read_params, void **write_params, void* user_data );

	struct le_ecs_interface_t {

		le_ecs_o * ( * create            ) ( );
		void       ( * destroy           ) ( le_ecs_o* self );

		EntityId   ( * entity_create     ) ( le_ecs_o *self );
		void       ( * entity_remove     ) ( le_ecs_o *self, EntityId entity);
		
		// Returns pointer to data allocated for component.
		// Store data to ecs using this pointer.
		// This may re-allocate component storage, and invalidate pointers and iterators to components held inside the ecs.
		void* ( *entity_component_at       )( le_ecs_o *self, EntityId entity_id, ComponentType const & component_type );
		void  ( *entity_remove_component   )( le_ecs_o *self, EntityId entity_id, ComponentType const & component_type );

		LeEcsSystemId  ( *system_create    )( le_ecs_o *self );

		void (* system_set_method          )( le_ecs_o*self, LeEcsSystemId system_id, system_fn fn);
		bool (* system_add_write_component )( le_ecs_o *self, LeEcsSystemId system_id, ComponentType const &component_type );
		bool (* system_add_read_component  )( le_ecs_o *self, LeEcsSystemId system_id, ComponentType const &component_type );

		// TODO: we should probaly name all write components read/write components,
		// as it appears that write implies read.

		void ( *execute_system             )( le_ecs_o *self, LeEcsSystemId system_id, void* user_data ) ;

		
	};

	le_ecs_interface_t       le_ecs_i;
};
// clang-format on

LE_MODULE( le_ecs );
LE_MODULE_LOAD_DEFAULT( le_ecs );

#ifdef __cplusplus

#	define LE_ECS_FLAG_COMPONENT( TypeName )          \
		struct TypeName {                              \
			static constexpr auto type_id = #TypeName; \
		}

#	define LE_ECS_COMPONENT( TypeName ) \
		struct TypeName {                \
			static constexpr auto type_id = #TypeName

#	define LE_ECS_COMPONENT_CLOSE() \
		}

// Helper macros to define system callback signatures
#	define LE_ECS_READ_WRITE_PARAMS EntityId entity, void const **read_c, void **write_c
#	define LE_ECS_WRITE_ONLY_PARAMS EntityId entity, void const **, void **write_c
#	define LE_ECS_READ_ONLY_PARAMS EntityId entity, void const **read_c, void **

// use this inside a system callback to fetch write parameter
#	define LE_ECS_GET_WRITE_PARAM( index, param_type ) \
		static_cast<param_type*>( write_c[ index ] )

// use this inside a system callback to fetch read parameter
#	define LE_ECS_GET_READ_PARAM( index, param_type ) \
		static_cast<param_type const*>( read_c[ index ] )

namespace le_ecs {
static const auto& api      = le_ecs_api_i;
static const auto& le_ecs_i = api -> le_ecs_i;
} // namespace le_ecs

class LeEcs : NoCopy, NoMove {
	le_ecs_o* self;

  public:
	LeEcs()
	    : self( le_ecs::le_ecs_i.create() ) {
	}

	~LeEcs() {
		le_ecs::le_ecs_i.destroy( self );
	}

	// -- entity

	inline EntityId create_entity();
	inline void     remove_entity( EntityId entity );

	// -- component

	template <typename T>
	inline bool entity_add_component( EntityId entity_id, const T&& component );

	template <typename T>
	inline void entity_remove_component( EntityId entity_id );

	class EntityBuilder {
		LeEcs&   parent;
		EntityId id;

	  public:
		EntityBuilder( LeEcs& parent_ )
		    : parent( parent_ )
		    , id( parent.create_entity() ) {
		}

		template <typename T>
		EntityBuilder& add_component( const T&& component ) {
			parent.entity_add_component( id, static_cast<const T&&>( component ) );
			return *this;
		}

		EntityId build() {
			return id;
		}
	};

	EntityBuilder entity() {
		return static_cast<EntityBuilder&&>( EntityBuilder{ *this } );
	}

	// Access data for component of a given entity
	template <typename T>
	T& entity_component_get( EntityId entity_id );

	// -- systems

	inline LeEcsSystemId create_system();

	inline void system_set_method( LeEcsSystemId system_id, le_ecs_api::system_fn fn );

	template <typename T>
	inline bool system_add_read_component( LeEcsSystemId system_id );

	template <typename R, typename S, typename... T>
	inline bool system_add_read_component( LeEcsSystemId system_id );

	template <typename T>
	inline bool system_add_write_component( LeEcsSystemId system_id );

	template <typename R, typename S, typename... T>
	inline bool system_add_write_component( LeEcsSystemId system_id );

	inline void update_system( LeEcsSystemId system_id, void* user_data );

	class SystemBuilder {
		LeEcs&        parent;
		LeEcsSystemId id;

	  public:
		SystemBuilder( LeEcs& parent_ )
		    : parent( parent_ )
		    , id( parent.create_system() ) {
		}

		template <typename T>
		SystemBuilder& add_read_components() {
			auto result = parent.system_add_read_component<T>( id );
			assert( result );
			return *this;
		}

		template <typename R, typename S, typename... T>
		SystemBuilder& add_read_components() {
			parent.system_add_read_component<R, S, T...>( id );
			return *this;
		}

		template <typename T>
		SystemBuilder& add_write_components() {
			auto result = parent.system_add_write_component<T>( id );
			assert( result );
			return *this;
		}

		template <typename R, typename S, typename... T>
		SystemBuilder& add_write_components() {
			parent.system_add_write_component<R, S, T...>( id );
			return *this;
		}

		LeEcsSystemId build() {
			return id;
		}
	};

	SystemBuilder system() {
		return static_cast<SystemBuilder&&>( SystemBuilder( *this ) );
	};

	// -- ecs

	inline operator le_ecs_o*() {
		return self;
	}
};

// ----------------------------------------------------------------------
// Fetches component type struct for component - this should happen at
// compile time.
template <typename T>
constexpr le_ecs_api::ComponentType const le_ecs_get_component_type() {
	// Note that this calculates the correct size for flag structs, which are empty.
	// - in c++ empty structs may use memory, whilst in c, they have zero size.
	//
	// we force a c-style size calculation by pretending to extend the empty struct by an int,
	// then removing the size of int. This will give us the correct size 0 for
	// empty structs.
	struct A : T {
		int i;
	};
	constexpr uint32_t                  component_size = uint32_t( sizeof( A ) - sizeof( int ) );
	constexpr le_ecs_api::ComponentType ct{ hash_64_fnv1a_const( T::type_id ), T::type_id, component_size };
	return static_cast<le_ecs_api::ComponentType const>( ct );
}
// ----------------------------------------------------------------------

EntityId LeEcs::create_entity() {
	return le_ecs::le_ecs_i.entity_create( self );
}

// ----------------------------------------------------------------------

void LeEcs::remove_entity( EntityId entity ) {
	le_ecs::le_ecs_i.entity_remove( self, entity );
}

template <typename T>
T& LeEcs::entity_component_get( EntityId entity_id ) {
	return *static_cast<T*>( le_ecs::le_ecs_i.entity_component_at( self, entity_id, le_ecs_get_component_type<T>() ) );
};
// ----------------------------------------------------------------------

LeEcsSystemId LeEcs::create_system() {
	return le_ecs::le_ecs_i.system_create( self );
}

// ----------------------------------------------------------------------

void LeEcs::system_set_method( LeEcsSystemId system_id, le_ecs_api::system_fn fn ) {
	le_ecs::le_ecs_i.system_set_method( self, system_id, fn );
}

// ----------------------------------------------------------------------

void LeEcs::update_system( LeEcsSystemId system_id, void* user_data ) {
	le_ecs::le_ecs_i.execute_system( self, system_id, user_data );
}

// ----------------------------------------------------------------------

template <typename R, typename S, typename... T>
bool LeEcs::system_add_write_component( LeEcsSystemId system_id ) {
	bool result = true;
	result &= system_add_write_component<R>( system_id );
	result &= system_add_write_component<S, T...>( system_id );
	return result;
}

// ----------------------------------------------------------------------

template <typename R, typename S, typename... T>
bool LeEcs::system_add_read_component( LeEcsSystemId system_id ) {
	bool result = true;
	result &= system_add_read_component<R>( system_id );
	result &= system_add_read_component<S, T...>( system_id );
	return result;
}

// ----------------------------------------------------------------------

template <typename T>
bool LeEcs::system_add_read_component( LeEcsSystemId system_id ) {
	constexpr auto ct = le_ecs_get_component_type<T>();
	return le_ecs::le_ecs_i.system_add_read_component( self, system_id, ct );
}

// ----------------------------------------------------------------------

template <typename T>
bool LeEcs::system_add_write_component( LeEcsSystemId system_id ) {
	constexpr auto ct = le_ecs_get_component_type<T>();
	return le_ecs::le_ecs_i.system_add_write_component( self, system_id, ct );
}

// ----------------------------------------------------------------------

template <typename T>
bool LeEcs::entity_add_component( EntityId entity_id, const T&& component ) {

	// Allocate memory inside the ECS for our component.
	constexpr auto ct  = le_ecs_get_component_type<T>();
	void*          mem = le_ecs::le_ecs_i.entity_component_at( self, entity_id, ct );

	if ( ct.num_bytes != 0 && nullptr != mem ) {
		new ( mem )( T ){ component }; // placement new
		return true;
	} else {
		// ERROR
		assert( ct.num_bytes == 0 && "if component size > 0 then memory must have been allocated" );
		return false;
	}
}

// ----------------------------------------------------------------------

template <typename T>
void LeEcs::entity_remove_component( EntityId entity_id ) {
	constexpr auto ct = le_ecs_get_component_type<T>();
	le_ecs::le_ecs_i.entity_remove_component( self, entity_id, ct );
}
#endif // __cplusplus

#endif

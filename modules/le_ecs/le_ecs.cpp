#include "le_ecs.h"
#include "pal_api_loader/ApiRegistry.hpp"

#include <array>
#include <vector>
#include <bitset>
#include "pal_api_loader/hash_util.h"

struct ComponentStorage {
	std::vector<uint8_t> storage;
};

static constexpr size_t MAX_COMPONENT_TYPES = 32;

using system_fn       = le_ecs_api::system_fn;
using ComponentType   = le_ecs_api::ComponentType;        //
using ComponentFilter = std::bitset<MAX_COMPONENT_TYPES>; // each bit corresponds to a component type and an index in le_ecs_o::components
// if bit is set this means that entity has-a component of this type

struct System {
	ComponentFilter readComponents;  // read always before write
	ComponentFilter writeComponents; //

	std::vector<size_t> read_component_indices;  // indices into component storage/component type
	std::vector<size_t> write_component_indices; // indices into component storage/component type

	system_fn fn; // we must cast params back to struct of entities' components
	void *    user_data;
};

struct le_ecs_o {
	std::vector<ComponentType>    component_types;
	std::vector<ComponentStorage> component_storage; // one store per component type
	std::vector<ComponentFilter>  entities;          // each entity may be different, index corresponds to entity ID
	std::vector<System>           systems;
};

// ----------------------------------------------------------------------

static le_ecs_o *le_ecs_create() {
	auto self = new le_ecs_o();
	return self;
}

// ----------------------------------------------------------------------

static void le_ecs_destroy( le_ecs_o *self ) {
	delete self;
}

// ----------------------------------------------------------------------

static size_t get_index_from_entity_id( EntityId id ) {
	return reinterpret_cast<size_t>( id );
}

// ----------------------------------------------------------------------

static EntityId get_entity_id_from_index( size_t idx ) {
	return reinterpret_cast<EntityId>( idx );
}

// ----------------------------------------------------------------------

static size_t get_index_from_sytem_id( LeEcsSystemId id ) {
	return reinterpret_cast<size_t>( id );
}

static LeEcsSystemId get_system_id_from_index( size_t idx ) {
	return reinterpret_cast<LeEcsSystemId>( idx );
}

size_t le_ecs_find_component_type_index( le_ecs_o const *self, ComponentType const &component_type ) {
	// Find component storage index
	size_t     storage_index       = 0;
	auto const component_types_end = self->component_types.data() + self->component_types.size();
	for ( auto s_type = self->component_types.data(); s_type != component_types_end; storage_index++, s_type++ ) {
		if ( s_type->type_hash == component_type.type_hash ) {
			break;
		}
	}
	return storage_index;
}
// ----------------------------------------------------------------------

static void *le_ecs_entity_add_component( le_ecs_o *self, EntityId entity_id, ComponentType const &component_type ) {

	// Find if entity exists
	size_t e_idx = get_index_from_entity_id( entity_id );

	if ( e_idx >= self->entities.size() ) {
		// ERROR: entity does not exist.
		return nullptr;
	}

	auto &entity = self->entities.at( e_idx );

	// -- Does component of this type already exist in component storage?

	// Find component storage index
	size_t storage_index = le_ecs_find_component_type_index( self, component_type );

	// if storage_index == size of component_types, we must add a new component type

	if ( storage_index == self->component_types.size() ) {
		self->component_types.push_back( component_type );
		self->component_storage.push_back( {} );
		self->component_storage.back().storage.reserve( 4096 ); // reserve 1 page of memory, just in case.
	}

	// Find if a component with this type (at this storage_index) already exists with this entity
	// we do this to make sure that entities have only one component of each distinct type.
	// This is important as we filter components by type, and there would be no way of distinguishing
	// between components of the same type

	if ( entity.test( storage_index ) ) {
		// ERROR A  component of this type has already been added to this entity.
		return nullptr;
	} else {
		// All good: Add flag to mark that entity has component of type component_type
		entity[ storage_index ] = true;
	}

	auto &component_storage = self->component_storage[ storage_index ].storage;

	// we must add memory (allocate) to the relevant component storage

	size_t start_offset = component_storage.size(); // Store byte index where new memory will be placed.

	component_storage.insert(
	    component_storage.end(),
	    component_type.num_bytes,
	    0 ); // zero-initialize data

	return &component_storage[ start_offset ];
}

// ----------------------------------------------------------------------
// create a new, empty entity
static EntityId le_ecs_entity_create( le_ecs_o *self ) {
	self->entities.push_back( {} ); // add a new, empty entity
	return get_entity_id_from_index( self->entities.size() - 1 );
}

// ----------------------------------------------------------------------

static LeEcsSystemId le_ecs_system_create( le_ecs_o *self ) {
	self->systems.push_back( {
	    0,
	    0,
	    {},
	    {},
	    {},
	    nullptr,
	} );
	return get_system_id_from_index( self->systems.size() - 1 );
}

// ----------------------------------------------------------------------

static void le_ecs_system_set_method( le_ecs_o *self, LeEcsSystemId system_id, system_fn fn, void *user_data ) {

	size_t system_index = get_index_from_sytem_id( system_id );

	assert( system_index < self->systems.size() );

	// --------| invariant: system with this index exists.

	auto &system = self->systems[ system_index ];

	system.fn        = fn;
	system.user_data = user_data;
}

// ----------------------------------------------------------------------

// adds a component type as a read parameter to system
static bool le_ecs_system_add_read_component( le_ecs_o *self, LeEcsSystemId system_id, ComponentType const &component_type ) {

	// check if component type exists as a type in ecs - we do this by finding it's index.

	size_t storage_index = le_ecs_find_component_type_index( self, component_type );

	if ( storage_index == self->component_types.size() ) {
		return false;
	}

	// --------| invariant: storage type was found

	size_t system_index = get_index_from_sytem_id( system_id );

	if ( system_index >= self->systems.size() ) {
		return false;
	}

	// --------| invariant: system with this index exists.

	auto &system = self->systems[ system_index ];

	// we mark the the component to be used.

	system.readComponents[ storage_index ] = true;
	system.read_component_indices.push_back( storage_index );

	return true;
}

// ----------------------------------------------------------------------

// adds a component type as a read parameter to system
static bool le_ecs_system_add_write_component( le_ecs_o *self, LeEcsSystemId system_id, ComponentType const &component_type ) {

	// check if component type exists as a type in ecs - we do this by finding it's index.

	size_t storage_index = le_ecs_find_component_type_index( self, component_type );

	if ( storage_index == self->component_types.size() ) {
		return false;
	}

	// --------| invariant: storage type was found

	size_t system_index = get_index_from_sytem_id( system_id );

	if ( system_index >= self->systems.size() ) {
		return false;
	}

	// --------| invariant: system with this index exists.

	auto &system = self->systems[ system_index ];

	// we mark the the component to be used.

	system.writeComponents[ storage_index ] = true;
	system.write_component_indices.push_back( storage_index );

	return true;
}

// ----------------------------------------------------------------------

static void le_ecs_execute_system( le_ecs_o *self, LeEcsSystemId system_id ) {

	// Filter all entities - we only want those which provide all the component types which our system
	// cares about.

	// System's function is called on matching components which together form part of an entity.
	// Function call happens repeatedly over all matching entities.

	auto &system = self->systems.at( get_index_from_sytem_id( system_id ) );

	if ( system.fn == nullptr ) {
		// if system does not define callable function there is
		// we can return early.
		return;
	}

	// --------| invariant: system provides callable function

	const size_t types_count = self->component_types.size(); // total number of types/component stores inside this ecs.

	auto required_components = ( system.readComponents | system.writeComponents );

	// Find indices for required components

	std::array<size_t, MAX_COMPONENT_TYPES>       component_iterators; // current offset as object counts into component storage at index
	std::array<void const *, MAX_COMPONENT_TYPES> read_containers;
	std::array<void *, MAX_COMPONENT_TYPES>       write_containers;

	component_iterators.fill( 0 );
	read_containers.fill( nullptr );
	write_containers.fill( nullptr );

	for ( auto &e : self->entities ) {

		// We must test if all required components are present in current entity.

		auto matching_components = ( e & ( required_components ) );

		if ( matching_components == 0 ) {
			// if no needed components are present, we can safely jump over this entity.
			continue;
		}

		if ( matching_components != required_components ) {

			// If some but not all are present, we must make sure that the ones which were
			// present do get iterated over, otherwise successive components of that type
			// won't match up with their respective entity.
			//
			// This is only because we use sparse storage for our components, meaning we
			// don't leave holes in our component storage, just because an entity doesn't
			// use a specific type of component.

			// -- find out which components were present
			// -- inrease component_iterators for these components by 1

			for ( size_t i = 0; i != types_count; ++i ) {
				if ( matching_components[ i ] ) {
					component_iterators[ i ]++;
				}
			}
			continue;
		}

		// ---------| Invariant: all required components are present

		// group relevant components into structure which may be used
		size_t read_containter_count  = 0;
		size_t write_containter_count = 0;

		for ( auto &read_component : system.read_component_indices ) {
			read_containers[ read_containter_count++ ] =
			    ( self->component_storage[ read_component ].storage.data() +
			      self->component_types[ read_component ].num_bytes *
			          component_iterators[ read_component ] );
		}
		for ( auto &write_component : system.write_component_indices ) {
			write_containers[ write_containter_count++ ] =
			    ( self->component_storage[ write_component ].storage.data() +
			      self->component_types[ write_component ].num_bytes *
			          component_iterators[ write_component ] );
		}

		// this is where we call the function

		system.fn( read_containers.data(), write_containers.data(), system.user_data );

		// now we must increase component iterators for all elements which were
		// named in required_components

		for ( size_t i = 0; i != types_count; ++i ) {
			if ( matching_components[ i ] ) {
				component_iterators[ i ]++;
			}
		}
	}
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_ecs_api( void *api ) {
	auto &le_ecs_i = static_cast<le_ecs_api *>( api )->le_ecs_i;

	le_ecs_i.create               = le_ecs_create;
	le_ecs_i.destroy              = le_ecs_destroy;
	le_ecs_i.entity_create        = le_ecs_entity_create;
	le_ecs_i.entity_add_component = le_ecs_entity_add_component;

	le_ecs_i.system_create              = le_ecs_system_create;
	le_ecs_i.system_add_read_component  = le_ecs_system_add_read_component;
	le_ecs_i.system_set_method          = le_ecs_system_set_method;
	le_ecs_i.system_add_write_component = le_ecs_system_add_write_component;

	le_ecs_i.execute_system = le_ecs_execute_system;
}

#include "le_ecs.h"
#include "le_core/le_core.h"
#include "le_core/hash_util.h"

#include <array>
#include <vector>
#include <bitset>
#include "assert.h"
#include <algorithm>

/* Note
 * 
 * Because we don't use sparse storage for our component data, we must iterate
 * over all entities previous to the entity which we want to access (seek).
 * 
 * Generally, this is not all too bad, as systems will iterate over entities in sequence. 
 * 
 * But removing entities becomes rather costly, as each entity removal operation means 
 * a seek for each component data type affected, plus one or more vector erase operations.
 *
 * 
 * CAVEAT:
 * 
 * Do not add or remove components from within systems, as this will invalidate arrays.
 * This effectively means: Do not access the le_ecs_i interface from within a system 
 * callback. 
 * 
 * this is a common limitation of ECS and a strategy around this is to record any changes
 * which you may want to apply from iniside the system, and apply these changes from the 
 * main (controlling) thread. This works similar to a command buffer. 
 *  
 */

struct ComponentStorage {
	std::vector<uint8_t> storage; // raw data
};

static constexpr size_t MAX_COMPONENT_TYPES = 128;

using system_fn       = le_ecs_api::system_fn;
using ComponentType   = le_ecs_api::ComponentType;        //
using ComponentFilter = std::bitset<MAX_COMPONENT_TYPES>; // each bit corresponds to a component type and an index in le_ecs_o::components
// if bit is set this means that entity has-a component of this type

struct Entity {
	uint64_t        id; //unique id
	ComponentFilter filter;
};

struct System {
	ComponentFilter readComponents;  // read always before write
	ComponentFilter writeComponents; //

	std::vector<size_t> read_component_indices;  // indices into component storage/component type
	std::vector<size_t> write_component_indices; // indices into component storage/component type

	system_fn fn; // we must cast params back to struct of entities' components
	void *    user_data;
};

struct le_ecs_o {
	uint64_t                      next_entity_id = 0; // next available entity index (internal)
	std::vector<ComponentType>    component_types;    // index corresponds to ComponentFilter[index]
	std::vector<ComponentStorage> component_storage;  // one store per component type
	std::vector<Entity>           entities;           // each entity may be different, index corresponds to entity ID, sorted by entity.id
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

static inline size_t get_index_from_entity_id( le_ecs_o const *self, EntityId id ) {
	Entity search_entity;
	search_entity.id = reinterpret_cast<size_t>( id );

	auto found_element = std::lower_bound(
	    self->entities.begin(), self->entities.end(), search_entity,
	    []( Entity const &lhs, Entity const &rhs )
	        -> bool { return lhs.id < rhs.id; } );

	assert( found_element != self->entities.end() &&
	        found_element->id == search_entity.id );

	// index is pointer diff found_element - start

	return ( found_element - self->entities.begin() );
}

// ----------------------------------------------------------------------

static inline EntityId entity_get_entity_id( Entity const &e ) {
	return reinterpret_cast<EntityId>( e.id );
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
// Iterate over all entities, accumulating offset for component at storage_index
inline uint32_t seek_offset( std::vector<Entity> const &entities, size_t e_idx, size_t storage_index, uint32_t stride ) {

	uint32_t offset = 0;

	for ( size_t i = 0; i != e_idx; ++i ) {
		if ( entities[ i ].filter.test( storage_index ) ) {
			offset += stride;
		}
	}

	return offset;
}

// ----------------------------------------------------------------------

static size_t le_ecs_produce_component_type_index( le_ecs_o *self, ComponentType const &component_type ) {

	size_t storage_index = le_ecs_find_component_type_index( self, component_type );

	if ( storage_index == self->component_types.size() ) {

		// Component storage for this component type does not yet exist, we must add it

		self->component_types.push_back( component_type );
		self->component_storage.push_back( {} );

		if ( component_type.num_bytes > 0 ) {
			self->component_storage.back().storage.reserve( 4096 ); // reserve 1 page of memory, just in case.
		}
	}
	return storage_index;
}

// ----------------------------------------------------------------------
// access component storage for entity based on component type
// if entity doesn't yet have storage for given component type, storage is created.
// if component type is not yet known to ecs the component type is added to list of known component types.
static void *le_ecs_entity_component_at( le_ecs_o *self, EntityId entity_id, ComponentType const &component_type ) {

	// Find if entity exists
	size_t e_idx = get_index_from_entity_id( self, entity_id );

	if ( e_idx >= self->entities.size() ) {
		// ERROR: entity does not exist.
		return nullptr;
	}

	auto &entity = self->entities.at( e_idx );

	// -- Does component of this type already exist in component storage?
	size_t component_type_index = le_ecs_produce_component_type_index( self, component_type );

	if ( 0 == component_type.num_bytes ) {
		// If component type is empty (a flag-only component), then we set the flag and return early.
		entity.filter[ component_type_index ] = true;
		return nullptr; // signal that no memory has been allocated.
	}

	// ----------| Invariant: Component is not flag-only

	bool     needs_search     = true;
	bool     needs_allocation = true;
	uint32_t offset           = 0;

	if ( entity.filter.test( component_type_index ) ) {
		// A component of this type was already present - we must return
		// the current memory address for the component, and make sure
		// not to allocate any more memory.
		needs_search     = true;
		needs_allocation = false;
	} else {
		entity.filter[ component_type_index ] = true;
		needs_allocation                      = true;
	}

	// If our entity is the last entity in the list of entities,
	// our component memory will also be at the end of the corresponding
	// component storage. This means in that case we don't have to search.

	if ( e_idx == self->entities.size() - 1 ) {
		needs_search = false;
	}

	auto &component_storage = self->component_storage[ component_type_index ].storage;

	if ( needs_search == false ) {
		offset = uint32_t( component_storage.size() );
	} else {
		offset = seek_offset( self->entities, e_idx, component_type_index, self->component_types[ component_type_index ].num_bytes );
	}

	if ( needs_allocation ) {
		component_storage.insert( component_storage.begin() + offset, component_type.num_bytes, 0 ); // zero-initialize data
	}

	return &component_storage[ offset ];
}

// ----------------------------------------------------------------------

static void entity_at_index_remove_component( le_ecs_o *self, size_t e_idx, ComponentType const &component_type ) {
	// Find component storage index
	size_t storage_index = le_ecs_find_component_type_index( self, component_type );

	if ( storage_index == self->component_types.size() ) {
		// component does not exist
		return;
	}

	auto &entity = self->entities.at( e_idx );

	if ( false == entity.filter[ storage_index ] ) {
		return;
	}

	// ----------| Invariant: entity does not have such a component.

	if ( 0 != component_type.num_bytes ) {

		// -- If component has allocated storage, we must find it, and free it.
		// We must iterate through all entities up until our current entity.
		// If any entity has a component of our type, we must add to offset
		// so that we may skip over it when deleting the data for our component.
		uint32_t stride = component_type.num_bytes;

		uint32_t offset = seek_offset( self->entities, e_idx, storage_index, stride );

		auto &storage = self->component_storage[ storage_index ].storage;
		storage.erase( storage.begin() + offset, storage.begin() + offset + stride );
	}

	// -- Remove flag which indicates that component is part of entity
	entity.filter[ storage_index ] = false;
}

// ----------------------------------------------------------------------
// removes component from entity.
static void le_ecs_entity_remove_component( le_ecs_o *self, EntityId entity_id, ComponentType const &component_type ) {

	// Find if entity exists
	size_t e_idx = get_index_from_entity_id( self, entity_id );

	if ( e_idx >= self->entities.size() ) {
		// ERROR: entity does not exist.
		return;
	}

	entity_at_index_remove_component( self, e_idx, component_type );
}

// ----------------------------------------------------------------------
// create a new, empty entity
static EntityId le_ecs_entity_create( le_ecs_o *self ) {
	size_t this_entity_id = self->next_entity_id;
	self->next_entity_id++;
	Entity new_entity{};
	new_entity.id = this_entity_id;
	self->entities.emplace_back( new_entity ); // add a new, empty entity
	return reinterpret_cast<EntityId>( this_entity_id );
}

// ----------------------------------------------------------------------
// Remove entity from ecs.
// this first removes any components, then the entity entry.
static void le_ecs_entity_remove( le_ecs_o *self, EntityId entity_id ) {
	// Find if entity exists
	size_t e_idx = get_index_from_entity_id( self, entity_id );

	if ( e_idx >= self->entities.size() ) {
		// ERROR: entity does not exist.
		return;
	}

	auto const &entity = self->entities.at( e_idx );

	// -- iterate to correct position at all components which use this entity

	for ( uint32_t i = 0; i != MAX_COMPONENT_TYPES; ++i ) {
		if ( entity.filter[ i ] ) {
			entity_at_index_remove_component( self, e_idx, self->component_types[ i ] );
		}
		if ( entity.filter.none() ) {
			break;
		}
	}

	assert( entity.filter.none() && "entity must have no components left" );

	self->entities.erase( self->entities.begin() + uint32_t( e_idx ) );
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

	size_t storage_index = le_ecs_produce_component_type_index( self, component_type );

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

// adds a component type as a write parameter to system
static bool le_ecs_system_add_write_component( le_ecs_o *self, LeEcsSystemId system_id, ComponentType const &component_type ) {

	// check if component type exists as a type in ecs - we do this by finding it's index.

	size_t storage_index = le_ecs_produce_component_type_index( self, component_type );

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

		auto matching_components = ( e.filter & ( required_components ) );

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
			// -- increase component_iterators for these components by 1

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

		system.fn( entity_get_entity_id( e ), read_containers.data(), write_containers.data(), system.user_data );

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

LE_MODULE_REGISTER_IMPL( le_ecs, api ) {
	auto &le_ecs_i = static_cast<le_ecs_api *>( api )->le_ecs_i;

	le_ecs_i.create  = le_ecs_create;
	le_ecs_i.destroy = le_ecs_destroy;

	le_ecs_i.entity_create           = le_ecs_entity_create;
	le_ecs_i.entity_remove           = le_ecs_entity_remove;
	le_ecs_i.entity_component_at     = le_ecs_entity_component_at;
	le_ecs_i.entity_remove_component = le_ecs_entity_remove_component;

	le_ecs_i.system_create              = le_ecs_system_create;
	le_ecs_i.system_add_read_component  = le_ecs_system_add_read_component;
	le_ecs_i.system_set_method          = le_ecs_system_set_method;
	le_ecs_i.system_add_write_component = le_ecs_system_add_write_component;

	le_ecs_i.execute_system = le_ecs_execute_system;
}

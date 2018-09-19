#include "simple_module.h"
#include "pal_api_loader/ApiRegistry.hpp"

#include "iostream"
#include "iomanip"
#include <chrono>
#include <thread>
#include <bitset>
#include <vector>
#include <assert.h>

constexpr size_t MAX_NUM_LAYER_RESOURCES = 64;
using BitField                           = std::bitset<MAX_NUM_LAYER_RESOURCES>;

enum AccessType : int {
	eAccessTypeRead      = 0b01,
	eAccessTypeWrite     = 0b10,
	eAccessTypeReadWrite = eAccessTypeRead | eAccessTypeWrite,
};

struct Layer {
	BitField reads;
	BitField writes;
};

struct simple_module_o {
};

// ----------------------------------------------------------------------

static simple_module_o *simple_module_create() {
	auto self = new simple_module_o();
	return self;
}

// ----------------------------------------------------------------------

static void simple_module_destroy( simple_module_o *self ) {
	delete self;
}

// ----------------------------------------------------------------------

/// Note: `sortIndices` must point to an array of `numLayers` elements of type uint32_t
static void layers_resolve_dependencies( Layer const *const layers, const size_t numLayers, uint32_t *sortIndices ) {

	BitField read_accum{};
	BitField write_accum{};

	/// Each bit in the layer bitfield stands for one resource.
	/// Bitfield index corresponds to a resource id. Note that
	/// bitfields are indexed right-to left (index zero is right-most).

	bool needs_barrier = false;

	uint32_t sortIndex = 0;

	{
		Layer const *const layers_end = layers + numLayers;
		uint32_t *         layerO     = sortIndices;
		for ( Layer const *layer = layers; layer != layers_end; layer++, layerO++ ) {

			BitField read_write = ( layer->reads & layer->writes ); // read_after write in same layer - this means a layer boundary if it does touch any previously read or written elements

			// A barrier is needed, if:
			needs_barrier = ( read_accum & read_write ).any() ||     // - any previously read elements are touched by read-write, OR
			                ( write_accum & read_write ).any() ||    // - any previously written elements are touched by read-write, OR
			                ( write_accum & layer->reads ).any() ||  // - the current layer wants to read from a previously written layer, OR
			                ( write_accum & layer->writes ).any() || // - the current layer writes to a previously written resource, OR
			                ( read_accum & layer->writes ).any();    // - the current layer wants to write to a layer which was previously read.

			// print out debug information
			std::cout << "Needs barrier: " << ( needs_barrier ? "true" : "false" ) << std::endl
			          << std::flush;

			if ( needs_barrier ) {
				++sortIndex;         // Barriers are expressed by increasing the sortIndex. Layers with the same sortIndex *may* execute concurrently.
				read_accum.reset();  // Barriers apply everything before the current layer
				write_accum.reset(); //
				needs_barrier = false;
			}

			write_accum |= layer->writes;
			read_accum |= layer->reads;

			*layerO = sortIndex;
		}
	}

	// print out debug information
	//	std::cout << "Needs barrier: " << ( needs_barrier ? "true" : "false" ) << std::endl
	//	          << std::flush;

	//	std::cout << layer->reads << " reads" << std::endl
	//	          << std::flush;
	//	std::cout << read_accum << " read accum" << std::endl
	//	          << std::flush;
	//	std::cout << write_accum << " write accum" << std::endl
	//	          << std::flush;
}

/// When we add a Resource to the dependency tracker,
///
/// 0. we first must know the current layer
/// 1. we compare the resource id against resources already known to the tracker
/// 2. if resource is known, we use the index into the ids vector to identify the resource
/// 3. we update the current layer at the index of the current resource based on the resource's read or write access.
///
/// This means there is a lookup into a vector of resources for every resource which we add to the layer - this vector may grow, so we always just store indices.
///
struct dependency_manager_o {
	std::array<uint64_t, MAX_NUM_LAYER_RESOURCES> knownResources; // Stores all known resources over all layers, provides us with canonical indices for each known resource
	size_t                                        knownResourcesCount = 0;
	std::vector<Layer>                            layers;             // r/w information for each layer. `Layer::reads` and `Layer::writes` bitfield indices correspond to `knownResources` indices
	std::vector<uint32_t>                         layers_sort_order;  // Sort order for layers
	std::vector<std::string>                      layers_debug_names; // debug name for each layer(optional), but must be same element count as layers.
};

static dependency_manager_o *dependency_manager_create() {
	auto self = new dependency_manager_o();
	return self;
}

static void dependency_manager_destroy( dependency_manager_o *self ) {
	delete self;
}

static void dependency_manager_reset( dependency_manager_o *self ) {

	// NOTE: we don't have to explicitly reset the contents of self->knownResources, since self->knownResourcesCount
	// keeps track of the number of valid elements in the array.
	self->knownResourcesCount = 0;
	self->layers.clear();
	self->layers_sort_order.clear();
	self->layers_debug_names.clear();
}

static void dependency_manager_add_resource( dependency_manager_o *self, uint64_t resourceID, uint8_t access_type ) {
	// - Check if resource is in known resources
	// - If not, add to known resources

	size_t       resource_idx      = 0;
	const size_t numKnownResources = self->knownResourcesCount;
	for ( uint64_t *currentRes = self->knownResources.data(); resource_idx != numKnownResources; ++resource_idx, ++currentRes ) {
		if ( *currentRes == resourceID ) {
			break;
		}
	}

	if ( resource_idx == numKnownResources ) {
		// resourceID was not in knownResources; we must add it

		// we cannot track more individual resources than we have channels in our bitset.
		assert( self->knownResourcesCount < MAX_NUM_LAYER_RESOURCES );

		self->knownResources[ numKnownResources ] = resourceID;
		self->knownResourcesCount++;
	}

	// ----------| invariant: resource_idx names the resource in question

	// -- ensure there is a current layer

	if ( self->layers.empty() ) {
		self->layers.emplace_back(); // add an empty layer
		self->layers_debug_names.emplace_back( "default" );
	}

	// -- fetch current layer

	auto &currentLayer = self->layers.back();

	// -- add resource to current layer

	if ( access_type & eAccessTypeRead ) {
		currentLayer.reads |= ( 1 << resource_idx );
	}
	if ( access_type & eAccessTypeWrite ) {
		currentLayer.writes |= ( 1 << resource_idx );
	}
}

// ----------------------------------------------------------------------

static void dependency_manager_next_layer( dependency_manager_o *self, char const *debug_name = nullptr ) {
	self->layers.emplace_back();
	self->layers_debug_names.emplace_back( debug_name );
}

// ----------------------------------------------------------------------

static void dependency_manager_print_sort_order( dependency_manager_o *self ) {
	for ( size_t i = 0; i != self->layers_sort_order.size(); ++i ) {
		std::cout << "Layer " << std::dec << std::setw( 3 ) << i << " sort order : " << std::setw( 3 ) << self->layers_sort_order[ i ] << " - (`" << self->layers_debug_names[ i ] << "`)" << std::endl
		          << std::flush;
	}
}

// ----------------------------------------------------------------------

static void dependency_manager_resolve_dependencies( dependency_manager_o *self ) {

	self->layers_sort_order.resize( self->layers.size(), 0 );

	layers_resolve_dependencies( self->layers.data(), self->layers.size(), self->layers_sort_order.data() );
}

// ----------------------------------------------------------------------

static bool test_dependency_manager() {
	auto dm = dependency_manager_create();

	dependency_manager_next_layer( dm, "layer 0" );
	dependency_manager_add_resource( dm, 33, eAccessTypeReadWrite );
	dependency_manager_add_resource( dm, 12, eAccessTypeRead );
	dependency_manager_next_layer( dm, "layer 1" );
	dependency_manager_add_resource( dm, 1, eAccessTypeWrite );
	dependency_manager_add_resource( dm, 2, eAccessTypeWrite );
	dependency_manager_next_layer( dm, "layer 2" );
	dependency_manager_add_resource( dm, 3, eAccessTypeReadWrite );
	dependency_manager_add_resource( dm, 2, eAccessTypeRead );

	dependency_manager_resolve_dependencies( dm );

	dependency_manager_print_sort_order( dm );

	dependency_manager_destroy( dm );

	return true;
}

// ----------------------------------------------------------------------

static bool test_sorting() {
	std::vector<Layer> layers = {
	    {
	        0b000001, // reads
	        0b011001, // writes
	    },
	    {
	        0b010010, // reads
	        0b000110, // writes
	    },
	    {
	        0b001100, // reads
	        0b000100, // writes
	    },
	    {
	        0b111011, // reads
	        0b100001, // writes
	    },
	    {
	        0b001000, // reads
	        0b000100, // writes
	    },
	};

	std::vector<uint32_t> layerOrders( layers.size() ); // Holds one sort index for each layer

	layers_resolve_dependencies( layers.data(), layers.size(), layerOrders.data() );

	for ( size_t i = 0; i != layerOrders.size(); ++i ) {
		std::cout << "Layer " << std::dec << i << ", sort order : " << layerOrders[ i ] << std::endl
		          << std::flush;
	}

	return true;
}

// ----------------------------------------------------------------------

constexpr uint32_t fnv1a_val_32_const   = 0x811c9dc5;
constexpr uint32_t fnv1a_prime_32_const = 0x1000193;
constexpr uint64_t fnv1a_val_64_const   = 0xcbf29ce484222325;
constexpr uint64_t fnv1a_prime_64_const = 0x100000001b3;

// adapted from: https://notes.underscorediscovery.com/constexpr-fnv1a/
inline constexpr uint64_t hash_64_fnv1a_const( const char *const str, const uint64_t value = fnv1a_val_64_const ) noexcept {
	return ( *str ) ? hash_64_fnv1a_const( str + 1, ( value ^ uint64_t( *str ) ) * fnv1a_prime_64_const ) : value;
}

// adapted from: https://notes.underscorediscovery.com/constexpr-fnv1a/
inline constexpr uint32_t hash_32_fnv1a_const( const char *const str, const uint32_t value = fnv1a_val_32_const ) noexcept {
	return ( *str ) ? hash_32_fnv1a_const( str + 1, ( value ^ uint32_t( *str ) ) * fnv1a_prime_32_const ) : value;
}

// ----------------------------------------------------------------------

// adapted from: https://notes.underscorediscovery.com/constexpr-fnv1a/
inline uint64_t hash_64_fnv1a( char const *const input ) noexcept {

	uint64_t           hash  = fnv1a_val_64_const;
	constexpr uint64_t prime = fnv1a_prime_64_const;

	for ( char const *i = input; *i != 0; ++i ) {
		uint8_t value = static_cast<const uint8_t &>( *i );
		hash          = hash ^ value;
		hash          = hash * prime;
	}

	return hash;

} //hash_64_fnv1a

// ----------------------------------------------------------------------

inline uint32_t hash_32_fnv1a( char const *const input ) noexcept {

	uint32_t           hash  = fnv1a_val_32_const;
	constexpr uint32_t prime = fnv1a_prime_32_const;

	for ( char const *i = input; *i != 0; ++i ) {
		uint8_t value = static_cast<const uint8_t &>( *i );
		hash          = hash ^ value;
		hash          = hash * prime;
	}

	return hash;

} //hash_32_fnv1a

// ----------------------------------------------------------------------

static bool run_tests() {
	bool                       result = true;
	std::array<std::string, 4> test{"Hello world", "", " and another string ...", "weird string\0"};

	for ( size_t i = 0; i != test.size(); ++i ) {
		bool testResult = ( hash_64_fnv1a( test[ i ].c_str() ) == hash_64_fnv1a_const( test[ i ].c_str() ) );
		std::cout << "Test hash_64 #" << i << " :" << ( testResult ? "Passed" : "Failed" ) << std::endl
		          << std::flush;
		result &= testResult;
	}

	for ( size_t i = 0; i != test.size(); ++i ) {
		bool testResult = ( hash_32_fnv1a( test[ i ].c_str() ) == hash_32_fnv1a_const( test[ i ].c_str() ) );
		std::cout << "Test hash_32 #" << i << " :" << ( testResult ? "Passed" : "Failed" ) << std::endl
		          << std::flush;
		result &= testResult;
	}

	test_sorting();

	test_dependency_manager();

	return result;
}

// ----------------------------------------------------------------------

static void simple_module_update( simple_module_o *self ) {
	static int firstRun = true;

	if ( firstRun ) {

		if ( run_tests() ) {
			std::cout << "All Tests passed." << std::flush;
		} else {
			std::cout << "Some Tests failed." << std::flush;
		};

		firstRun = false;
	} else {
		using std::chrono_literals::operator""ms;
		// std::this_thread::sleep_for( 100ms );
	}
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_simple_module_api( void *api ) {
	auto &simple_module_i = static_cast<simple_module_api *>( api )->simple_module_i;

	simple_module_i.create  = simple_module_create;
	simple_module_i.destroy = simple_module_destroy;
	simple_module_i.update  = simple_module_update;
}

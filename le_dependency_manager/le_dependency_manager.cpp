#include "le_dependency_manager.h"
#include "pal_api_loader/ApiRegistry.hpp"

#include "iostream"
#include "iomanip"
#include <bitset>
#include <vector>
#include <assert.h>

constexpr size_t MAX_NUM_LAYER_RESOURCES                = 64;
using BitField                                          = std::bitset<MAX_NUM_LAYER_RESOURCES>;
constexpr uint64_t LE_DEPENDENCY_MANAGER_ROOT_LAYER_TAG = hash_64_fnv1a_const( "DEPENDENCY_MANAGER_ROOT_LAYER_TAG" );

#ifndef NDEBUG
#	define LE_DEPENDENCY_MANAGER_USE_DEBUG_NAMES
#endif

using namespace le_dependency_manager;

struct Layer {
	BitField reads;
	BitField writes;
};

/// When we add a Resource to the dependency tracker,
///
/// 0. we first must know the current layer
/// 1. we compare the resource id against resources already known to the tracker
/// 2. if resource is known, we use the index into the ids vector to identify the resource
/// 3. we update the current layer at the index of the current resource based on the resource's read or write access.
///
/// This means there is a lookup into a vector of resources for every resource which we add to the layer - this vector may grow, so we always just store indices.
///
struct le_dependency_manager_o {
	std::array<uint64_t, MAX_NUM_LAYER_RESOURCES> knownResources;          // Stores all known resources over all layers, provides us with canonical indices for each known resource
	size_t                                        knownResourcesCount = 0; // count of used elements in knownResources
	std::vector<Layer>                            layers;                  // r/w information for each layer. `Layer::reads` and `Layer::writes` bitfield indices correspond to `knownResources` indices
	std::vector<uint32_t>                         layers_sort_order;       // Sort order for layers
#ifdef LE_DEPENDENCY_MANAGER_USE_DEBUG_NAMES
	std::vector<std::string> layers_debug_names; // debug name for each layer(optional), but must be same element count as layers.
#endif
};

/*
 * I think - it should be possible to get any dependent layers by up-walking the layers array
 * and looking for writes where we have reads in a layer above.
 *
 * Our goal is to discard any layers which have no contribution to the final product.
 *
 * 0. Root layers are layers which have been tagged manually as "must use" -
 *    These layers - and their dependencies must always be seen as contributing
 *
 *    By default, the last layer in the ordered list of layers should be tagged as a
 *    root layer.
 *
 * 1. First eliminate any layers which have no effect on any root layers
 * -- we go from last root layer to first layer .
 * -- we accumulate reads
 * -- if there is a write in l(n-1) where we have a read in l(accum) then l(n-1) is a provider.
 * -- null out all positions in accum where there was a write in l(n-1), unless there was a read-write, in which case we keep the read
 *
 * 2. Calculate sort indices for layers.
 *
 0 R: 00
 * W: 01 (used by 2)
 * -----
 1 R: 00 (can be discarded)
 * W: 00
 * -----
 2 R: 01
 * W: 01 (used by 2, 3)
 * -----
 3 R: 01
 * W: 10
 * -----
 *
 * */

/// \brief Tag any layers which contribute to any root layer
/// \details We do this so that we can weed out any layers which are provably
///          not contributing - these don't need to be executed at all.
static void layers_tag_contributing( Layer *const layers, const size_t numLayers ) {

	// we must iterate backwards from last layer to first layer
	Layer *            layer      = layers + numLayers;
	Layer const *const layer_rend = layers;

	BitField read_accum;

	// find first root layer
	//    monitored reads will be from the first root layer

	while ( layer != layer_rend ) {
		--layer;

		bool isRoot = layer->reads[ 0 ]; // any layer which has the root signal set in the first read channel is considered a root layer

		// If it's a root layer, get all reads from (= providers to) this layer
		// If it's not a root layer, first see if there are any writes to currently monitored reads
		//    if yes, add all reads to monitored reads

		if ( isRoot || ( layer->writes & read_accum ).any() ) {
			// If this layer is a root layer - OR					   ) this means the layer is contributing
			// If this layer writes to any subsequent monitored reads, )
			// Then we must monitor all reads of this layer.
			read_accum |= layer->reads;

			layer->reads[ 0 ] = true; // Make sure the layer is tagged as contributing
		} else {
			// Otherwise - this layer does not contribute
		}
	} // end for all layers, backwards iteration
}

// ----------------------------------------------------------------------

/// Note: `sortIndices` must point to an array of `numLayers` elements of type uint32_t
static void layers_calculate_sort_indices( Layer const *const layers, const size_t numLayers, uint32_t *sortIndices ) {

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

			// Weed out any layers which are marked as non-contributing

			if ( layer->reads[ 0 ] == false ) {
				*layerO = ~( 0u ); // tag layer as not contributing by marking it with the maximum sort index
				continue;
			}

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

			*layerO = sortIndex; // store current sortIndex value with layer
		}
	}

	// print out debug information
	//	std::cout << layer->reads << " reads" << std::endl
	//	          << std::flush;
	//	std::cout << read_accum << " read accum" << std::endl
	//	          << std::flush;
	//	std::cout << write_accum << " write accum" << std::endl
	//	          << std::flush;
}

// ----------------------------------------------------------------------

static void le_dependency_manager_reset( le_dependency_manager_o *self ) {

	// NOTE: we don't have to explicitly reset the contents of self->knownResources, since self->knownResourcesCount
	// keeps track of the number of valid elements in the array. We only make sure that the very first element in
	// knownResources is a special tag which we use to tag a layer as a root (i.e. a layer which always contributes)
	// layer.
	self->knownResourcesCount = 1;
	self->knownResources[ 0 ] = LE_DEPENDENCY_MANAGER_ROOT_LAYER_TAG;

	self->layers.clear();
	self->layers_sort_order.clear();
#ifdef LE_DEPENDENCY_MANAGER_USE_DEBUG_NAMES
	self->layers_debug_names.clear();
#endif
}

// ----------------------------------------------------------------------

static void le_dependency_manager_add_resource( le_dependency_manager_o *self, uint64_t resourceID, int access_type ) {
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
#ifdef LE_DEPENDENCY_MANAGER_USE_DEBUG_NAMES
		self->layers_debug_names.emplace_back( "default" );
#endif
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

static void le_dependency_manager_next_layer( le_dependency_manager_o *self, char const *debug_name = nullptr ) {
	self->layers.emplace_back();
#ifdef LE_DEPENDENCY_MANAGER_USE_DEBUG_NAMES
	self->layers_debug_names.emplace_back( debug_name );
#endif
}

// ----------------------------------------------------------------------

static void le_dependency_manager_next_root_layer( le_dependency_manager_o *self, char const *debug_name = nullptr ) {
	self->layers.emplace_back();
	le_dependency_manager_add_resource( self, LE_DEPENDENCY_MANAGER_ROOT_LAYER_TAG, eAccessTypeRead );
#ifdef LE_DEPENDENCY_MANAGER_USE_DEBUG_NAMES
	self->layers_debug_names.emplace_back( debug_name );
#endif
}

// ----------------------------------------------------------------------

static void le_dependency_manager_print_sort_order( le_dependency_manager_o *self ) {
	for ( size_t i = 0; i != self->layers_sort_order.size(); ++i ) {

		std::cout << "Layer " << std::dec << std::setw( 3 ) << i << " sort order : " << std::setw( 3 ) << self->layers_sort_order[ i ]
#ifdef LE_DEPENDENCY_MANAGER_USE_DEBUG_NAMES
		          << " - (`" << self->layers_debug_names[ i ] << "`)"
#endif
		          << std::endl
		          << std::flush;
	}
}

// ----------------------------------------------------------------------

static void le_dependency_manager_resolve_dependencies( le_dependency_manager_o *self ) {

	self->layers_sort_order.resize( self->layers.size(), 0 );

	// Find out which layers contribute to any root layer
	layers_tag_contributing( self->layers.data(), self->layers.size() );

	// Calculate sort indices (layers which were tagged as non-contributing will receive sort index ~(0u) )
	layers_calculate_sort_indices( self->layers.data(), self->layers.size(), self->layers_sort_order.data() );
}

// ----------------------------------------------------------------------

static void le_dependency_manager_get_layer_sort_indices( le_dependency_manager_o *self, uint32_t **pIndices, size_t *pIndicesCount ) {
	*pIndices      = self->layers_sort_order.data();
	*pIndicesCount = self->layers_sort_order.size();
};

// ----------------------------------------------------------------------

static le_dependency_manager_o *le_dependency_manager_create() {
	auto self = new le_dependency_manager_o();
	le_dependency_manager_reset( self );
	return self;
}

// ----------------------------------------------------------------------

static void le_dependency_manager_destroy( le_dependency_manager_o *self ) {
	delete self;
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

	layers_calculate_sort_indices( layers.data(), layers.size(), layerOrders.data() );

	for ( size_t i = 0; i != layerOrders.size(); ++i ) {
		std::cout << "Layer " << std::dec << i << ", sort order : " << layerOrders[ i ] << std::endl
		          << std::flush;
	}

	return true;
}

// ----------------------------------------------------------------------

ISL_API_ATTR void register_le_dependency_manager_api( void *api ) {
	auto &le_dependency_manager_i = static_cast<le_dependency_manager_api *>( api )->le_dependency_manager_i;

	le_dependency_manager_i.create                 = le_dependency_manager_create;
	le_dependency_manager_i.destroy                = le_dependency_manager_destroy;
	le_dependency_manager_i.add_resource           = le_dependency_manager_add_resource;
	le_dependency_manager_i.next_layer             = le_dependency_manager_next_layer;
	le_dependency_manager_i.next_root_layer        = le_dependency_manager_next_root_layer;
	le_dependency_manager_i.resolve_dependencies   = le_dependency_manager_resolve_dependencies;
	le_dependency_manager_i.get_layer_sort_indices = le_dependency_manager_get_layer_sort_indices;
	le_dependency_manager_i.print_sort_order       = le_dependency_manager_print_sort_order;
}

#include "le_core.h"
#include "le_core/le_api_loader.h"
#include "le_file_watcher/le_file_watcher.h"
#include "hash_util.h"
#include <vector>
#include <string>
#include <stdio.h>
#include <iostream>
#include <iomanip>
#include <array>
#include <assert.h>

struct ApiStore {
	static constexpr size_t        fp_max_bytes = 4096 * 10; // 10 pages of function pointers should be enough
	std::array<char, fp_max_bytes> fp_storage{};             // byte storage space for function pointers
	size_t                         fp_used_bytes{};          // number of bytes in use for function pointer usage

	std::vector<std::string> names{};      // debug api names
	std::vector<uint64_t>    nameHashes{}; // hashed api names (used for lookup)
	std::vector<void *>      ptrs{};       // pointer to struct holding api for each api name
};

struct loader_callback_params_o {
	le_module_loader_o *loader;
	void *              api;
	std::string         lib_register_fun_name;
};

// FIXME: there is a vexing bug with initialisation order when compiling a static version of this app
// -- ApiStore is first written to and then initialised again - no idea why this happens. Because fp_used_bytes
// is not reset this is mitigated, as apis registered after the very first won't overwrite the very first, but the
// names and namehashes, and ptrs get reset...
static ApiStore apiStore{};

static auto &file_watcher_i = le_file_watcher_api_i -> le_file_watcher_i; // le_file_watcher_api_i provided by le_file_watcher.h
static auto  file_watcher   = file_watcher_i.create();

// ----------------------------------------------------------------------
// We use C++ RAII to add pointers to a "kill list" so that objects which
// need global lifetime can be unloaded once the app unloads.
struct DeferDelete {

	std::vector<le_module_loader_o *>       loaders; // loaders to clean up
	std::vector<loader_callback_params_o *> params;  // callback params to clean up

	~DeferDelete() {
		for ( auto &l : loaders ) {
			if ( l ) {
				le_module_loader_api_i->le_module_loader_i.destroy( l );
			}
		}
		for ( auto &p : params ) {
			delete p;
		}
	}
};

static DeferDelete defer_delete; // any elements referenced in this pool will get deleted when program unloads.

// ----------------------------------------------------------------------
// Trigger callbacks for change in watched files.
ISL_API_ATTR void le_core_poll_for_module_reloads() {
	file_watcher_i.poll_notifications( file_watcher );
}

// ----------------------------------------------------------------------
/// \returns index into apiStore entry for api with given id
/// \param id        Hashed api name string
/// \param debugName Api name string for debug purposes
/// \note  In case a given id is not found in apiStore, a new entry is appended to apiStore
static size_t produce_api_index( uint64_t id, const char *debugName ) {

	size_t foundElement = 0;

	for ( const auto &n : apiStore.nameHashes ) {
		if ( n == id ) {
			break;
		}
		++foundElement;
	}

	if ( foundElement == apiStore.nameHashes.size() ) {
		// no element found, we need to add an element
		apiStore.nameHashes.emplace_back( id );
		apiStore.ptrs.emplace_back( nullptr );    // initialise to nullptr
		apiStore.names.emplace_back( debugName ); // implicitly creates a string
	}

	// --------| invariant: foundElement points to correct element

	return foundElement;
}

// ----------------------------------------------------------------------

static void *le_core_get_api( uint64_t id, const char *debugName ) {

	size_t foundElement = produce_api_index( id, debugName );
	return apiStore.ptrs[ foundElement ];
}

// ----------------------------------------------------------------------

static void *le_core_create_api( uint64_t id, size_t apiStructSize, const char *debugName ) {

	size_t foundElement = produce_api_index( id, debugName );

	auto &apiPtr = apiStore.ptrs[ foundElement ];

	if ( apiPtr == nullptr ) {

		// api struct has not yet been allocated, we must do so now.

		if ( apiStore.fp_used_bytes + apiStructSize > apiStore.fp_max_bytes ) {
			std::cerr << __PRETTY_FUNCTION__ << " FATAL ERROR: Out of function pointer memory" << std::endl
			          << std::flush;
			assert( false ); // out of function pointer memory
			exit( -1 );
		}

		void *apiMemory = ( apiStore.fp_storage.data() + apiStore.fp_used_bytes ); // point to next free space in api store
		apiStore.fp_used_bytes += apiStructSize;                                   // increase number of used bytes in api store

		apiPtr = apiMemory; // Store updated address for api - this address won't change for the
		                    // duration of the program.
	}

	return apiPtr;
}

// ----------------------------------------------------------------------

static int addWatch( const char *watchedPath, loader_callback_params_o *user_data ) {

	le_file_watcher_watch_settings watchSettings = {};

	watchSettings.callback_fun = []( const char *, void *user_data ) -> bool {
		auto params = static_cast<loader_callback_params_o *>( user_data );
		le_module_loader_api_i->le_module_loader_i.load( params->loader );
		return le_module_loader_api_i->le_module_loader_i.register_api( params->loader, params->api, params->lib_register_fun_name.c_str() );
	};

	watchSettings.callback_user_data = reinterpret_cast<void *>( user_data );
	watchSettings.filePath           = watchedPath;

	return file_watcher_i.add_watch( file_watcher, &watchSettings );
}

// ----------------------------------------------------------------------

ISL_API_ATTR void *le_core_load_module_static( char const *module_name, void ( *module_reg_fun )( void * ), uint64_t api_size_in_bytes ) {
	void *api = le_core_create_api( hash_64_fnv1a_const( module_name ), api_size_in_bytes, module_name );
	module_reg_fun( api );
	return api;
};

ISL_API_ATTR void *le_core_load_module_dynamic( char const *module_name, uint64_t api_size_in_bytes, bool should_watch ) {

	uint64_t module_name_hash = hash_64_fnv1a_const( module_name );

	void *api = le_core_get_api( module_name_hash, module_name );

	if ( api == nullptr ) {

		static auto &module_loader_i = le_module_loader_api_i->le_module_loader_i;

		char api_register_fun_name[ 256 ];
		snprintf( api_register_fun_name, 255, "le_module_register_%s", module_name );

		std::string         module_path = "./modules/lib" + std::string( module_name ) + ".so";
		le_module_loader_o *loader      = module_loader_i.create( module_path.c_str() );

		defer_delete.loaders.push_back( loader ); // add to cleanup list

		// Important to store api back to table here *before* calling loadApi,
		// as loadApi might recursively add other apis
		// which would have the effect of allocating more than one copy of the api
		//
		api = le_core_create_api( hash_64_fnv1a_const( module_name ), api_size_in_bytes, module_name );

		module_loader_i.load( loader );
		module_loader_i.register_api( loader, api, api_register_fun_name );

		// ----
		if ( should_watch ) {
			loader_callback_params_o *callbackParams = new loader_callback_params_o{loader, api, api_register_fun_name};
			defer_delete.params.push_back( callbackParams ); // add to cleanup list
			auto watchId = addWatch( module_path.c_str(), callbackParams );
		}

	} else {
		// TODO: we should warn that this api was already added.
	}

	return api;
};

ISL_API_ATTR bool le_core_load_library_persistently( char const *library_name ) {
	return le_module_loader_api_i->le_module_loader_i.load_library_persistently( library_name );
}

// ----------------------------------------------------------------------

/* Provide storage for a lookup table for uniform arguments - any argument  
 * set via the LE_ARGUMENT_NAME macro will be placed in this table should
 * we run in Debug mode. 
 * 
 * In Release mode the macro evaluates to a constexpr, and argument ids are 
 * resolved at compile-time, therefore will not be
 * placed in table.
 * 
 */
struct ArgumentNameTable {
	// std::mutex mtx; // TODO: we want to add a mutex so that any modifications to this table are protected when multithreading.
	std::vector<std::string> names;
	std::vector<uint64_t>    hashes;
};

static ArgumentNameTable argument_names_table{};

ISL_API_ATTR void update_argument_name_table( const char *name, uint64_t value ) {

	// find index of entry with current value in table

	uint64_t const *hashes_begin = argument_names_table.hashes.data();
	auto            hashes_end   = hashes_begin + argument_names_table.hashes.size();

	size_t name_index = 0;
	for ( auto h = hashes_begin; h != hashes_end; h++, name_index++ ) {
		if ( *h == value ) {
			break;
		}
	}

	if ( name_index == argument_names_table.names.size() ) {
		// not found, we must add a new entry
		argument_names_table.names.push_back( name );
		argument_names_table.hashes.push_back( value );
		// std::cout << "Argument: '" << std::setw( 30 ) << source << "' : 0x" << std::hex << value << std::endl
		//           << std::flush;
	} else {
		// entry already exists - test whether the names match
		assert( argument_names_table.names.at( name_index ) == std::string( name ) && "Possible hash collision, names for hashes don't match!" );
	}
};

ISL_API_ATTR char const *get_argument_name_from_hash( uint64_t value ) {

	if ( argument_names_table.hashes.empty() ) {
		return "<< Argument name table empty. >>";
	}

	uint64_t const *hashes_begin = argument_names_table.hashes.data();
	auto            hashes_end   = hashes_begin + argument_names_table.hashes.size();

	size_t name_index = 0;
	for ( auto h = hashes_begin; h != hashes_end; h++, name_index++ ) {
		if ( *h == value ) {
			return argument_names_table.names.at( name_index ).c_str();
		}
	}

	return "<< Argument name could not be resolved. >>";
}

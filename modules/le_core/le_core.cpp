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
#include <atomic>
#include <algorithm>
#include <string.h> // for memcpy

#ifndef _WIN32
#	include <sys/mman.h>
#	include <unistd.h>
#endif

struct ApiStore {
	std::vector<std::string> names{};      // Api names (used for debugging)
	std::vector<uint64_t>    nameHashes{}; // Hashed api names (used for lookup)
	std::vector<void *>      ptrs{};       // Pointer to struct holding api for each api name
	~ApiStore() {
		// We must free any api table entry for which memory was been allocated.
		for ( auto p : ptrs ) {
			if ( p ) {
				free( p );
			}
		}
	}
};

struct loader_callback_params_o {
	le_module_loader_o *loader;
	void *              api;
	std::string         lib_register_fun_name;
	int                 watch_id;
};

static ApiStore apiStore{};

static auto &file_watcher_i = le_file_watcher_api_i -> le_file_watcher_i; // le_file_watcher_api_i provided by le_file_watcher.h
static auto  file_watcher   = file_watcher_i.create();

// ----------------------------------------------------------------------
// Trigger callbacks in case change was detected in watched files.
ISL_API_ATTR void le_core_poll_for_module_reloads() {
	file_watcher_i.poll_notifications( file_watcher );
}

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

static DeferDelete defer_delete; // Any elements referenced in this pool will get deleted when program unloads.

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

		void *apiMemory = calloc( 1, apiStructSize ); // allocate space for api pointers on heap
		assert( apiMemory && "Could not allocate memory for API struct." );

		apiPtr = apiMemory; // Store updated address for api - this address won't change for the
		                    // duration of the program.
	}

	return apiPtr;
}

// ----------------------------------------------------------------------

ISL_API_ATTR void *le_core_load_module_static( char const *module_name, void ( *module_reg_fun )( void * ), uint64_t api_size_in_bytes ) {
	void *api = le_core_create_api( hash_64_fnv1a_const( module_name ), api_size_in_bytes, module_name );
	module_reg_fun( api );
	return api;
};

// ----------------------------------------------------------------------

ISL_API_ATTR void *le_core_load_module_dynamic( char const *module_name, uint64_t api_size_in_bytes, bool should_watch ) {

	uint64_t module_name_hash = hash_64_fnv1a_const( module_name );

	void *api = le_core_get_api( module_name_hash, module_name );

	if ( module_name_hash == hash_64_fnv1a_const( "le_file_watcher" ) ) {
		// To answer that age-old question: No-one watches the watcher.
		should_watch = false;
	}

	if ( api == nullptr ) {

		static auto &module_loader_i = le_module_loader_api_i->le_module_loader_i;

		char api_register_fun_name[ 256 ];
		snprintf( api_register_fun_name, 255, "le_module_register_%s", module_name );

#ifdef WIN32

		std::string         module_path = "./" + std::string( module_name ) + ".dll";
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
			loader_callback_params_o *callbackParams = new loader_callback_params_o{};
			callbackParams->api                      = api;
			callbackParams->loader                   = loader;
			callbackParams->lib_register_fun_name    = api_register_fun_name;
			callbackParams->watch_id                 = 0;
			defer_delete.params.push_back( callbackParams ); // add to deferred cleanup list

			le_file_watcher_watch_settings watchSettings = {};

			watchSettings.callback_fun = []( const char *, void *user_data ) -> bool {
				auto params = static_cast<loader_callback_params_o *>( user_data );
				le_module_loader_api_i->le_module_loader_i.load( params->loader );
				return le_module_loader_api_i->le_module_loader_i.register_api( params->loader, params->api, params->lib_register_fun_name.c_str() );
			};

			watchSettings.callback_user_data = reinterpret_cast<void *>( callbackParams );
			watchSettings.filePath           = module_path.c_str();

			callbackParams->watch_id = file_watcher_i.add_watch( file_watcher, &watchSettings );
		}

#else

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
			loader_callback_params_o *callbackParams = new loader_callback_params_o{};
			callbackParams->api                      = api;
			callbackParams->loader                   = loader;
			callbackParams->lib_register_fun_name    = api_register_fun_name;
			callbackParams->watch_id                 = 0;
			defer_delete.params.push_back( callbackParams ); // add to deferred cleanup list

			le_file_watcher_watch_settings watchSettings = {};

			watchSettings.callback_fun = []( const char *, void *user_data ) -> bool {
				auto params = static_cast<loader_callback_params_o *>( user_data );
				le_module_loader_api_i->le_module_loader_i.load( params->loader );
				return le_module_loader_api_i->le_module_loader_i.register_api( params->loader, params->api, params->lib_register_fun_name.c_str() );
			};

			watchSettings.callback_user_data = reinterpret_cast<void *>( callbackParams );
			watchSettings.filePath           = module_path.c_str();

			callbackParams->watch_id = file_watcher_i.add_watch( file_watcher, &watchSettings );
		}
#endif
	} else {
		// TODO: we should warn that this api was already added.
	}

	return api;
};

// ----------------------------------------------------------------------

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
	// std::mutex mtx; // TODO: consider: do we want to add a mutex so that any modifications to this table are protected when multithreading?
	std::vector<std::string> names;
	std::vector<uint64_t>    hashes;
};

static ArgumentNameTable argument_names_table{};

// ----------------------------------------------------------------------

ISL_API_ATTR void le_update_argument_name_table( const char *name, uint64_t value ) {

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

// ----------------------------------------------------------------------

ISL_API_ATTR char const *le_get_argument_name_from_hash( uint64_t value ) {

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

// callback forwarding --------------------------------------------------

#if !defined( NDEBUG ) && defined( __x86_64__ )

static size_t const PAGE_SIZE = sysconf( _SC_PAGESIZE );
// Maximum number of available callback forwarders. we can add more if needed
// by adding extra pages of 'plt' tables and and offset tables.
static size_t const CORE_MAX_CALLBACK_FORWARDERS = PAGE_SIZE / 16;

/// Callback forwarding works via a virtual plt/got table:
///
/// First, we allocate two pages, a top page and a bottom page.
/// The top page contains trampoline functions. Each trampoline
/// function entry is identical. Each has a corresponding got entry
/// which is addressed rip-relative.
///
/// The top page with the trampoline entries has execution permissions,
/// but is set to have no write permissions once the two pages have
/// been initialised.
///
/// Since we make sure that each entry in the plt table is 16 bytes
/// in size, and each entry has the exact same offset (of exactly one page)
/// we can place entries in the .got at 16 byte intervals.
///
///
///     | plt entry   (9 Bytes)      xx xx xx xx xx xx xx | --.        --- top (plt) page
///     | plt entry   (9 Bytes)      xx xx xx xx xx xx xx |   | --.
///     | plt entry   (9 Bytes)      xx xx xx xx xx xx xx |   |   |
///     | plt entry   (9 Bytes)      xx xx xx xx xx xx xx |   |   |
///     | ..                                              |   |   |
///     | got entry 0 (8 Bytes)   xx xx xx xx xx xx xx xx | <-"   |    --- bottom (got) page
///     | got entry 1 (8 Bytes)   xx xx xx xx xx xx xx xx |     <-"
///     | got entry 2 (8 Bytes)   xx xx xx xx xx xx xx xx |
///     | got entry 3 (8 Bytes)   xx xx xx xx xx xx xx xx |
///
/// The plt page contains identical code for each plt entry.
///
/// It indirectly loads the jmp address pointed to in the
/// corresponding got entry:
///
///     mov rax, [rip + offset] // offset is 4096-7: page_size(4096B) - this instruction size (which is 7bytes).
///     jmp [rax]
///
/// Note that in the last assembly instruction $rax gets dereferenced again.
///
/// This means that what we want to store a fixed address of the callback
/// function pointer in the got. Typically, this will be an entry from an
/// api interface struct which is automatically updated when the module
/// to which it points reloads.
///

class PltGot {
	void * plt_got;
	size_t plt_got_sz;
	char * plt_page;
	char * got_page;

  public:
	PltGot() {
		// map plt and got page.

		plt_got_sz = PAGE_SIZE * 2;
		plt_got    = mmap( NULL, plt_got_sz, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0 );
		assert( plt_got != MAP_FAILED && "Map did not succeed" );

		plt_page = static_cast<char *>( plt_got );
		got_page = plt_page + PAGE_SIZE;

		// We fill the first page with trampoline thunks - this is program code.
		// It only works for amd64 systems.

		uint8_t thunk[ 16 ] = {
		    // mov rax      , [rip + offset]
		    0x48, 0x8b, 0x05, 0x00, 0x00, 0x00, 0x00,
		    // jmp [rax]
		    0xff, 0x20,
		    // filler to 16B
		    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

		int32_t *offset = ( int32_t * )( thunk + 3 ); // get address of offset inside thunk

		*offset = PAGE_SIZE; // set offset to one page.
		*offset -= 7;        // as the current instruction has 7 bytes, we must subtract 7 from the offset value.

		for ( size_t i = 0; i != CORE_MAX_CALLBACK_FORWARDERS; i++ ) {
			memcpy( plt_page + 16 * i, thunk, 16 );
		}

		// now we set permissions for this page to be exec + read only.
		// We keep read/write for the second page, as second page will
		// contain the got, which we might want to update externally.

		mprotect( plt_got, PAGE_SIZE, PROT_READ | PROT_EXEC );
	};

	void *plt_at( size_t index ) {
		assert( index < CORE_MAX_CALLBACK_FORWARDERS && "callback plt index out of bounds" );
		return plt_page + ( index * 16 );
	}

	void *got_at( size_t index ) {
		assert( index < CORE_MAX_CALLBACK_FORWARDERS && "callback got index out of bounds" );
		return got_page + ( index * 16 );
	}

	~PltGot() {
		int result = munmap( plt_got, plt_got_sz );
		assert( result != -1 && "unmap failed" );
	};
};

static PltGot         CALLBACK_PLT_GOT{};
std::atomic<uint32_t> USED_CALLBACK_FORWARDERS = 0;

// ----------------------------------------------------------------------

void *le_core_get_callback_forwarder_addr( void *callback_addr ) {

	uint32_t current_index = USED_CALLBACK_FORWARDERS++; // post-increment

	// Make sure we're not overshooting
	assert( USED_CALLBACK_FORWARDERS < CORE_MAX_CALLBACK_FORWARDERS );

	void *got_addr = CALLBACK_PLT_GOT.got_at( current_index );

	// copy value of callback pointer into got table
	memcpy( got_addr, &callback_addr, sizeof( void * ) );

	return CALLBACK_PLT_GOT.plt_at( current_index );
};
#endif
// end callback forwarding-----------------------------------------------
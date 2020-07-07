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

// ----------------------------------------------------------------------

// Maximum number of available callback forwarders.
// Note: run `python create_sled.py > sled.asm` after changing.
#define CORE_MAX_CALLBACK_FORWARDERS 32

void *                target_func_addr[ CORE_MAX_CALLBACK_FORWARDERS ] = {};
std::atomic<uint32_t> USED_CALLBACK_FORWARDERS                         = 0;

// clang-format off
extern "C" void trampoline_func() {

    // What follows is a sled, which, depending on how far into it the call is entered,
    // tells us which index to look up from the global phone directory of function
    // pointers.

    // Auto-generated via the python script, see CORE_MAX_CALLBACK_FORWARDERS above
    #include "sled.asm" 

asm( R"ASM(
.text
    addq    %%rbx, %%rax
    movq    %0, %%rax
    movq    (%%rax), %%rax
    pop     %%rbx                   /* restore rbx register */
    jmpq    *%%rax
)ASM" : /* empty input operands */
      : "m" ( target_func_addr[0] ) // note that offset into this array
                                    // happens via sled above, which manipulates rbx register.
    );
};
// clang-format on

// ----------------------------------------------------------------------

void *core_get_callback_forwarder_addr( void *callback_addr ) {

	uint32_t current_index = USED_CALLBACK_FORWARDERS++; // post-increment

	// Make sure we're not overshooting
	assert( USED_CALLBACK_FORWARDERS < CORE_MAX_CALLBACK_FORWARDERS );

	target_func_addr[ current_index ] = callback_addr;

#if defined( __clang__ ) && ( defined __amd64__ )

	// Clang will issue one type of jmp call: the 5 Bytes `e9 xx xx xx xx` type.
	// Clang will not issue an ENDBR instruction at the beginning of the method,
	// therefore we only need to skip 4 bytes to reach the first element of our
	// sled.

	return ( char * )&trampoline_func +
	       ( 4 +                   // Jump over first push rbp instruction
	         13 * current_index ); // Any entry: 13B each

#elif ( defined( __GNUC__ ) || defined( __GNUG__ ) ) && ( defined __amd64__ )

	// For GCC, the size of sled entry depends on how close it is placed to the
	// `sled_end` label:
	//
	// GNU GCC will issue two types of jmp calls - depending on how
	// close we are to the target of the jmp.
	//
	//
	// If it is within 128 bytes, (that's the last 13 entries) it will be 10 bytes
	// in length, all other sled entries will be 13 bytes in length.
	// This is because the jmp instruction is 2Bytes of machine code for
	// short jmp (`eb xx`), and 5 Bytes (`e9 xx xx xx xx` ) for near jmp.
	//
	// Additionally, gnu gcc will also issue an `endbr` instruction at the start
	// of the method, meaning we will have to skip 8 bytes to reach the first element
	// of our sled.

	int num_large_jumps = 0;
	int num_small_jumps = 0;

	int start_addr = std::min( 0, 13 - CORE_MAX_CALLBACK_FORWARDERS );

	int val         = start_addr + int( current_index );
	num_large_jumps = std::max( 0, std::min( val, 0 ) - start_addr );
	num_small_jumps = std::max( 0, val );

	return ( char * )&trampoline_func +
	       ( 8 +                     // Jump over first push rbp instruction
	         10 * num_small_jumps +  // Last 13 entries: 10B each
	         13 * num_large_jumps ); // Any other entry: 13B each
#else
	assert( false && "missing implementation of callback_forwarder_addr for this compiler." );
#endif
};

// ----------------------------------------------------------------------
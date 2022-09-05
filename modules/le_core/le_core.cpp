#include "le_core.h"
#include "le_api_loader.h"
#include "le_file_watcher.h"
#include "le_hash_util.h"
#include <vector>
#include <unordered_map>
#include <string>
#include <stdio.h>
#include <iostream>
#include <iomanip>
#include <array>
#include <assert.h>
#include <atomic>
#include <algorithm>
#include <string.h> // for memcpy
#include <memory>
#include <mutex>

#ifndef _WIN32
#	include <sys/mman.h>
#	include <unistd.h>
#endif

struct ApiStore {
	std::vector<std::string> names{};      // Api names (used for debugging)
	std::vector<uint64_t>    nameHashes{}; // Hashed api names (used for lookup)
	std::vector<void*>       ptrs{};       // Pointer to struct holding api for each api name
	~ApiStore() {
		// We must free any api table entry for which memory was been allocated.
		for ( auto p : ptrs ) {
			if ( p ) {
				free( p );
			}
		}
	}
};

// We use a function here because this means lazy, but deterministic initialisation.
// see also: <http://www.cs.technion.ac.il/users/yechiel/c++-faq/static-init-order.html>
static ApiStore& apiStore() {
	static ApiStore obj;
	return obj;
};

ISL_API_ATTR void** le_core_produce_dictionary_entry( uint64_t key ) {
	static std::mutex                          mtx;
	std::scoped_lock                           lock( mtx );
	static std::unordered_map<uint64_t, void*> store{};
	return &store[ key ];
}

struct loader_callback_params_o {
	le_module_loader_o* loader;
	void*               api;      // address of api interface struct
	size_t              api_size; // api interface struct size in bytes
	std::string         lib_register_fun_name;
	int                 watch_id;
};

static le_module_loader_api const* get_module_loader_api() {
	static auto api = ( le_module_loader_api const* )
	    le_core_load_module_static(
	        le_module_name_le_module_loader,
	        le_module_register_le_module_loader,
	        sizeof( le_module_loader_api ) );
	return api;
}

static le_file_watcher_api const* get_le_file_watcher_api() {
	static auto api = ( le_file_watcher_api const* )
	    le_core_load_module_dynamic(
	        le_module_name_le_file_watcher,
	        sizeof( le_file_watcher_api ), false );
	return api;
}

// Wrapper used to ensure file watcher object gets deleted once
// this module unloads (at program exit) otherwise we would
// leak the object.
class FileWatcherWrapper : NoCopy, NoMove {
	le_file_watcher_o* self = get_le_file_watcher_api()->le_file_watcher_i.create();

  public:
	FileWatcherWrapper() {
	}
	~FileWatcherWrapper() {
		get_le_file_watcher_api()->le_file_watcher_i.destroy( self );
	}
	le_file_watcher_o* get() {
		return self;
	};
};

le_file_watcher_o* get_file_watcher() {
	static FileWatcherWrapper file_watcher{};
	return file_watcher.get();
}

// ----------------------------------------------------------------------
// Trigger callbacks in case change was detected in watched files.
ISL_API_ATTR void le_core_poll_for_module_reloads() {
	static auto file_watcher_i = get_le_file_watcher_api()->le_file_watcher_i;
	file_watcher_i.poll_notifications( get_file_watcher() );
}

// ----------------------------------------------------------------------
// We use C++ RAII to add pointers to a "kill list" so that objects which
// need global lifetime can be unloaded once the app unloads.
struct DeferDelete {

	std::vector<le_module_loader_o*>       loaders; // loaders to clean up
	std::vector<loader_callback_params_o*> params;  // callback params to clean up

	~DeferDelete() {
		static auto module_loader_i = get_module_loader_api()->le_module_loader_i;
		for ( auto& l : loaders ) {
			if ( l ) {
				module_loader_i.destroy( l );
			}
		}
		for ( auto& p : params ) {
			delete p;
		}
	}
};

static DeferDelete& defer_delete() {
	static DeferDelete obj{};
	return obj;
}

// ----------------------------------------------------------------------
/// \returns index into apiStore entry for api with given id
/// \param id        Hashed api name string
/// \param debugName Api name string for debug purposes
/// \note  In case a given id is not found in apiStore, a new entry is appended to apiStore
static size_t produce_api_index( uint64_t id, const char* debugName ) {

	size_t foundElement = 0;

	for ( const auto& n : apiStore().nameHashes ) {
		if ( n == id ) {
			break;
		}
		++foundElement;
	}

	if ( foundElement == apiStore().nameHashes.size() ) {
		// no element found, we need to add an element
		apiStore().nameHashes.emplace_back( id );
		apiStore().ptrs.emplace_back( nullptr );    // initialise to nullptr
		apiStore().names.emplace_back( debugName ); // implicitly creates a string
	}

	// --------| invariant: foundElement points to correct element

	return foundElement;
}

// ----------------------------------------------------------------------

static void* le_core_get_api( uint64_t id, const char* debugName ) {

	size_t foundElement = produce_api_index( id, debugName );
	return apiStore().ptrs[ foundElement ];
}

// ----------------------------------------------------------------------

static void* le_core_create_api( uint64_t id, size_t apiStructSize, const char* debugName ) {

	size_t foundElement = produce_api_index( id, debugName );

	auto& apiPtr = apiStore().ptrs[ foundElement ];

	if ( apiPtr == nullptr ) {

		// api struct has not yet been allocated, we must do so now.

		void* apiMemory = calloc( 1, apiStructSize ); // allocate space for api pointers on heap
		assert( apiMemory && "Could not allocate memory for API struct." );

		apiPtr = apiMemory; // Store updated address for api - this address won't change for the
		                    // duration of the program.
	}

	return apiPtr;
}

// ----------------------------------------------------------------------

static void le_core_reset_api( void* api, size_t api_size ) {
	memset( api, 0, api_size ); // blank out all entries.
}

// ----------------------------------------------------------------------

ISL_API_ATTR void* le_core_load_module_static( char const* module_name, void ( *module_reg_fun )( void* ), uint64_t api_size_in_bytes ) {
	void* api = le_core_create_api( hash_64_fnv1a_const( module_name ), api_size_in_bytes, module_name );
	module_reg_fun( api );
	return api;
};

// ----------------------------------------------------------------------

ISL_API_ATTR void* le_core_load_module_dynamic( char const* module_name, uint64_t api_size_in_bytes, bool should_watch ) {

	uint64_t module_name_hash = hash_64_fnv1a_const( module_name );

	void* api = le_core_get_api( module_name_hash, module_name );

	if ( module_name_hash == hash_64_fnv1a_const( "le_file_watcher" ) ) {
		// To answer that age-old question: No-one watches the watcher.
		should_watch = false;
	}

	static auto module_loader_i = get_module_loader_api()->le_module_loader_i;

	if ( api == nullptr ) {

		char api_register_fun_name[ 256 ];
		snprintf( api_register_fun_name, 255, "le_module_register_%s", module_name );

#ifdef WIN32
		// On Windows, we dont directly watch the .dll - but we watch a matching .flag file
		// for updates. This allows us to make sure that all write operations to the .dll
		// are complete before we trigger a reload. If we watched the .dll directly, the
		// directory watcher may report multiple file modification events during the build
		// process, which will trigger the hot-reloading mechanism multiple times for the
		// same library (for example if the file is being written into in multiple steps).
		//
		// By using a .flag file we can make sure that we're subscribing to an atomic event
		// which only gets triggered once the build is finished and the .flag file gets
		// touch'ed by the build scripts to signal successful completion of the build.
		std::string         module_path       = "./" + std::string( module_name ) + ".dll";
		std::string         module_watch_path = "./" + std::string( module_name ) + ".flag";
		le_module_loader_o* loader            = module_loader_i.create( module_path.c_str() );
#else
		std::string         module_path       = "./modules/lib" + std::string( module_name ) + ".so";
		std::string         module_watch_path = module_path;
		le_module_loader_o* loader            = module_loader_i.create( module_path.c_str() );
#endif

		defer_delete().loaders.push_back( loader ); // add to cleanup list

		// Important to store api back to table here *before* calling loadApi,
		// as loadApi might recursively add other apis
		// which would have the effect of allocating more than one copy of the api
		//
		api = le_core_create_api( hash_64_fnv1a_const( module_name ), api_size_in_bytes, module_name );

		module_loader_i.load( loader );
		module_loader_i.register_api( loader, api, api_register_fun_name );

		// ----
		if ( should_watch ) {
			loader_callback_params_o* callbackParams = new loader_callback_params_o{};
			callbackParams->api                      = api;
			callbackParams->loader                   = loader;
			callbackParams->lib_register_fun_name    = api_register_fun_name;
			callbackParams->watch_id                 = 0;
			callbackParams->api_size                 = api_size_in_bytes;
			defer_delete().params.push_back( callbackParams ); // add to deferred cleanup list

			le_file_watcher_watch_settings watchSettings = {};

			watchSettings.callback_fun = []( const char*, void* user_data ) {
				auto params = static_cast<loader_callback_params_o*>( user_data );
				le_core_reset_api( params->api, params->api_size );
				module_loader_i.load( params->loader );
				module_loader_i.register_api( params->loader, params->api, params->lib_register_fun_name.c_str() );
			};

			watchSettings.callback_user_data = reinterpret_cast<void*>( callbackParams );
			watchSettings.filePath           = module_watch_path.c_str();

			static auto le_file_watcher_i = get_le_file_watcher_api()->le_file_watcher_i;
			callbackParams->watch_id      = le_file_watcher_i.add_watch( get_file_watcher(), &watchSettings );
		}

	} else {
		// TODO: we should warn that this api was already added.
	}

	return api;
};

// ----------------------------------------------------------------------

ISL_API_ATTR void* le_core_load_library_persistently( char const* library_name ) {
	static auto module_loader_i = get_module_loader_api()->le_module_loader_i;
	return module_loader_i.load_library_persistently( library_name );
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
	std::mutex                                mtx;
	std::unordered_map<uint64_t, std::string> map;
};

static ArgumentNameTable argument_names_table{};

// ----------------------------------------------------------------------

ISL_API_ATTR void le_update_argument_name_table( const char* name, uint64_t value ) {

	// find index of entry with current value in table

	std::scoped_lock lock( argument_names_table.mtx );

	auto result = argument_names_table.map.insert( { value, name } );

	if ( !result.second ) {
		// insertion did not take place
		assert( result.first->second == std::string( name ) && "Possible hash collision, names for hashes don't match!" );
	}
};

// ----------------------------------------------------------------------

ISL_API_ATTR char const* le_get_argument_name_from_hash( uint64_t value ) {

	std::scoped_lock lock( argument_names_table.mtx );
	if ( argument_names_table.map.empty() ) {
		return "<< Argument name table empty. >>";
	}

	auto const& e = argument_names_table.map.find( value );
	if ( e == argument_names_table.map.end() ) {
		return "<< Argument name could not be resolved. >>";

	} else {
		return e->second.c_str();
	}
}

// callback forwarding --------------------------------------------------

#if !defined( NDEBUG ) && defined( __x86_64__ )

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
///     | plt entry   (16 Bytes)      xx xx xx xx xx xx xx | --.        --- top (plt) page
///     | plt entry   (16 Bytes)      xx xx xx xx xx xx xx |   | --.
///     | plt entry   (16 Bytes)      xx xx xx xx xx xx xx |   |   |
///     | plt entry   (16 Bytes)      xx xx xx xx xx xx xx |   |   |
///     | ..                                               |   |   |
///     | got entry 0 (16 Bytes)   xx xx xx xx xx xx xx xx | <-"   |    --- bottom (got) page
///     | got entry 1 (16 Bytes)   xx xx xx xx xx xx xx xx |     <-"
///     | got entry 2 (16 Bytes)   xx xx xx xx xx xx xx xx |
///     | got entry 3 (16 Bytes)   xx xx xx xx xx xx xx xx |
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
	void*                plt_got    = nullptr;
	size_t               plt_got_sz = 0;
	char*                plt_page   = nullptr;
	char*                got_page   = nullptr;
	const size_t         PAGE_SIZE;
	const size_t         MAX_CALLBACK_FORWARDERS_PER_PAGE;
	std::vector<uint8_t> usage_markers; // A bitfield, where each bit represents an entry
	                                    // in the table, 0 where each bit signals whether an entry
	                                    // was used or not. Because we check occupancy before adding a new entry,
	                                    // this helps us to recycle entries which have been released.

	void* plt_at( size_t index ) {
		assert( index < MAX_CALLBACK_FORWARDERS_PER_PAGE && "callback plt index out of bounds" );
		return plt_page + ( index * 16 );
	}

	void* got_at( size_t index ) {
		assert( index < MAX_CALLBACK_FORWARDERS_PER_PAGE && "callback got index out of bounds" );
		return got_page + ( index * 16 );
	}

  public:
	PltGot()
	    : PAGE_SIZE( sysconf( _SC_PAGESIZE ) )
	    , MAX_CALLBACK_FORWARDERS_PER_PAGE( PAGE_SIZE / ( 16 ) )
	    , usage_markers( ( MAX_CALLBACK_FORWARDERS_PER_PAGE + 7 ) / 8, 0 ) // allocate greedily to make sure we have enough bits to cover all
	{

		plt_got_sz = PAGE_SIZE * 2;
		plt_got    = mmap( NULL, plt_got_sz, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0 );
		assert( plt_got != MAP_FAILED && "Map did not succeed" );

		plt_page = static_cast<char*>( plt_got );
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

		int32_t* offset = ( int32_t* )( thunk + 3 ); // get address of offset inside thunk

		*offset = PAGE_SIZE; // set offset to one page.
		*offset -= 7;        // as the current instruction has 7 bytes, we must subtract 7 from the offset value.

		for ( size_t i = 0; i != MAX_CALLBACK_FORWARDERS_PER_PAGE; i++ ) {
			memcpy( plt_page + 16 * i, thunk, 16 );
		}

		// Now we set permissions for this page to be exec + read only.
		// We keep read/write for the second page, as second page will
		// contain the got, which we might want to update externally.

		mprotect( plt_got, PAGE_SIZE, PROT_READ | PROT_EXEC );
	};

	~PltGot() {
		int result = munmap( plt_got, plt_got_sz );
		assert( result != -1 && "unmap failed" );
	};

	bool new_entry( void** plt, void** got ) {
		uint32_t entry = 0;
		size_t   i     = 0;
		for ( ; i != usage_markers.size(); i++ ) {
			if ( usage_markers[ i ] != uint8_t( ~0 ) ) {
				// we will return the first usage marker that has a hole in it
				break;
			}
		}

		if ( i == usage_markers.size() ) {
			return false;
		}

		// ---------| invariant: i is the first index in usage_markers that points to a free entry

		for ( uint32_t offset = 0; ( offset + i * 8 ) < MAX_CALLBACK_FORWARDERS_PER_PAGE; offset++ ) {
			if ( ( usage_markers[ i ] >> offset & uint8_t( 1 ) ) == 0 ) {
				// we found a free element!
				entry = offset + i * 8;
				*plt  = plt_at( entry );
				*got  = got_at( entry );
				usage_markers[ i ] |= ( uint8_t( 1 ) << offset ); // flip this bit to used
				return true;
			}
		}
		return false;
	}

	bool free_entry( void* plt ) {
		if ( plt < plt_page || plt >= plt_page + 16 * MAX_CALLBACK_FORWARDERS_PER_PAGE ) {
			// plt cannot be in this table as it is outside our address range
			return false;
		}

		// Todo: implement.
		// first find entry, then set it to unused, zero out the appropriate marker
		uint32_t entry = ( ( char* )plt - ( char* )plt_page ) / 16;

		usage_markers[ entry / 8 ] &= ~( uint8_t( 1 ) << ( entry % 8 ) );

		return true;
	}

  private:
	// Intrusive list.
	friend class PltGotForwardList;
	std::unique_ptr<PltGot> list_next;
};

// We use our own intrusive list - this is so that we can enforce thread safety
// and automatic destruction of plt got table entries.
class PltGotForwardList {
	std::mutex              mtx;
	std::unique_ptr<PltGot> list;

  public:
	void next_entry( void** plt, void** got ) {
		auto lck = std::unique_lock( mtx );
		while ( list.get() == nullptr || list.get()->new_entry( plt, got ) == false ) {
			auto new_table = std::make_unique<PltGot>();
			std::swap( new_table->list_next, list );
			std::swap( list, new_table );
		}
	}

	// ----------------------------------------------------------------------

	void release_entry( void* plt ) {
		auto lck = std::unique_lock( mtx );

		if ( list.get() == nullptr ) {
			// no entries in list, ergo nothing to release.
			return;
		}

		PltGot* list_item = list.get();

		while ( list_item != nullptr && ( false == list_item->free_entry( plt ) ) ) {
			list_item = list_item->list_next.get();
		};
	}
};

// ----------------------------------------------------------------------

static PltGotForwardList* le_core_get_plt_got_forward_list() {
	// We use a getter function for our static plt_got singleton so that we can guarantee
	// that this static variable gets instantiated on first use.
	//
	// By using it as an object, we can be sure that its destructor will get called
	// upon program exit.
	static PltGotForwardList plt_got{};
	return &plt_got;
}

// ----------------------------------------------------------------------

void* le_core_get_callback_forwarder_addr_impl( void* callback_addr ) {

	static PltGotForwardList* plt_got = le_core_get_plt_got_forward_list();

	void* plt_addr = nullptr;
	void* got_addr = nullptr;

	plt_got->next_entry( &plt_addr, &got_addr );

	// copy value of callback pointer into got table
	memcpy( got_addr, &callback_addr, sizeof( void* ) );

	return plt_addr;
};

// ----------------------------------------------------------------------

void le_core_release_callback_forwarder_addr_impl( void* plt_addr ) {
	static PltGotForwardList* plt_got = le_core_get_plt_got_forward_list();
	plt_got->release_entry( plt_addr );
}

#endif
// end callback forwarding-----------------------------------------------
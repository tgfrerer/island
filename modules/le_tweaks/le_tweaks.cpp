#include "le_tweaks.h"
#include "le_core.h"

#include <stdint.h>
#include "le_core.h"

#include "le_file_watcher.h"
#include "le_log.h" ///< if you get an error message saying that this header can't be found, make sure that the module that uses le_tweakable has a line saying `depends_on_island_module(le_log)` in its CMake file.

#include <fstream>
#include <iostream>

#include <cstring>
#include <string>
#include <unordered_map>

// ----------------------------------------------------------------------
// We use `class` here purely for RAAI, to ensure the destructor
// gets called if the object gets deleted.
class FileWatcher : NoCopy, NoMove {
    le_file_watcher_o* self = le_file_watcher_api_i->le_file_watcher_i.create();

  public:
    FileWatcher() = default;
    ~FileWatcher() {
        le_file_watcher_api_i->le_file_watcher_i.destroy( self );
    }

	operator auto() {
		return self;
	}
};

// ----------------------------------------------------------------------
// Note that we use a wrapper class so that the watcher gets deleted
// automatically when the compilation unit which contains it gets unloaded.
static FileWatcher aux_source_watcher{};

using CbData = le_tweaks_api::CbData;

struct tweak_entry_t {
	CbData* cb_data  = nullptr;
	int     watch_id = 0;
};

static auto& fetch_existing_tweaks_per_file() {
	static std::unordered_map<std::string, tweak_entry_t> existing_tweaks_per_file;
	return existing_tweaks_per_file;
}

static int le_tweaks_add_watch( CbData* cb_data ) {

	le_file_watcher_watch_settings watch;
	watch.filePath           = cb_data->file_path;
	watch.callback_user_data = cb_data;

	/* We only want to open source files once per-file.
	 *
	 * We can do this by adding only one single watch per-file, and chaining any tweaks that apply to a file
	 * that we are already watching to the first watch entry by way of a linked list.
	 *
	 * When a cpp file gets hot-reloaded, we must remove any watches that apply to this file. We do this by
	 * triggering an explicit destructor on the first watch entry for this file when the library gets unloaded.
	 *
	 * This destructor then removes the watch associated with the file. (See le_tweaks_destroy_watch) -
	 * this gets called via the destructor of CbData if the CbData object has an explicit destructor pointer
	 * set.
	 *
	 */

	tweak_entry_t cb_entry{ cb_data, -1 };

	auto [ tweak, was_inserted ] = fetch_existing_tweaks_per_file().try_emplace( cb_data->file_path, cb_entry );

	if ( was_inserted ) {
		// A new entry was inserted - we must set its explicit destructor
		// so that this entry knows to remove its file watcher.
		//
		// Only the first entry has an actual file watch associated with it,
		// subsequent tweaks are just linked to the first entry so that
		// for each file, all linked entries get evaluated.
		//
		LeLog( "le_tweaks" ).info( "+ WATCH: %s", cb_data->file_path );

		cb_data->p_watch_destructor = le_tweaks::le_tweaks_i.destroy_watch;
		cb_data->next               = nullptr;

		// This callback will get triggered by the file watcher when
		// our watched source file gets updated:
		watch.callback_fun = []( const char* path, void* user_data ) -> void {
			auto cb_data = static_cast<CbData*>( user_data );

			// Open file read-only.
			// Print line at correct line number.

			std::ifstream file( path, std::ios::in );

			if ( !file.is_open() ) {

				LeLog( "le_tweaks" ).error( "Unable to open file: '%'", path );
				return;
			}

			bool tweaks_remaining = true; // whether there are tweaks left to process.
			                              // we use this flag to signal early exit if
			                              // we know that there are no more tweaks left.

			// line number  means we must seek until we find the number of newlines
			// then we may extract the token which sets the value.

			size_t current_line_num = 1;
			while ( file.good() && tweaks_remaining ) {
				std::string str;
				std::getline( file, str );

				char const* str_start = str.c_str();

				static auto logger = LeLog( "le_tweaks" );

				while ( current_line_num == cb_data->line_num ) {

					// Find chunk which actually designates data:

					str_start = strstr( str_start, "LE_TWEAK" );

					if ( str_start == nullptr ) {
						logger.warn( "Could not tweak line %d.", cb_data->line_num );
						logger.warn( "Line contents: '%s'", str.c_str() );
						return;
					}

					CbData::Data old_data;
					old_data.raw = cb_data->data.raw;

					// Set data based on data type
					//
					// note: Whitespace in scanf format string means "zero or many"
					//
					switch ( cb_data->type ) {
					case CbData::Type::u64:
						sscanf( str_start, "LE_TWEAK ( %lu ) ", &cb_data->data.u64 );
						break;
					case CbData::Type::i64:
						sscanf( str_start, "LE_TWEAK ( %li ) ", &cb_data->data.i64 );
						break;
					case CbData::Type::i32:
						sscanf( str_start, "LE_TWEAK ( %d ) ", &cb_data->data.i32 );
						break;
					case CbData::Type::u32:
						sscanf( str_start, "LE_TWEAK ( %ud ) ", &cb_data->data.u32 );
						break;
					case CbData::Type::f32:
						sscanf( str_start, "LE_TWEAK ( %f ) ", &cb_data->data.f32 );
						break;
					case CbData::Type::f64:
						sscanf( str_start, "LE_TWEAK ( %lf ) ", &cb_data->data.f64 );
						break;
					case CbData::Type::b32: {
						char token[ 6 ];
						sscanf( str_start, "LE_TWEAK ( %5c ) ", token );
						if ( strncmp( token, "true", 4 ) == 0 ) {
							cb_data->data.b32 = true;
						} else if ( strncmp( token, "false", 5 ) == 0 ) {
							cb_data->data.b32 = false;
						} else {
							// invalid token
						}

					} break;
					} // end switch ( cb_data->type )

					if ( cb_data->data.raw != old_data.raw && str_start ) {
						// Applied tweak.
						long long   len = strstr( str_start, ")" ) - str_start;
						std::string s   = { str_start, str_start + len + 1 };
						logger.info( "> TWEAK %s:%d", cb_data->file_path, current_line_num );
					}

					// -- Check if there is a next tweak in our linked list of tweaks.
					// If yes, we must continue processing the file.
					//   Is it on the same line?
					//     In that case offset str_start, and process line again
					if ( cb_data->next ) {
						if ( cb_data->next->line_num == cb_data->line_num ) {
							// We offset `str_start` by one, so that we can begin searching
							// for the next `LE_TWEAK` token on the same line.
							cb_data = cb_data->next;
							str_start++;
							continue;
						} else {
							cb_data = cb_data->next;
						}
					} else {
						// No next element in cb_data.
						//
						// We can signal that we're done with this file.
						tweaks_remaining = false;
					}
					break;
				}
				current_line_num++;

			} // while ( file.good() )

			file.close();
		}; // watch.callback_fun

		tweak->second.watch_id = le_file_watcher_api_i->le_file_watcher_i.add_watch( aux_source_watcher, &watch );
	} else {

		// There was already a watch set up for this file - we add this tweak
		// to the linked list of tweaks that already exist for this file.
		CbData* p_last = tweak->second.cb_data;
		while ( p_last->next ) {
			p_last = p_last->next;
		}
		p_last->next = cb_data;
	}

	// you need to batch watches by file, and count them. once the last watch for a file is gone,
	// you can remove the file from the list of watchers.
	//
	// note that the container of previous watches will be zero if the
	// tweak library gets reloaded.

	LeLog( "le_tweaks" ).info( "+ TWEAK: %s:%d", cb_data->file_path, cb_data->line_num );

	return tweak->second.watch_id;
};

// ----------------------------------------------------------------------

static void le_tweaks_update() {
	le_file_watcher_api_i->le_file_watcher_i.poll_notifications( aux_source_watcher );
}

// ----------------------------------------------------------------------

static void le_tweaks_destroy_watch( CbData* self ) {

	// Try to find a watch for the file path referenced by `self`.
	auto& tweaks = fetch_existing_tweaks_per_file();
	auto  it     = tweaks.find( self->file_path );

	if ( it != tweaks.end() ) {
		// We found an entry - this means that a watch for this file path exists. We must remove the watch.
		LeLog( "le_tweaks" ).info( "- WATCH: %s", self->file_path );
		le_file_watcher_api_i->le_file_watcher_i.remove_watch( aux_source_watcher, it->second.watch_id );
		tweaks.erase( it );
	}

	// Report that we removed all tweaks for this watched file
	CbData* list_entry = self;

	do {
		LeLog( "le_tweaks" ).info( "- TWEAK: %s:%d", list_entry->file_path, list_entry->line_num );
		list_entry = list_entry->next;
	} while ( list_entry != nullptr );

	self->next = nullptr;
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_tweaks, api ) {
	auto& le_tweaks_i = static_cast<le_tweaks_api*>( api )->le_tweaks_i;

	le_tweaks_i.update        = le_tweaks_update;
	le_tweaks_i.add_watch     = le_tweaks_add_watch;
	le_tweaks_i.destroy_watch = le_tweaks_destroy_watch;
}

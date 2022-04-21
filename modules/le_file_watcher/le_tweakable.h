#ifndef GUARD_PAL_TWEAKABLE_H
#define GUARD_PAL_TWEAKABLE_H

/*

  Tweakable allows you to tweak numerical parameters. It will only work in debug mode,
  and in Release mode it will melt down to nothing.

  Each compilation unit which has tweaks must include this header, and must, at the
  most convenient time, call

        UPDATE_TWEAKS();

  Which is the polling method for tweaks. Calling this will trigger callbacks if source
  file changes have been detected via the file watcher.

  To tweak individual values, set them as such:

        int myVal = TWEAK(10);

  Important: You must only place one tweakable value per line.

  Note: This header file includes a few headers - which is a break with the #1 rule,
  but there's no good way around it for now...

  Thanks to Dennis Gustafsson, who originally described this technique in his blog:
  http://blog.tuxedolabs.com/2018/03/13/hot-reloading-hardcoded-parameters.html

*/

#ifndef NDEBUG

#	include <stdint.h>
#	include "le_core.h"

#	include "le_file_watcher.h"
#	include "le_log.h"

#	include <fstream>
#	include <iostream>

#	include <cstring>

// ----------------------------------------------------------------------

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

struct CbData {

	enum Type : uint32_t {
		u64,
		i64,
		i32,
		u32,
		f32,
		f64,
		b32, // 32 bit bool
	};

	union Data {
		uint64_t u64;
		int64_t  i64;
		double   f64;
		uint32_t u32;
		int32_t  i32;
		float    f32;
		bool     b32;
		uint64_t raw;
	};

	uint32_t       line_num;
	Type           type;
	Data           data;
	struct CbData* next; // linked list.

#	define INITIALISER( T, TID )                          \
		explicit CbData( uint32_t line_num_, TID param ) { \
			line_num = line_num_;                          \
			data.T   = param;                              \
			type     = Type::T;                            \
			next     = nullptr;                            \
		}

	INITIALISER( u64, uint64_t )
	INITIALISER( u32, uint32_t )
	INITIALISER( i32, int32_t )
	INITIALISER( i64, int64_t )
	INITIALISER( f32, float )
	INITIALISER( f64, double )
	INITIALISER( b32, bool )

#	undef INITIALISER
};

static int tweakable_add_watch( CbData* cb_data, char const* file_path ) {

	le_file_watcher_watch_settings watch;
	watch.filePath           = file_path;
	watch.callback_user_data = cb_data;

	// Only open source file once per translation unit - we ensure this
	// by storing all callback data in a linked list, and we're
	// adding to the end of the linked list if we detect that
	// there's already an element in the list.
	//
	// The callback itself is only added once, but will receive its user_data
	// parameter containing a linked list of all other tweakable watches for
	// this file.
	//
	// If the file triggers a callback, we go through all elements
	// in the linked list of callback parameters, and apply the values we
	// parse from the file at the given line numbers per list item.
	static CbData* has_previous_cb = nullptr;

	if ( nullptr == has_previous_cb ) {

		watch.callback_fun = []( const char* path, void* user_data ) -> void {
			auto cb_data = static_cast<CbData*>( user_data );

			// Open file read-only.
			// Print line at correct line number.

			std::ifstream file( path, std::ios::in );

			if ( !file.is_open() ) {

				LeLog( "le_tweakable" ).error( "Unable to open file: '%'", path );
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

				static auto logger = LeLog( "le_tweakable" );

				while ( current_line_num == cb_data->line_num ) {

					// Find chunk which actually designates data:

					str_start = strstr( str_start, "TWEAK" );

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
						sscanf( str_start, "TWEAK ( %lu ) ", &cb_data->data.u64 );
						break;
					case CbData::Type::i64:
						sscanf( str_start, "TWEAK ( %li ) ", &cb_data->data.i64 );
						break;
					case CbData::Type::i32:
						sscanf( str_start, "TWEAK ( %d ) ", &cb_data->data.i32 );
						break;
					case CbData::Type::u32:
						sscanf( str_start, "TWEAK ( %ud ) ", &cb_data->data.u32 );
						break;
					case CbData::Type::f32:
						sscanf( str_start, "TWEAK ( %f ) ", &cb_data->data.f32 );
						break;
					case CbData::Type::f64:
						sscanf( str_start, "TWEAK ( %lf ) ", &cb_data->data.f64 );
						break;
					case CbData::Type::b32: {
						char token[ 6 ];
						sscanf( str_start, "TWEAK ( %5c ) ", token );
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
						long        len = strstr( str_start, ")" ) - str_start;
						std::string s   = { str_start, str_start + len + 1 };
						logger.info( "Applied tweak at line #", current_line_num );
					}

					// -- Check if there is a next tweak in our linked list of tweaks.
					// If yes, we must continue processing the file.
					//   Is it on the same line?
					//     In that case offset str_start, and process line again
					if ( cb_data->next ) {
						if ( cb_data->next->line_num == cb_data->line_num ) {
							// We offset `str_start` by one, so that we can begin searching
							// for the next `TWEAK` token on the same line.
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

		has_previous_cb = cb_data;

		return le_file_watcher_api_i->le_file_watcher_i.add_watch( aux_source_watcher, &watch );
	} else {
		// Add to linked list instead of adding a new callback for this file.
		has_previous_cb->next = cb_data;
		has_previous_cb       = cb_data;
		return 0;
	}
};

	// ----------------------------------------------------------------------

#	define TWEAK( x )                                                                              \
		[]( auto val, uint32_t line, char const* file_path )                                        \
		    -> decltype( val )& {                                                                   \
			static CbData cb_data( line, val );                                                     \
			static int    val_watch = tweakable_add_watch( &cb_data, file_path );                   \
			( void )val_watch; /* <- this does nothing, only to suppress unused variable warning */ \
			return reinterpret_cast<decltype( val )&>( cb_data.data );                              \
		}( x, __LINE__, __FILE__ )

	// ----------------------------------------------------------------------

#	define UPDATE_TWEAKS() \
		le_file_watcher_api_i->le_file_watcher_i.poll_notifications( aux_source_watcher )

#else

// In Release, we don't want to be able to tweak - therefore all tweak macros evaluate to
// the values themselves.
#	define TWEAK( x ) x

// There is nothing to update, therefore this macro is a no-op.
#	define UPDATE_TWEAKS()

#endif // ifndef NDEBUG

#endif // GUARD_PAL_TWEAKABLE_H

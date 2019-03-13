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
#	include "pal_api_loader/ApiRegistry.hpp"

#	include "pal_file_watcher/pal_file_watcher.h"

#	include <fstream>
#	include <iostream>

#	include <cstring>

// ----------------------------------------------------------------------

static auto &aux_file_watcher_i = *Registry::getApi<pal_file_watcher_i>();

// ----------------------------------------------------------------------
// We use `class` here purely for RAAI, to ensure the destructor
// gets called if the object gets deleted.
class FileWatcher : NoCopy, NoMove {
	pal_file_watcher_o *self = aux_file_watcher_i.create();

  public:
	FileWatcher() = default;
	~FileWatcher() {
		aux_file_watcher_i.destroy( self );
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

	uint32_t line_num;
	Type     type;
	Data     data;

#	define INITIALISER( T, TID )                          \
	    explicit CbData( uint32_t line_num_, TID param ) { \
	        line_num = line_num_;                          \
	        data.T   = param;                              \
	        type     = Type::T;                            \
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

static int tweakable_add_watch( CbData *cb_data, char const *file_path ) {

	pal_file_watcher_watch_settings watch;
	watch.filePath           = file_path;
	watch.callback_user_data = cb_data;

	watch.callback_fun = []( const char *path, void *user_data ) -> bool {
		auto cb_data = static_cast<CbData *>( user_data );

		// Open file read-only.
		// Print line at correct line number.

		std::ifstream file( path, std::ios::in );

		if ( !file.is_open() ) {
			std::cerr << "Unable to open file: " << path << std::endl
			          << std::flush;
			return false;
		}

		// line number  means we must seek until we find the number of newlines
		// then we may extract the token which sets the value.

		size_t current_line_num = 1;
		while ( file.good() ) {
			std::string str;
			std::getline( file, str );

			if ( current_line_num == cb_data->line_num ) {

				// Find chunk which actually designates data:

				auto start = strstr( str.c_str(), "TWEAK" );

				if ( start == nullptr ) {
					std ::cout << "Could not tweak line: " << std::dec << cb_data->line_num << std::endl
					           << std::flush;
					std::cout << "Line contents: " << str << std::endl
					          << std::flush;
					return false;
				}

				CbData::Data old_data;
				old_data.raw = cb_data->data.raw;

				// Set data based on data type
				//
				// note: Whitespace in scanf format string means "zero or many"
				//
				switch ( cb_data->type ) {
				case CbData::Type::u64:
					sscanf( start, "TWEAK ( %lu ) ", &cb_data->data.u64 );
				    break;
				case CbData::Type::i64:
					sscanf( start, "TWEAK ( %li ) ", &cb_data->data.i64 );
				    break;
				case CbData::Type::i32:
					sscanf( start, "TWEAK ( %d ) ", &cb_data->data.i32 );
				    break;
				case CbData::Type::u32:
					sscanf( start, "TWEAK ( %ud ) ", &cb_data->data.u32 );
				    break;
				case CbData::Type::f32:
					sscanf( start, "TWEAK ( %f ) ", &cb_data->data.f32 );
				    break;
				case CbData::Type::f64:
					sscanf( start, "TWEAK ( %lf ) ", &cb_data->data.f64 );
				    break;
				case CbData::Type::b32: {
					char token[ 6 ];
					sscanf( start, "TWEAK ( %5c ) ", token );
					if ( strncmp( token, "true", 4 ) == 0 ) {
						cb_data->data.b32 = true;
					} else if ( strncmp( token, "false", 5 ) == 0 ) {
						cb_data->data.b32 = false;
					} else {
						// invalid token
					}

				} break;
				} // end switch ( cb_data->type )

				if ( cb_data->data.raw != old_data.raw ) {
					std::cout << "Applied tweak: " << str << std::endl
					          << std::flush;
				}

				break;
			}

			++current_line_num;
		} // while ( file.good() )

		file.close();

		return true;
	}; // watch.callback_fun

	return aux_file_watcher_i.add_watch( aux_source_watcher, watch );
};

    // ----------------------------------------------------------------------

#	define TWEAK( x )                                                                              \
	    []( auto val, uint32_t line, char const *file_path )                                        \
	        -> decltype( val ) & {                                                                  \
	        static CbData cb_data( line, val );                                                     \
	        static int    val_watch = tweakable_add_watch( &cb_data, file_path );                   \
	        ( void )val_watch; /* <- this does nothing, only to suppress unused variable warning */ \
	        return reinterpret_cast<decltype( val ) &>( cb_data.data );                             \
	    }( x, __LINE__, __FILE__ )

    // ----------------------------------------------------------------------

#	define UPDATE_TWEAKS() \
	    aux_file_watcher_i.poll_notifications( aux_source_watcher )

#else

// In Release, we don't want to be able to tweak - therefore all tweak macros evaluate to
// the values themselves.
#	define TWEAK( x ) x

// There is nothing to update, therefore this macro is a no-op.
#	define UPDATE_TWEAKS()

#endif // ifndef NDEBUG

#endif // GUARD_PAL_TWEAKABLE_H

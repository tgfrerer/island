#ifndef GUARD_le_tweaks_H
#define GUARD_le_tweaks_H

#include "le_core.h"

/*

  Tweakable allows you to tweak numerical parameters. It will only work in debug mode,
  and in Release mode it will melt down to nothing.

  Each compilation unit which has tweaks must include this header, and must, at the
  most convenient time, call

        LE_UPDATE_TWEAKS();

  Which is the polling method for tweaks. Calling this will trigger callbacks if source
  file changes have been detected via the file watcher.

  To tweak individual values, set them as such:

        int myVal = LE_TWEAK(10);

  Important: You must only place one tweakable value per line.

  Thanks to Dennis Gustafsson, who originally described this technique in his blog:
  http://blog.tuxedolabs.com/2018/03/13/hot-reloading-hardcoded-parameters.html

*/

struct le_tweaks_api {

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

		uint32_t    line_num;
		Type        type;
		Data        data;
		char const* file_path;

		struct CbData* next; // linked list.

#define INITIALISER( T, TID )                                            \
	explicit CbData( uint32_t line_num_, TID param, const char* path ) { \
	    line_num  = line_num_;                                           \
	    data.T    = param;                                               \
	    type      = Type::T;                                             \
	    file_path = path;                                                \
	    next      = nullptr;                                             \
	}

		INITIALISER( u64, uint64_t )
		INITIALISER( u32, uint32_t )
		INITIALISER( i32, int32_t )
		INITIALISER( i64, int64_t )
		INITIALISER( f32, float )
		INITIALISER( f64, double )
		INITIALISER( b32, bool )

#undef INITIALISER

		void ( *p_watch_destructor )( CbData* self ); // f

		~CbData() {
			// we call the destructor so that we can clean up any callbacks on file reload
			// before they get triggered.
			if ( p_watch_destructor ) {
				p_watch_destructor( this );
			}
		};
	};

	// clang-format off
	struct le_tweaks_interface_t {
		void ( *update    )();
		int  ( *add_watch )( CbData* cb_data );
		void (*destroy_watch)(CbData* cb_data);
	};
	// clang-format on

	le_tweaks_interface_t le_tweaks_i;
};

LE_MODULE( le_tweaks );
LE_MODULE_LOAD_DEFAULT( le_tweaks );

#ifdef __cplusplus

namespace le_tweaks {
static const auto& api         = le_tweaks_api_i;
static const auto& le_tweaks_i = api->le_tweaks_i;
} // namespace le_tweaks
#endif // __cplusplus

#ifndef NDEBUG

// ----------------------------------------------------------------------

#	define LE_TWEAK( x )                                                                                \
	    []( auto val )                                                                                   \
	        -> decltype( val )& {                                                                        \
	        static char const*           file_path = __FILE__;                                           \
	        static le_tweaks_api::CbData cb_data( __LINE__, val, file_path );                            \
	        static int                   val_watch = le_tweaks_api_i->le_tweaks_i.add_watch( &cb_data ); \
	        ( void )val_watch; /* <- this does nothing, only to suppress unused variable warning */      \
	        return reinterpret_cast<decltype( val )&>( cb_data.data );                                   \
	    }( x )

// ----------------------------------------------------------------------

#	define LE_UPDATE_TWEAKS() \
	    le_tweaks_api_i->le_tweaks_i.update()

#else // ifdef NDEBUG

// In Release, we don't want to be able to tweak - therefore all tweak macros evaluate to
// the values themselves.
#	define TWEAK( x ) x

// There is nothing to update, therefore this macro is a no-op.
#	define UPDATE_TWEAKS()

#endif // ifndef NDEBUG

#endif

#ifndef GUARD_le_console_H
#define GUARD_le_console_H

#include "le_core.h"

// This module allows you to remote-control an island app -
// It will listen for connections on port 3535/tcp.
// The console will run on its own thread.

// The console should also be able to mirror log messages
// - std::out and std::err should be mirrored to the console.
// see: https://wordaligned.org/articles/cpp-streambufs

struct le_console_o;

// clang-format off
struct le_console_api {

	struct le_console_interface_t {

		le_console_o *    ( * create                   ) ( );
        
        bool (* server_start)( le_console_o* self );
        bool (* server_stop)( le_console_o* self );

		void              ( * destroy                  ) ( le_console_o* self );

	};

	le_console_interface_t       le_console_i;
};
// clang-format on

LE_MODULE( le_console );
LE_MODULE_LOAD_DEFAULT( le_console );

#ifdef __cplusplus

namespace le_console {
static const auto& api          = le_console_api_i;
static const auto& le_console_i = api->le_console_i;
} // namespace le_console

class LeConsole : NoCopy, NoMove {

	le_console_o* self;

  public:
	LeConsole()
	    : self( le_console::le_console_i.create() ) {
	}

	~LeConsole() {
		le_console::le_console_i.destroy( self );
	}

	bool serverStart() {
		return le_console::le_console_i.server_start( self );
	}

	bool serverStop() {
		return le_console::le_console_i.server_stop( self );
	}

	operator auto() {
		return self;
	}
};

#endif // __cplusplus

#endif

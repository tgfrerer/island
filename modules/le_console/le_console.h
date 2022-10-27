#ifndef GUARD_le_console_H
#define GUARD_le_console_H

#include "le_core.h"

// This module allows you to remote-control an island app -
// It will listen for connections on port 3535/tcp.
//
// The console server runs on its own thread, and it
// communicates with the console over thread-safe channels.
//
// The console server is only started on-demand.

struct le_console_o;

// clang-format off
struct le_console_api {

	struct le_console_interface_t {

		void ( * inc_use_count ) ( );
		void ( * dec_use_count ) ( );
        
        bool ( * server_start)();
        bool ( * server_stop)();

        void ( * process_input )(); // process input from connected clients
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

  public:
	LeConsole() {
		le_console::le_console_i.inc_use_count();
	}

	~LeConsole() {
		le_console::le_console_i.dec_use_count();
	}

	bool serverStart() {
		return le_console::le_console_i.server_start();
	}

	bool serverStop() {
		return le_console::le_console_i.server_stop();
	}

	static void processInput() {
		le_console::le_console_i.process_input();
	}
};

namespace le {
using Console = LeConsole;
}

#endif // __cplusplus

#endif

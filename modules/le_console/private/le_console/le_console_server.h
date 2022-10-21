#pragma once

struct le_console_server_o;

struct le_console_server_api_t {

	le_console_server_o* ( *create )( struct le_console_o* console );
	void ( *destroy )( le_console_server_o* self );

	void ( *start )( le_console_server_o* self );
	void ( *start_thread )( le_console_server_o* self );
	void ( *stop_thread )( le_console_server_o* self );
	void ( *stop )( le_console_server_o* self );
};

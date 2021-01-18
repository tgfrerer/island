#include "le_log.h"
#include "le_core/le_core.h"

#include <atomic>
#include <mutex>
#include <unordered_map>

struct le_log_module_o {
	std::string     name      = "DEFAULT";
	std::atomic_int log_level = 1;
};

static le_log_module_o *le_log_module_default() {
	static auto module = new le_log_module_o();
	return module;
}

static le_log_module_o *le_log_get_module( const char *name ) {
	static std::unordered_map<std::string, le_log_module_o *> modules;
	static std::mutex                                         mtx;

	std::scoped_lock g( mtx );
	if ( modules.find( name ) == modules.end() ) {
		auto module     = new le_log_module_o();
		module->name    = name;
		modules[ name ] = module;
		return module;
	}
	return modules[ name ];
}

static void le_log_set_level( le_log_module_o *module, le_log::Level level ) {
	if ( !module ) {
		module = le_log_module_default();
	}
	module->log_level = static_cast<std::underlying_type<le_log::Level>::type>( level );
}

static const char *le_log_level_name( le_log::Level level ) {
	switch ( level ) {
	case le_log::Level::DEBUG:
		return "DEBUG";
	case le_log::Level::INFO:
		return "INFO";
	case le_log::Level::WARN:
		return "WARN";
	case le_log::Level::ERROR:
		return "ERROR";
	}
	return "";
}

static void le_log_printf( const le_log_module_o *module, le_log::Level level, const char *msg, va_list args ) {

	if ( !module ) {
		module = le_log_module_default();
	}

	if ( module->log_level > static_cast<std::underlying_type<le_log::Level>::type>( level ) ) {
		return;
	}

	auto file = stdout;

	if ( level == le_log::Level::ERROR ) {
		file = stderr;
	}

	fprintf( file, "[ %s | %s ] ", module->name.c_str(), le_log_level_name( level ) );

	vfprintf( file, msg, args );
	fprintf( file, "\n" );
}

template <le_log::Level level>
static void le_log_implementation( const le_log_module_o *module, const char *msg, ... ) {
	va_list arglist;
	va_start( arglist, msg );
	le_log_printf( module, level, msg, arglist );
	va_end( arglist );
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_log, api ) {
	auto le_api        = static_cast<le_log_api *>( api );
	le_api->debug      = le_log_implementation<le_log::Level::DEBUG>;
	le_api->info       = le_log_implementation<le_log::Level::INFO>;
	le_api->warn       = le_log_implementation<le_log::Level::WARN>;
	le_api->error      = le_log_implementation<le_log::Level::ERROR>;
	le_api->get_module = le_log_get_module;
	le_api->set_level  = le_log_set_level;
}

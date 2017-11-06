#ifndef GUARD_LOGGER_H
#define GUARD_LOGGER_H

#include <stdint.h>
#include "registry/ApiRegistry.hpp"

#ifdef __cplusplus
extern "C" {
#endif

void register_logger_api( void *api );

struct pal_logger_o;

struct pal_logger_api {
	static constexpr auto id      = "logger";
	static constexpr auto pRegFun = register_logger_api;

	struct logger_interface_t{
		pal_logger_o *( *create )();
		void ( *destroy )( pal_logger_o *obj );
		void ( *append ) ( pal_logger_o *obj, const char *message );
		void ( *flush )  ( pal_logger_o *obj );
	} logger_i;
};

#ifdef __cplusplus
} // extern "C"

namespace pal {

class Logger {
	pal_logger_api::logger_interface_t const *mInterface;
	pal_logger_o *      mObj;

  private:
	// Note this class disables copy constructor and copy assignment operator,
	// as the object held by the logger contains an osstream,
	// for which there exist no copy operation.

	// Also, this class disables move operators, as a move will trigger the
	// destructor, and we hijack the destructor to print to the log.

	// copy assignment operator
	Logger &operator=( const Logger &rhs ) = delete;

	// copy constructor
	Logger( const Logger &rhs ) = delete;

	// move assignment operator
	Logger &operator=( Logger &&rhs ) = delete;

	// move constructor
	Logger( const Logger &&rhs ) = delete;

  public:
	// default constructor
	Logger()
	    : mInterface( &Registry::getApi<pal_logger_api>()->logger_i )
	    , mObj( mInterface->create() ) {
	}

	~Logger() {
		mInterface->flush( mObj );
		mInterface->destroy( mObj );
	}

	Logger &operator<<( const char *message_ ) {
		mInterface->append( mObj, message_ );
		return *this;
	}

	void flush() {
		mInterface->flush( mObj );
	}
};

} // namespace pal

#endif

#endif

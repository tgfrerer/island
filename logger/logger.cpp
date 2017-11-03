#include "logger/logger.h"

#include <iomanip>
#include <iostream>


struct pal_logger_o {
	std::ostringstream buffer;
};

static pal_logger_o *create() {
	auto tmp = new pal_logger_o();
	return tmp;
}

static void append( pal_logger_o *instance_, const char *message_ ) {
	instance_->buffer << message_;
}

static void flush( pal_logger_o *instance ) {
	std::cout << "[ NOTICE ] " << instance->buffer.str() ;
	std::cout << std::flush;
	instance->buffer.clear();
}

static void destroy( pal_logger_o *instance ) {
	delete instance;
}


void register_logger_api( void *api ) {
	auto a     = static_cast<pal_logger_i *>( api );
	a->create  = create;
	a->destroy = destroy;
	a->append  = append;
	a->flush   = flush;
}




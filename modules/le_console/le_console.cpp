#include "le_console.h"
#include "le_core.h"
#include "le_log.h"

#include <mutex>
#include <thread>

#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <vector>
#include <iostream>
#include <fstream>
#include <condition_variable>

#include <poll.h>

#include <deque>

/*

Hot-reloading is more complicated for this module.

What we are doing is that we use a static Raii object to detect if the module is being destroyed,
in which case we use this Raii object to first remove us as a listener from the log object, and
then we end and join the server thread.

once that is done, this module can be safely hot-reloaded.

another thing we must do is that we want to restart the server once that the module has been reloaded.
we can do this by adding a singleton object and thereby detecting the state of play.

Ideally we find out whether we are hot-reloading or actually closing, and we close the network
connection only when we are really closing. that way we can keep clients connected during the
hot-reloading process.


is this a general problem with hot-reloading and multi-threading?
- quite possibly, we need to look more into this.

*/

static constexpr auto LOG_CHANNEL = "le_console";
static constexpr auto PORT        = "3535";
static constexpr auto BACKLOG     = 3;

struct le_console_o {
	// members
	bool        is_running = false;
	bool        should_run = true;
	std::thread server_thread;
	uint64_t    log_subscriber_handle = 0;

	std::mutex              messages_mtx;
	std::deque<std::string> messages;
};

// ----------------------------------------------------------------------

static le_console_o* le_console_create() {
	auto self = new le_console_o();
	return self;
}

// ----------------------------------------------------------------------
// get sockaddr, IPv4 or IPv6:
static void* get_in_addr( struct sockaddr* sa ) {
	if ( sa->sa_family == AF_INET ) {
		return &( ( ( struct sockaddr_in* )sa )->sin_addr );
	}

	return &( ( ( struct sockaddr_in6* )sa )->sin6_addr );
}

class RAIITest {
  public:
	RAIITest() {
		static auto logger = le::Log( LOG_CHANNEL );
		logger.info( "created thread-owned object" );
	};

	~RAIITest() {
		static auto logger = le::Log( LOG_CHANNEL );
		logger.info( "destroyed thread-owned object" );
	}
};

static void server_thread( le_console_o* self ) {
	static auto logger = le::Log( LOG_CHANNEL );
	logger.info( "initializing server thread" );

	RAIITest testobj;

	int              listener                      = 0; // listen on sock_fd
	addrinfo         hints                         = {};
	addrinfo*        servinfo                      = nullptr;
	addrinfo*        p                             = nullptr;
	sockaddr_storage remote_addr                   = {}; // connector's address information
	socklen_t        remote_addr_len               = 0;
	char             remote_ip[ INET6_ADDRSTRLEN ] = {};
	int              yes                           = 1;
	int              rv                            = 0;
	char             buf[ 1024 ]                   = {}; // Buffer for client data

	hints.ai_family   = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags    = AI_PASSIVE; // use my IP

	if ( ( rv = getaddrinfo( NULL, PORT, &hints, &servinfo ) ) != 0 ) {
		logger.error( "getaddrinfo: %s\n", gai_strerror( rv ) );
		return;
	}
	// loop through all the results and bind to the first we can
	for ( p = servinfo; p != NULL; p = p->ai_next ) {
		if ( ( listener = socket( p->ai_family, p->ai_socktype, p->ai_protocol ) ) == -1 ) {
			perror( "server: socket" );
			continue;
		}

		if ( setsockopt( listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof( int ) ) == -1 ) {
			perror( "setsockopt" );
			exit( 1 );
		}

		if ( bind( listener, p->ai_addr, p->ai_addrlen ) == -1 ) {
			close( listener );
			perror( "server: bind" );
			continue;
		}

		break;
	}
	freeaddrinfo( servinfo ); // all done with this structure

	std::vector<pollfd> fds( 1 );
	fds[ 0 ].fd      = listener;
	fds[ 0 ].events  = POLLIN;
	fds[ 0 ].revents = 0;

	if ( p == NULL ) {
		fprintf( stderr, "server: failed to bind\n" );
		exit( 1 );
	}

	if ( listen( listener, BACKLOG ) == -1 ) {
		perror( "listen" );
		exit( 1 );
	}

	self->is_running = true;

	logger.info( "server ready to accept connections" );
	while ( self->should_run ) {
		int poll_count = poll( fds.data(), fds.size(), 60 ); // 60 ms timeout

		if ( poll_count == -1 ) {
			perror( "poll" );
			exit( 1 );
		}

		// Store size of file descriptors here, so that we don't
		// end up checking any file descriptors which are dynamically
		// added.
		size_t fd_count = fds.size();
		for ( size_t i = 0; i != fd_count; ) {

			if ( fds[ i ].revents & POLLIN ) {

				if ( fds[ i ].fd == listener ) {
					// this is the listening socket - we want to open a new connection
					remote_addr_len = sizeof remote_addr;
					int newfd       = accept( listener,
					                          ( struct sockaddr* )&remote_addr,
					                          &remote_addr_len );

					if ( newfd == -1 ) {
						perror( "accept" );
					} else {
						fds.push_back( { .fd = newfd, .events = POLLIN, .revents = 0 } );

						logger.info( "pollserver: new connection from %s on socket %d\n",
						             inet_ntop( remote_addr.ss_family,
						                        get_in_addr( ( struct sockaddr* )&remote_addr ),
						                        remote_ip, INET6_ADDRSTRLEN ),
						             newfd );
					}
				} else {
					// fd != listener, this is a regular client

					int received_bytes_count = recv( fds[ i ].fd, buf, sizeof( buf ), 0 );
					if ( received_bytes_count <= 0 ) {
						// error or connection closed
						if ( received_bytes_count == 0 ) {
							// connection closed
							close( fds[ i ].fd );
							logger.info( "closed connection on file descriptor %d", fds[ i ].fd );
							fds[ i ].fd = -1; // set file descriptor to -1 - this is a signal to remove it from the vector of watched file descriptors
						} else {
							perror( "recv" );
						}
					} else {
						// we received some bytes -- we must process these bytes
						logger.info( ">> %.*s", received_bytes_count, buf );
					}

				} // end: if fd==listener
			}     // end: revents & POLLIN

			if ( fds[ i ].fd == -1 ) {
				// File descriptor has been invalidated
				// We must delete the element at i
				fds.erase( fds.begin() + i );

				// Reduce total number of elements to iterate over
				--fd_count;

				continue; // Note that this *does not increment* i
			}
			i++;
		}
		{
			auto lock = std::unique_lock( self->messages_mtx ); // fixme: this is a mess: what does unique_lock do?
			while ( !self->messages.empty() ) {

				std::string str;
				std::swap( str, self->messages.front() ); // fetch by swapping
				self->messages.pop_front();
				str.append( "\n" ); // we must add a carriage return
				for ( size_t i = 0; i != fd_count; i++ ) {
					// send to all listeners
					if ( fds[ i ].fd != listener )
						// repeat until all bytes sent, or result <= 0
						for ( size_t num_bytes_sent = 0; num_bytes_sent < str.size(); ) {
							ssize_t result = send( fds[ i ].fd, str.data() + num_bytes_sent, str.size() - num_bytes_sent, 0 );
							if ( result <= 0 ) {
								logger.error( "Could not send message, result code: %d", result );
							} else {
								num_bytes_sent += result;
							}
						};
				}
			}
		}
	}

	// TODO: do not close all connections when reloading this module
	// close all open file descriptors
	for ( auto& fd : fds ) {
		close( fd.fd ); // close file descriptor
	}

	logger.info( "leaving server thread" );
}
// ----------------------------------------------------------------------

static bool le_console_server_stop( le_console_o* self ) {

	static auto logger = le::Log( LOG_CHANNEL );
	if ( self->is_running ) {
		self->should_run = false;
		if ( self->server_thread.joinable() ) {
			self->server_thread.join();
			logger.info( "joined server thread" );
		} else {
			logger.error( "could not join server thread" );
			return false;
		}
		logger.info( "stopped server" );
	}
	return true;
}
// ----------------------------------------------------------------------
static void logger_callback( char* chars, uint32_t num_chars, void* user_data ) {
	static std::string msg;

	auto self = ( le_console_o* )user_data;

	auto lock = std::unique_lock( self->messages_mtx );

	while ( self->messages.size() >= 20 ) {
		self->messages.pop_front();
	} // make sure there are at most 19 lines waiting

	self->messages.emplace_back( chars, num_chars );
}

// ----------------------------------------------------------------------

// if we initialize an object from this and store it static,
// the destructor will get called when this module is being unloaded
// this allows us to remove ourselves from listeners before the listener
// gets destroyed.
class RaiiSubscriber : NoCopy {
  public:
	explicit RaiiSubscriber( le_console_o* self_ )
	    : fun( ( le_log_api::fn_subscriber_push_chars )le_core_forward_callback( le_console_api_i->log_callbacks_i.push_chars_callback_addr ) )
	    , handle( le_log::api->add_subscriber(
	          fun,
	          self_,
	          ~uint32_t( 0 ) ) )
	    , self( self_ ){};
	~RaiiSubscriber() {
		// in case that this module gets reloaded, we must remove the subscriber
		// and we must also make sure that there is no server thread running
		// because if there is a server thread running and we change its
		// machine code underneath, it will crash.
		// the server thread must be restarted once we have reloaded this module.
		// we might be able to keep the connection alive.
		le_log::api->remove_subscriber( handle );
		static auto logger = le::Log( LOG_CHANNEL );
		logger.warn( "removed subscriber" );
		le_console_server_stop( self );
	};

  private:
	le_log_api::fn_subscriber_push_chars fun;
	uint64_t                             handle;
	le_console_o*                        self;
};

// ----------------------------------------------------------------------

static auto& getSubscriber( le_console_o* self ) {
	static auto subscriber = RaiiSubscriber( self );
	return subscriber;
}

// ----------------------------------------------------------------------

static bool le_console_server_start( le_console_o* self ) {
	static auto logger = le::Log( LOG_CHANNEL );

	// Note: this adds us as a subscriber to le_log as a side-effect.
	// we do this, so that Raii helps us to unregister ourselves when
	// this module gets destroyed.
	auto& subscriber = getSubscriber( self );

	logger.info( "starting server..." );
	self->should_run = true;
	if ( false == self->is_running ) {
		self->server_thread = std::thread( [ self ]() { server_thread( self ); } );
		logger.info( "started server" );
	}

	return true;
}

// ----------------------------------------------------------------------

static void le_console_destroy( le_console_o* self ) {
	le_console_server_stop( self );

	// we can do this because le_log will never be reloaded as it is part of the core.

	delete self;
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_console, api_ ) {

	auto  api          = static_cast<le_console_api*>( api_ );
	auto& le_console_i = api->le_console_i;

	le_console_i.create       = le_console_create;
	le_console_i.destroy      = le_console_destroy;
	le_console_i.server_start = le_console_server_start;
	le_console_i.server_stop  = le_console_server_stop;

	auto& log_callbacks_i                    = api->log_callbacks_i;
	log_callbacks_i.push_chars_callback_addr = ( void* )logger_callback;
}

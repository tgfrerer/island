#include "le_console.h"
#include "le_core.h"
#include "le_log.h"

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

#include <poll.h>

static constexpr auto LOG_CHANNEL = "le_console";
static constexpr auto PORT        = "3535";
static constexpr auto BACKLOG     = 3;

struct le_console_o {
	// members
	bool        is_running = false;
	bool        should_run = true;
	std::thread server_thread;
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
						logger.info( "%.*s", received_bytes_count, buf );
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
	}

	// close all open file descriptors
	for ( auto& fd : fds ) {
		close( fd.fd ); // close file descriptor
	}

	logger.info( "leaving server thread" );
}

// ----------------------------------------------------------------------

static bool le_console_server_start( le_console_o* self ) {
	static auto logger = le::Log( LOG_CHANNEL );

	logger.info( "starting server..." );
	self->should_run = true;
	if ( false == self->is_running ) {
		self->server_thread = std::thread( [ self ]() { server_thread( self ); } );
		logger.info( "started server" );
	}

	return true;
}

// ----------------------------------------------------------------------

static bool le_console_server_stop( le_console_o* self ) {
	static auto logger = le::Log( LOG_CHANNEL );
	if ( self->is_running ) {
		self->should_run = false;
		if ( self->server_thread.joinable() ) {
			self->server_thread.join();
			logger.info( "detached server thread" );
		} else {
			logger.error( "cold not join server thread" );
			return false;
		}
		logger.info( "stopped server" );
	}
	return true;
}

// ----------------------------------------------------------------------

static void le_console_destroy( le_console_o* self ) {
	le_console_server_stop( self );
	delete self;
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_console, api ) {
	auto& le_console_i = static_cast<le_console_api*>( api )->le_console_i;

	le_console_i.create       = le_console_create;
	le_console_i.destroy      = le_console_destroy;
	le_console_i.server_start = le_console_server_start;
	le_console_i.server_stop  = le_console_server_stop;
}

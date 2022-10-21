#include "le_core.h"
#include "le_log.h"

#include <mutex>
#include <thread>

#include <errno.h>
#include <string.h>
#include <sys/types.h>

#ifndef WIN32
#	include <sys/socket.h>
#	include <netinet/in.h>
#	include <netdb.h>
#	include <arpa/inet.h>
#	include <sys/wait.h>
#	include <poll.h>
#else
#	include <WinSock2.h>
#	include <WS2tcpip.h>
#endif

#include <memory>
#include <vector>
#include <iostream>
#include <fstream>
#include <condition_variable>

#include <deque>

#include "le_console_types.h"
#include "le_console_server.h"

// Translation between winsock and posix sockets
#ifdef WIN32
#	define poll_sockets WSAPoll
#	define close_socket closesocket
#else
#	define poll_sockets poll
#	define close_socket close
#endif

struct le_console_server_o {
	le_console_o* console;
	std::thread   threaded_function;
	bool          thread_should_run = false;
	bool          is_running        = false;

	bool                connection_established = false;
	int                 listener               = 0; // listener socket on sock_fd
	addrinfo*           servinfo               = nullptr;
	std::vector<pollfd> sockets;
};

// ----------------------------------------------------------------------

static auto logger = le::Log( LOG_CHANNEL );

// ----------------------------------------------------------------------

static void                 le_console_server_start( le_console_server_o* self ); // ffd
static void                 le_console_server_stop( le_console_server_o* self );
static void                 le_console_server_start_thread( le_console_server_o* self );
static void                 le_console_server_stop_thread( le_console_server_o* self );
static le_console_server_o* le_console_server_create();
static void                 le_console_server_destroy( le_console_server_o* self );

// ----------------------------------------------------------------------

// get sockaddr, IPv4 or IPv6:
static void* get_in_addr( struct sockaddr* sa ) {
	if ( sa->sa_family == AF_INET ) {
		return &( ( ( struct sockaddr_in* )sa )->sin_addr );
	}
	return &( ( ( struct sockaddr_in6* )sa )->sin6_addr );
}
// ----------------------------------------------------------------------

static void le_console_server_start( le_console_server_o* self ) {
#ifdef WIN32
	WSADATA wsa;
	if ( WSAStartup( MAKEWORD( 2, 2 ), &wsa ) != 0 ) {
		logger.error( "could not start winsock2" );
		exit( 1 );
	}
#endif

	{
		addrinfo hints    = {};
		hints.ai_family   = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags    = AI_PASSIVE; // use my IP
		int rv            = 0;
		if ( ( rv = getaddrinfo( NULL, PORT, &hints, &self->servinfo ) ) != 0 ) {
			logger.error( "getaddrinfo: %s\n", gai_strerror( rv ) );
			return;
		}
	}

	// loop through all the results and bind to the first we can
	for ( addrinfo* p = self->servinfo; p != NULL; p = p->ai_next ) {
		if ( ( self->listener = socket( p->ai_family, p->ai_socktype, p->ai_protocol ) ) == -1 ) {
			perror( "server: socket" );
			continue;
		}

		int yes = 1;
		if ( setsockopt( self->listener, SOL_SOCKET, SO_REUSEADDR, ( char* )&yes, sizeof( int ) ) == -1 ) {
			perror( "setsockopt" );
			exit( 1 );
		}

		if ( bind( self->listener, p->ai_addr, p->ai_addrlen ) == -1 ) {
			close_socket( self->listener );
			perror( "server: bind" );
			continue;
		}

		self->connection_established = true;
		break;
	}
	freeaddrinfo( self->servinfo ); // all done with this structurye

	if ( listen( self->listener, BACKLOG ) == -1 ) {
		perror( "listen" );
		exit( 1 );
	}
}
// ----------------------------------------------------------------------

static void le_console_server_stop( le_console_server_o* self ) {

	// we must ensure that the server thread is not running anymore.
	if ( self->is_running ) {
		le_console_server_stop_thread( self );
	}

	// close all open sockets
	for ( auto& fd : self->sockets ) {
		close_socket( fd.fd ); // close file descriptor
	}
}
// ----------------------------------------------------------------------

static void le_console_server_start_thread( le_console_server_o* self ) {

	if ( self->is_running ) {
		logger.warn( "cannot start server: server is already running." );
		return;
	}

	if ( !self->connection_established ) {
		logger.warn( "Cannot start server thread: Connection not established." );
		return;
	}
	self->thread_should_run = true;

	// ----------------------------------------------------------------------
	// Start threaded function
	self->threaded_function = std::thread( [ server = self ]() {
		server->is_running = true;

		if ( server->sockets.size() < 1 ) {
			server->sockets.resize( 1 );
		}
		server->sockets[ 0 ].fd      = server->listener;
		server->sockets[ 0 ].events  = POLLIN;
		server->sockets[ 0 ].revents = 0;

		sockaddr_storage remote_addr                   = {}; // connector's address information
		socklen_t        remote_addr_len               = 0;
		char             remote_ip[ INET6_ADDRSTRLEN ] = {};
		char             buf[ 1024 ]                   = {}; // Buffer for client data

		if ( server->connection_established == false ) {
			logger.error( "Server: failed to bind\n" );
			server->is_running = false;
			return;
		}

		logger.info( "Server ready to accept connections" );
		while ( server->thread_should_run ) {
			int poll_count = poll_sockets( server->sockets.data(), server->sockets.size(), 60 ); // 60 ms timeout

			if ( poll_count == -1 ) {
				perror( "poll" );
				exit( 1 );
			}

			// Store size of file descriptors here, so that we don't
			// end up checking any file descriptors which are dynamically
			// added.
			size_t fd_count = server->sockets.size();
			for ( size_t i = 0; i != fd_count; ) {

				if ( server->sockets[ i ].revents & POLLIN ) {

					if ( server->sockets[ i ].fd == server->listener ) {
						// this is the listening socket - we want to open a new connection
						remote_addr_len = sizeof remote_addr;
						int newfd       = accept( server->listener,
						                          ( struct sockaddr* )&remote_addr,
						                          &remote_addr_len );

						if ( newfd == -1 ) {
							perror( "accept" );
						} else {
							{
								pollfd tmp_pollfd = {};
								tmp_pollfd.fd     = newfd;
								tmp_pollfd.events = POLLIN;
								server->sockets.emplace_back( tmp_pollfd );
							}
							logger.info( "Pollserver: New connection from %s on socket %d\n",
							             inet_ntop( remote_addr.ss_family,
							                        get_in_addr( ( struct sockaddr* )&remote_addr ),
							                        remote_ip, INET6_ADDRSTRLEN ),
							             newfd );
						}
					} else {
						// fd != listener, this is a regular client

						int received_bytes_count = recv( server->sockets[ i ].fd, buf, sizeof( buf ), 0 );
						if ( received_bytes_count <= 0 ) {
							// error or connection closed
							if ( received_bytes_count == 0 ) {
								// connection closed
								close_socket( server->sockets[ i ].fd );
								logger.info( "Closed connection on file descriptor %d", server->sockets[ i ].fd );
								server->sockets[ i ].fd = -1; // set file descriptor to -1 - this is a signal to remove it from the vector of watched file descriptors
							} else {
								perror( "recv" );
							}
						} else {
							// we received some bytes -- we must process these bytes
							logger.info( ">> %.*s", received_bytes_count, buf );
						}

					} // end: if fd==listener
				}     // end: revents & POLLIN

				if ( server->sockets[ i ].fd == -1 ) {
					// File descriptor has been invalidated
					// We must delete the element at i
					server->sockets.erase( server->sockets.begin() + i );

					// Reduce total number of elements to iterate over
					--fd_count;

					continue; // Note that this *does not increment* i
				}
				i++;
			}
			{
				auto lock = std::unique_lock( server->console->messages_mtx );
				while ( !server->console->messages.empty() ) {

					std::string str;
					std::swap( str, server->console->messages.front() ); // fetch by swapping
					server->console->messages.pop_front();
					str.append( "\n" ); // we must add a carriage return
					for ( size_t i = 0; i != fd_count; i++ ) {
						// send to all listeners
						if ( server->sockets[ i ].fd != server->listener )
							// repeat until all bytes sent, or result <= 0
							for ( size_t num_bytes_sent = 0; num_bytes_sent < str.size(); ) {
								int result = send( server->sockets[ i ].fd, str.data() + num_bytes_sent, str.size() - num_bytes_sent, 0 );
								if ( result <= 0 ) {
									logger.error( "Could not send message, result code: %d", result );
								} else {
									num_bytes_sent += result;
								}
							};
					}
				}
			}
		} // end thread should run

		// if we arrive here, this means that the server thread has completed running.
		server->is_running = false;
	} );
};

// ----------------------------------------------------------------------

static void le_console_server_stop_thread( le_console_server_o* self ) {
	if ( self->threaded_function.joinable() ) {
		self->thread_should_run = false;
		self->threaded_function.join();
	}
}

// ----------------------------------------------------------------------

static le_console_server_o* le_console_server_create( le_console_o* console ) {
	le_console_server_o* self = new le_console_server_o{};
	self->console             = console;
	return self;
}

// ----------------------------------------------------------------------

static void le_console_server_destroy( le_console_server_o* self ) {
	le_console_server_stop( self );
	delete self;
}

// ----------------------------------------------------------------------

void le_console_server_register_api( void* api_ ) {
	auto api = ( le_console_server_api_t* )( api_ );

	api->create       = le_console_server_create;
	api->destroy      = le_console_server_destroy;
	api->start        = le_console_server_start;
	api->stop         = le_console_server_stop;
	api->start_thread = le_console_server_start_thread;
	api->stop_thread  = le_console_server_stop_thread;
}

// ----------------------------------------------------------------------
#include "le_core.h"
#include "le_log.h"

#include <mutex>
#include <thread>

#include <errno.h>
#include <string.h>
#include <sys/types.h>

#if defined( _WIN32 )
#	include <WinSock2.h>
#	include <WS2tcpip.h>
#else
#	include <sys/socket.h>
#	include <netinet/in.h>
#	include <netdb.h>
#	include <arpa/inet.h>
#	include <sys/wait.h>
#	include <poll.h>
#endif

#include <memory>
#include <vector>
#include <iostream>
#include <fstream>
#include <condition_variable>

#include <unordered_map>
#include <deque>

#include "le_console_types.h"
#include "le_console_server.h"

// Translation between winsock and posix sockets
#if defined( _WIN32 )
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
	std::vector<pollfd> poll_requests;
};

// ----------------------------------------------------------------------

static auto logger = le::Log( LOG_CHANNEL );

// ----------------------------------------------------------------------

static void                 le_console_server_start( le_console_server_o* self ); // ffd
static void                 le_console_server_stop( le_console_server_o* self );
static void                 le_console_server_start_thread( le_console_server_o* self );
static void                 le_console_server_stop_thread( le_console_server_o* self );
static le_console_server_o* le_console_server_create( le_console_o* console );
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

#ifdef _WIN32
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
	for ( auto& fd : self->poll_requests ) {
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

		if ( server->poll_requests.size() < 1 ) {
			server->poll_requests.resize( 1 );
		}
		server->poll_requests[ 0 ].fd      = server->listener;
		server->poll_requests[ 0 ].events  = POLLIN;
		server->poll_requests[ 0 ].revents = 0;

		sockaddr_storage remote_addr                   = {}; // connector's address information
		socklen_t        remote_addr_len               = 0;
		char             remote_ip[ INET6_ADDRSTRLEN ] = {};
		char             buf[ 1024 ]                   = {}; // Buffer for client data

		if ( server->connection_established == false ) {
			logger.error( "Server: failed to bind\n" );
			server->is_running = false;
			return;
		}

		// May only run on server thread
		static auto close_connection = []( le_console_server_o* self, std::vector<pollfd>::iterator it ) {
			close_socket( it->fd );
			{
				auto connections_lock = std::scoped_lock( self->console->connections_mutex );
				self->console->connections.erase( it->fd );
			}
			logger.info( "Closed connection on file descriptor %d", it->fd );
			it->fd = -1; // set file descriptor to -1 - this is a signal to remove it from server->poll_requests
		};

		// ----------------------------------------------------------------------
		logger.info( "Server ready to accept connections" );
		// ----------------------------------------------------------------------
		//
		while ( server->thread_should_run ) {

			int poll_count = poll_sockets( server->poll_requests.data(), server->poll_requests.size(), 60 ); // 60 ms timeout

			if ( poll_count == -1 ) {
				perror( "poll" );
				exit( 1 );
			}

			std::vector<pollfd> new_poll_requests;

			for ( auto request = server->poll_requests.begin(); request != server->poll_requests.end(); ) {

				// check if any of our client connections want to close
				if ( request->fd != server->listener ) {

					auto& connection = server->console->connections.at( request->fd );

					if ( connection->wants_close ) {
						close_connection( server, request );
					}
				}

				if ( request->revents & POLLIN ) {

					if ( request->fd == server->listener ) {
						// this is the listening socket - we want to open a new connection
						remote_addr_len = sizeof remote_addr;
						int newfd       = accept( server->listener,
						                          ( struct sockaddr* )&remote_addr,
						                          &remote_addr_len );

						if ( newfd == -1 ) {
							perror( "accept" );
						} else {
							pollfd tmp_pollfd = {};
							tmp_pollfd.fd     = newfd;
							tmp_pollfd.events = POLLIN;
							new_poll_requests.emplace_back( tmp_pollfd );
							{
								// Create a new connection - this means we must protect our map of connections
								auto connections_lock = std::scoped_lock( server->console->connections_mutex );

								server->console->connections[ tmp_pollfd.fd ] = std::make_unique<le_console_o::connection_t>( tmp_pollfd.fd );
							}
							logger.info( "Pollserver: New connection from %s on socket %d\n",
							             inet_ntop( remote_addr.ss_family,
							                        get_in_addr( ( struct sockaddr* )&remote_addr ),
							                        remote_ip, INET6_ADDRSTRLEN ),
							             newfd );
						}
					} else {
						// fd != listener, this is a regular client

						int32_t received_bytes_count = recv( request->fd, buf, sizeof( buf ), 0 );

						if ( received_bytes_count <= 0 ) {
							// error or connection closed
							if ( received_bytes_count == 0 ) {
								close_connection( server, request );
							} else {
								perror( "recv" );
							}
						} else {
							// we received some bytes -- we must process these bytes

							auto  connections_lock = std::scoped_lock( server->console->connections_mutex );
							auto& connection       = server->console->connections[ request->fd ];

							connection->channel_in.post( std::string( buf, received_bytes_count ) );
						}

					} // end: if fd != listener
				}     // end: revents & POLLIN

				if ( request->fd == -1 ) {
					// File descriptor has been invalidated
					// We must delete the current poll request
					request = server->poll_requests.erase( request );
				} else {
					request++;
				}

			} // end for all poll requests

			if ( !new_poll_requests.empty() ) {
				// Add any newly created sockets to the list of sockets that get polled
				server->poll_requests.insert( server->poll_requests.end(), new_poll_requests.begin(), new_poll_requests.end() );
			}

			{
				// Pump out messages

				auto connections_lock = std::scoped_lock( server->console->connections_mutex );

				for ( auto& c : server->console->connections ) {
					auto& connection = c.second;
					if ( c.first == server->listener ) {
						continue;
					}
					std::string str;

					while ( connection->channel_out.fetch( str ) ) {

						if ( c.first != server->listener )
							// repeat until all bytes sent, or result <= 0
							for ( size_t num_bytes_sent = 0; num_bytes_sent < str.size(); ) {
								int result = send( c.first, str.data() + num_bytes_sent, str.size() - num_bytes_sent, 0 );
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
	auto api          = ( le_console_server_api_t* )( api_ );
	api->create       = le_console_server_create;
	api->destroy      = le_console_server_destroy;
	api->start        = le_console_server_start;
	api->stop         = le_console_server_stop;
	api->start_thread = le_console_server_start_thread;
	api->stop_thread  = le_console_server_stop_thread;
}

// ----------------------------------------------------------------------
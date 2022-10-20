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

// ----------------------------------------------------------------------
// get sockaddr, IPv4 or IPv6:
static void* get_in_addr( struct sockaddr* sa ) {
	if ( sa->sa_family == AF_INET ) {
		return &( ( ( struct sockaddr_in* )sa )->sin_addr );
	}

	return &( ( ( struct sockaddr_in6* )sa )->sin6_addr );
}

void Server::startServer() {
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
		if ( ( rv = getaddrinfo( NULL, PORT, &hints, &servinfo ) ) != 0 ) {
			logger.error( "getaddrinfo: %s\n", gai_strerror( rv ) );
			return;
		}
	}

	// loop through all the results and bind to the first we can
	for ( addrinfo* p = servinfo; p != NULL; p = p->ai_next ) {
		if ( ( listener = socket( p->ai_family, p->ai_socktype, p->ai_protocol ) ) == -1 ) {
			perror( "server: socket" );
			continue;
		}

		int yes = 1;
		if ( setsockopt( listener, SOL_SOCKET, SO_REUSEADDR, ( char* )&yes, sizeof( int ) ) == -1 ) {
			perror( "setsockopt" );
			exit( 1 );
		}

		if ( bind( listener, p->ai_addr, p->ai_addrlen ) == -1 ) {
			close_socket( listener );
			perror( "server: bind" );
			continue;
		}

		connection_established = true;
		break;
	}
	freeaddrinfo( servinfo ); // all done with this structurye
	is_running = true;
}

void Server::stopServer() {
	// close all open sockets
	for ( auto& fd : sockets ) {
		close_socket( fd.fd ); // close file descriptor
	}
}

void Server::startThread() {

	if ( !connection_established ) {
		logger.warn( "Cannot start server thread: Connection not established." );
		return;
	}

	// start threaded function
	threaded_function = std::thread( [ this ]() {
		if ( sockets.size() < 1 ) {
			sockets.resize( 1 );
		}
		sockaddr_storage remote_addr                   = {}; // connector's address information
		socklen_t        remote_addr_len               = 0;
		char             remote_ip[ INET6_ADDRSTRLEN ] = {};
		char             buf[ 1024 ]                   = {}; // Buffer for client data

		while ( this->thread_should_run ) {
			sockets[ 0 ].fd      = listener;
			sockets[ 0 ].events  = POLLIN;
			sockets[ 0 ].revents = 0;

			if ( connection_established == false ) {
				fprintf( stderr, "server: failed to bind\n" );
				exit( 1 );
			}

			if ( listen( listener, BACKLOG ) == -1 ) {
				perror( "listen" );
				exit( 1 );
			}

			this->is_running = true;

			logger.info( "server ready to accept connections" );
			while ( this->thread_should_run ) {
				int poll_count = poll_sockets( sockets.data(), sockets.size(), 60 ); // 60 ms timeout

				if ( poll_count == -1 ) {
					perror( "poll" );
					exit( 1 );
				}

				// Store size of file descriptors here, so that we don't
				// end up checking any file descriptors which are dynamically
				// added.
				size_t fd_count = sockets.size();
				for ( size_t i = 0; i != fd_count; ) {

					if ( sockets[ i ].revents & POLLIN ) {

						if ( sockets[ i ].fd == listener ) {
							// this is the listening socket - we want to open a new connection
							remote_addr_len = sizeof remote_addr;
							int newfd       = accept( listener,
							                          ( struct sockaddr* )&remote_addr,
							                          &remote_addr_len );

							if ( newfd == -1 ) {
								perror( "accept" );
							} else {
								{
									pollfd tmp_pollfd = {};
									tmp_pollfd.fd     = newfd;
									tmp_pollfd.events = POLLIN;
									sockets.emplace_back( tmp_pollfd );
								}
								logger.info( "pollserver: new connection from %s on socket %d\n",
								             inet_ntop( remote_addr.ss_family,
								                        get_in_addr( ( struct sockaddr* )&remote_addr ),
								                        remote_ip, INET6_ADDRSTRLEN ),
								             newfd );
							}
						} else {
							// fd != listener, this is a regular client

							int received_bytes_count = recv( sockets[ i ].fd, buf, sizeof( buf ), 0 );
							if ( received_bytes_count <= 0 ) {
								// error or connection closed
								if ( received_bytes_count == 0 ) {
									// connection closed
									close_socket( sockets[ i ].fd );
									logger.info( "closed connection on file descriptor %d", sockets[ i ].fd );
									sockets[ i ].fd = -1; // set file descriptor to -1 - this is a signal to remove it from the vector of watched file descriptors
								} else {
									perror( "recv" );
								}
							} else {
								// we received some bytes -- we must process these bytes
								logger.info( ">> %.*s", received_bytes_count, buf );
							}

						} // end: if fd==listener
					}     // end: revents & POLLIN

					if ( sockets[ i ].fd == -1 ) {
						// File descriptor has been invalidated
						// We must delete the element at i
						sockets.erase( sockets.begin() + i );

						// Reduce total number of elements to iterate over
						--fd_count;

						continue; // Note that this *does not increment* i
					}
					i++;
				}
				{
					auto lock = std::unique_lock( self->messages_mtx );
					while ( !self->messages.empty() ) {

						std::string str;
						std::swap( str, self->messages.front() ); // fetch by swapping
						self->messages.pop_front();
						str.append( "\n" ); // we must add a carriage return
						for ( size_t i = 0; i != fd_count; i++ ) {
							// send to all listeners
							if ( sockets[ i ].fd != listener )
								// repeat until all bytes sent, or result <= 0
								for ( size_t num_bytes_sent = 0; num_bytes_sent < str.size(); ) {
									int result = send( sockets[ i ].fd, str.data() + num_bytes_sent, str.size() - num_bytes_sent, 0 );
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

			// TODO: do not close all connections when reloading this module
		}
	} );
};

void Server::stopThread() {
	if ( threaded_function.joinable() ) {
		thread_should_run = false;
		threaded_function.join();
	}
}

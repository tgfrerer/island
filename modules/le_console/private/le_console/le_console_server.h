#pragma once

class Server : NoCopy {

  public:
	explicit Server( le_console_o* self_ )
	    : self( self_ ){};
	~Server() {
		stopThread(); // blocks until thread has finished
		stopServer();
	};

	void startServer();

	void stopServer();

	void startThread();

	void stopThread();

  private:
	le_console_o* self;
	std::thread   threaded_function;
	bool          thread_should_run = false;
	bool          is_running        = false;

	bool                connection_established = false;
	int                 listener               = 0; // listener socket on sock_fd
	addrinfo*           servinfo               = nullptr;
	std::vector<pollfd> sockets;

	le::Log logger = le::Log( LOG_CHANNEL );
};
#pragma once

static constexpr auto LOG_CHANNEL = "le_console";
static constexpr auto PORT        = "3535";
static constexpr auto BACKLOG     = 3;

struct le_console_o {
	// members
	uint32_t use_count = 0;

	class channel {
		std::mutex              messages_mtx;
		std::deque<std::string> messages;
		uint32_t                max_messages_count = 32; // maximum number of messages to wait on this channel

	  public:
		bool fetch( std::string& msg ) {

			auto lock = std::scoped_lock( messages_mtx );
			if ( messages.empty() ) {
				return false;
			}
			std::swap( msg, messages.front() );
			messages.pop_front();
			return true;
		}

		bool post( std::string const&& msg ) {

			bool enough_space = true;
			auto lock         = std::scoped_lock( messages_mtx );
			while ( messages.size() >= max_messages_count ) {
				messages.pop_front();
				enough_space = false;
			}
			messages.emplace_back( std::move( msg ) );
			return enough_space;
		}
	};

	struct connection_t {
		channel channel_out;
		channel channel_in;

		bool        wants_log_subscriber = false;
		bool        wants_close          = false; // to signal that we want to close this connection
		bool        wants_redraw         = false; // to signal whether the console window must be refreshed
		int         fd;                           // file descriptor for the socket associated with this connection
		std::string remote_ip;

		uint32_t log_level_mask = 0; // -1 means everything 0, means nothing

		explicit connection_t( int fd_ )
		    : fd( fd_ ){};

		~connection_t(); // implementation must live in same compilation unit which provides access to  `le_console_produce_log_subscribers()`
		                 // so that we can remove any subscribers that are owned by this connection if needed

		enum class State {
			ePlain = 0,       // plain socket - this is how we start up
			eSuppressGoahead, // user-requested. telnet line mode
		};

		State state = State::ePlain;

		std::string input_buffer;         // used for linemode
		uint32_t    input_cursor_pos = 0; // position of linemode cursor (one past last element if at end)
		uint16_t    console_width    = 0; // window width of console
		uint16_t    console_height   = 0; // window height of console
	};

	std::mutex                                                           connections_mutex;
	std::unordered_map<int, std::unique_ptr<le_console_o::connection_t>> connections; // socket filedescriptor -> connection

	struct le_console_server_o* server = nullptr;
};
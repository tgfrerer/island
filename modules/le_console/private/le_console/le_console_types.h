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

		bool     wants_log_subscriber = false;
		bool     wants_close          = false; // to signal that we want to close this connection
		uint32_t log_level_mask       = 0;     // -1 means everything 0, means nothing

		enum class State {
			ePlain = 0,      // plain socket - this is how we start up
			eTelnetLineMode, // user-requested. telnet line mode
		};

		State state = State::ePlain;

		std::string input_buffer; // used for linemode

		// In case we have linemode active,
		// we must respect SLC, substitute local characters
		// for which we must keep around a mapping table.
	};

	std::mutex                                                           connections_mutex;
	std::unordered_map<int, std::unique_ptr<le_console_o::connection_t>> connections; // socket filedescriptor -> connection

	struct le_console_server_o* server = nullptr;
};
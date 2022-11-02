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
		bool     wants_log_subscriber = false;
		uint32_t log_level_mask       = ~( uint32_t( 0 ) );
		channel  channel_out;
		channel  channel_in;

		// TODO: add state - we want each connection to have a small state
		// machine embedded, so that we can implement negociations
		// and other aspects of the terminal protocol that require
		// back-and-forth communications.
	};

	std::mutex                                                                connections_mutex;
	std::unordered_map<uint32_t, std::unique_ptr<le_console_o::connection_t>> connections;

	struct le_console_server_o* server = nullptr;
};
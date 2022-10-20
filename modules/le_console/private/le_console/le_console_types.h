#pragma once

static constexpr auto LOG_CHANNEL = "le_console";
static constexpr auto PORT        = "3535";
static constexpr auto BACKLOG     = 3;

struct le_console_o {
	// members
	uint32_t use_count = 0;

	bool wants_log_subscriber = false;
	bool wants_server         = false;
	bool is_server_running    = false;

	std::mutex              messages_mtx;
	std::deque<std::string> messages;
};
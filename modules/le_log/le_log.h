#ifndef GUARD_le_log_H
#define GUARD_le_log_H

#include "le_core/le_core.h"

struct le_log_channel_o;
struct le_log_context_o;

// clang-format off
struct le_log_api {

	enum class Level : uint8_t {
		DEBUG = 0,
		INFO  = 1,
		WARN  = 2,
		ERROR = 3
	};

    le_log_context_o* context = nullptr;

    le_log_channel_o *( * get_channel )(const char *name);

    struct le_log_channel_interface_t {

        void ( *set_level  )(le_log_channel_o *module, Level level);

        void ( *debug      )(const le_log_channel_o *module, const char *msg, ...);
        void ( *info       )(const le_log_channel_o *module, const char *msg, ...);
        void ( *warn       )(const le_log_channel_o *module, const char *msg, ...);
        void ( *error      )(const le_log_channel_o *module, const char *msg, ...);

    };

    le_log_channel_interface_t   le_log_channel_i;
};
// clang-format on

LE_MODULE( le_log );
LE_MODULE_LOAD_DEFAULT( le_log );

#ifdef __cplusplus

namespace le::Log {

using Level = le_log_api::Level;


} // namespace le::Log

#endif // __cplusplus

#endif

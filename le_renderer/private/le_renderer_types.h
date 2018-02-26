#ifndef LE_RENDERER_TYPES_H
#define LE_RENDERER_TYPES_H

#include <stdint.h>

enum class CommandType : uint32_t {
	eDraw = 1,
};

struct CommandHeader {
	union {
		struct {
			CommandType type : 32;  // type of recorded command
			uint64_t    size : 32;	// number of bytes this command occupies in a tightly packed array
		};
		uint64_t u64all;
	} info;
};

struct BufferOffset {
	uint64_t buffer_id : 32;
	uint64_t offset : 32;
};

struct CommandDraw {
	CommandHeader header = {{{CommandType::eDraw, sizeof( CommandDraw )}}};
	struct {
		uint64_t pso_id;
		uint64_t vertex_buffer_bindings;
		uint64_t uniform_buffers_bindings;
	} payload;
};

#endif

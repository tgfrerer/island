#ifndef LE_RENDERER_TYPES_H
#define LE_RENDERER_TYPES_H

#include <stdint.h>

namespace le {

enum class CommandType : uint32_t {
	eDrawIndexed = 1,
};

struct CommandHeader {
	union {
		struct {
			CommandType type : 32; // type of recorded command
			uint64_t    size : 32; // number of bytes this command occupies in a tightly packed array
		};
		uint64_t u64all;
	} info;
};

struct CommandDrawIndexed {
	CommandHeader header = {{{CommandType::eDrawIndexed, sizeof( CommandDrawIndexed )}}};
	struct {
		uint64_t numIndices;
		uint64_t instanceCount;
		uint64_t firstIndex;
		uint64_t vertexOffset;
		uint64_t firstInstance;
	} info;
};

} // namespace le
#endif

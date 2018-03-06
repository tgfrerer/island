#ifndef LE_RENDERER_TYPES_H
#define LE_RENDERER_TYPES_H

#include <stdint.h>

namespace le {

enum class CommandType : uint32_t {
	eDrawIndexed,
	eDraw,
	eSetLineWidth,
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
		uint32_t numIndices;
		uint32_t instanceCount;
		uint32_t firstIndex;
		uint32_t vertexOffset;
		uint32_t firstInstance;
	} info;
};

struct CommandDraw {
	CommandHeader header = {{{CommandType::eDraw, sizeof( CommandDraw )}}};
	struct {
		uint32_t vertexCount;
		uint32_t instanceCount;
		uint32_t firstVertex;
		uint32_t firstInstance;
	} info;
};

struct CommandSetLineWidth{
	CommandHeader header = {{{CommandType::eSetLineWidth, sizeof(CommandSetLineWidth)}}};
	struct {
		float width;
	} info;
};



} // namespace le
#endif

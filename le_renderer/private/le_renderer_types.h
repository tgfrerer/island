#ifndef LE_RENDERER_TYPES_H
#define LE_RENDERER_TYPES_H

#include <stdint.h>

//#ifndef LE_DECLARE_HANDLE
//    #define LE_DECLARE_HANDLE(object) typedef struct object##_o* object;
//#endif

struct LeBufferWriteRegion {
	uint32_t width;
	uint32_t height;
};

namespace le {

enum class CommandType : uint32_t {
	eDrawIndexed,
	eDraw,
	eSetLineWidth,
	eSetViewport,
	eSetScissor,
	eSetArgumentUbo,
	eSetArgumentTexture,
	eBindIndexBuffer,
	eBindVertexBuffers,
	eBindPipeline,
	eWriteToBuffer,
	eWriteToImage,
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
		uint32_t indexCount;
		uint32_t instanceCount;
		uint32_t firstIndex;
		int32_t  vertexOffset;
		uint32_t firstInstance;
		uint32_t reserved; // padding
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

struct CommandSetViewport {
	CommandHeader header = {{{CommandType::eSetViewport, sizeof( CommandSetViewport )}}};
	struct {
		uint32_t firstViewport;
		uint32_t viewportCount;
	} info;
};

struct CommandSetScissor {
	CommandHeader header = {{{CommandType::eSetScissor, sizeof( CommandSetScissor )}}};
	struct {
		uint32_t firstScissor;
		uint32_t scissorCount;
	} info;
};

struct CommandSetArgumentUbo {
	CommandHeader header = {{{CommandType::eSetArgumentUbo, sizeof( CommandSetArgumentUbo )}}};
	struct {
		uint64_t argument_name_id; // const_char_hash id of argument name
		uint64_t buffer_id;        // id of buffer that holds data
		uint32_t offset;           // offset into buffer
		uint32_t range;            // size of argument data in bytes
	} info;
};

struct CommandSetArgumentTexture {
	CommandHeader header = {{{CommandType::eSetArgumentTexture, sizeof( CommandSetArgumentTexture )}}};
	struct {
		uint64_t argument_name_id; // const_char_hash id of argument name
		uint64_t texture_id;       // texture id, hash of texture name
		uint64_t array_index;      // argument array index (default is 0)
	} info;
};

struct CommandSetLineWidth {
	CommandHeader header = {{{CommandType::eSetLineWidth, sizeof( CommandSetLineWidth )}}};
	struct {
		float    width;
		uint32_t reserved; // padding
	} info;
};

struct CommandBindVertexBuffers {
	CommandHeader header = {{{CommandType::eBindVertexBuffers, sizeof( CommandBindVertexBuffers )}}};
	struct {
		uint32_t  firstBinding;
		uint32_t  bindingCount;
		uint64_t *pBuffers; // TODO: place proper buffer_id type here
		uint64_t *pOffsets;
	} info;
};

struct CommandBindIndexBuffer {
	CommandHeader header = {{{CommandType::eBindIndexBuffer, sizeof( CommandBindIndexBuffer )}}};
	struct {
		uint64_t buffer; // buffer id
		uint64_t offset;
		uint64_t indexType;
	} info;
};

struct CommandBindPipeline {
	CommandHeader header = {{{CommandType::eBindPipeline, sizeof( CommandBindPipeline )}}};
	struct {
		struct le_graphics_pipeline_state_o *pso;
		uint64_t                             pipelineHash; // TODO: do we need this?
	} info;
};

struct CommandWriteToBuffer {
	CommandHeader header = {{{CommandType::eWriteToBuffer, sizeof( CommandWriteToBuffer )}}};
	struct {
		uint64_t src_buffer_id; // le buffer id of scratch buffer
		uint64_t dst_buffer_id; // which resource to write to
		uint64_t src_offset;    // offset in scratch buffer where to find source data
		uint64_t dst_offset;    // offset where to write to in target resource
		uint64_t numBytes;      // number of bytes

	} info;
};

struct CommandWriteToImage {
	CommandHeader header = {{{CommandType::eWriteToImage, sizeof( CommandWriteToImage )}}};
	struct {
		uint64_t            src_buffer_id; // le buffer id of scratch buffer
		uint64_t            dst_image_id;  // which resource to write to
		uint64_t            src_offset;    // offset in scratch buffer where to find source data
		uint64_t            numBytes;      // number of bytes
		LeBufferWriteRegion dst_region;    // which part of the image to write to

	} info;
};

} // namespace le
#endif

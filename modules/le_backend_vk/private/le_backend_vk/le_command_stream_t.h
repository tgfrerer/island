#pragma once

#include <cstdlib>
#include <stddef.h>

/*
 * The Command Stream is where the renderer stores the bytecode for
 * our encoded command stream; Some data such as scissor dimensions and
 * push constants will also be encoded into the command stream.
 *
 * The Backend reads from the command stream and decodes it into
 * Vulkan commands.
 *
 * We keep the command stream in a header file that is shared by renderer
 * and backend so that the methods herein can be inlined, as this is all
 * happening on the hot path; we want renderer and backend to each have
 * direct access to the data.
 *
 * A command stream maps 1:1 to a renderpass. As such, there should be no
 * threading contention, as only ever one thread may access a renderpass,
 * and only ever the backend or the renderer access the command stream.
 *
 * Command streams are stored with and owned by the Backend Frame. The
 * Backend Frame creates new Command Streams so that there is one command
 * stream per renderpass. Command Streams are reset when a frame gets cleared.
 *
 * Command streams work as bump, or arena-allocators. This saves us allocating
 * and de-allocating command streams per-frame. At the same time, command
 * streams may grow, if there are a large number of commands to record.
 *
 */

struct le_command_stream_t {
	char*  data      = nullptr;
	size_t size      = 0;
	size_t capacity  = 0;
	size_t cmd_count = 0;

	le_command_stream_t()
	    : size( 0 )
	    , capacity( 8 ) {
		data = ( char* )malloc( capacity );
	}

	~le_command_stream_t() {

		if ( data ) {
			free( data );
			size      = 0;
			cmd_count = 0;
			data      = nullptr;
		}
	}

	void reset() {
		this->cmd_count = 0;
		this->size      = 0;
	}

	template <typename T>
	inline T* emplace_cmd( size_t payload_sz = 0 ) {

		size_t old_sz = this->size;
		size_t new_sz = old_sz + sizeof( T ) + payload_sz;

		while ( new_sz > this->capacity ) {
			this->capacity *= 2;
			this->data = ( char* )realloc( this->data, this->capacity );
		}

		this->size = new_sz;
		this->cmd_count++;
		return new ( this->data + old_sz )( T );
	}
};

// ----------------------------------------------------------------------

#include "le_midi.h"
#include "le_core/le_core.h"
#include "3rdparty/rtmidi/RtMidi.h"
#include <vector>
#include <string.h>

// A midi message blob contains:
// 4B .. length of the blob in bytes
// 4B .. time delta since last message
// nB .. buffer data

struct le_midi_message_blob {
	uint32_t size;          // size of the full message_blob, including any bytes used for buffer_data
	double   time_delta;    // time since last message
	uint8_t  buffer_data[]; // optional payload for message
};

struct le_midi_message_buffer_t {
	std::vector<unsigned char> data = { 0 };
	size_t                     size; // number of used bytes.
};

// ----------------------------------------------------------------------

struct le_midi_o {
	// members
	RtMidiIn *                 midi_in  = nullptr;
	RtMidiOut *                midi_out = nullptr;
	le_midi_message_buffer_t   buffers[ 2 ];
	uint32_t                   front_buffer = 0;
	uint32_t                   back_buffer  = 1;
	std::vector<unsigned char> tmp_data; // used to copy data via getMessage
};

// ----------------------------------------------------------------------

static inline void buffer_reset( le_midi_message_buffer_t *buffer, size_t resize_value = 0 ) {
	// reset buffer - we do this instead of clearing - this is so that we don't have any re-allocations.
	// effectively, we use each buffer as an arena, and the front buffer remains valid until swapped.
	buffer->size = 0;

	if ( resize_value ) {
		buffer->data.resize( resize_value );
	}

	if ( !buffer->data.empty() ) {
		buffer->data[ 0 ] = 0;
	}
}

// ----------------------------------------------------------------------

static le_midi_o *le_midi_create() {
	auto self = new le_midi_o();

	try {
		// Duh, I hate exceptions.
		self->midi_in = new RtMidiIn( RtMidi::Api::UNSPECIFIED, "Island MIDI input client", 400 );
	} catch ( RtMidiError &error ) {
		error.printMessage();
	}

	try {
		// Duh, I hate exceptions.
		self->midi_out = new RtMidiOut( RtMidi::Api::UNSPECIFIED, "Island MIDI input client" );
	} catch ( RtMidiError &error ) {
		error.printMessage();
	}

	self->tmp_data.reserve( 4096 ); // reserve 1 page of data.
	self->back_buffer  = 0;
	self->front_buffer = 1;
	buffer_reset( self->buffers );
	buffer_reset( self->buffers + 1 );
	return self;
}

// ----------------------------------------------------------------------

static bool le_midi_open_midi_out( le_midi_o *self, char const *selected_port_name ) {
	if ( nullptr == self->midi_out ) {
		return false;
	}
	// ---------| invariant: midi_out exists.
	try {
		// we iterate over all midi out ports and open the one which matches
		// our port name.
		uint32_t port_count = self->midi_out->getPortCount();

		int port_name_len = strlen( selected_port_name );

		for ( uint32_t i = 0; i != port_count; i++ ) {
			auto port_name = self->midi_out->getPortName( i );
			int  result    = strncmp( selected_port_name, port_name.c_str(), std::min<int>( port_name_len, port_name.size() ) );
			if ( result == 0 ) {
				// Selected_port_name was found
				self->midi_out->openPort( i );
				return true;
			}
		}
	} catch ( RtMidiError &error ) {
		return false;
	}
	// Not found
	return false;
}

// ----------------------------------------------------------------------

static bool le_midi_open_midi_in( le_midi_o *self, char const *selected_port_name ) {
	if ( nullptr == self->midi_in ) {
		return false;
	}
	// ---------| invariant: midi_out exists.

	try {
		// we iterate over all midi out ports and open the one which matches
		// our port name.

		uint32_t port_count = self->midi_in->getPortCount();

		int port_name_len = strlen( selected_port_name );

		for ( uint32_t i = 0; i != port_count; i++ ) {
			auto port_name = self->midi_in->getPortName( i );
			int  result    = strncmp( selected_port_name, port_name.c_str(), std::min<int>( port_name_len, port_name.size() ) );
			if ( result == 0 ) {
				// Selected_port_name was found
				self->midi_in->openPort( i );
				// Don't ignore sysex, timing, or active sending messages
				self->midi_in->ignoreTypes( true, true, true );
				return true;
			}
		}
	} catch ( RtMidiError &error ) {
		return false;
	}
	// not found

	return false;
}

// ----------------------------------------------------------------------

static void le_midi_destroy( le_midi_o *self ) {
	delete ( self->midi_out );
	delete ( self->midi_in );
	self->midi_in = nullptr;
	delete self;
}

// ----------------------------------------------------------------------

static void le_midi_swap( le_midi_o *self ) {
	// do something with self

	std::swap( self->front_buffer, self->back_buffer );

	// Store messages into our message backbuffer.
	auto &buffer = self->buffers[ self->back_buffer ];

	buffer_reset( &buffer );

	while ( true ) {
		// We call getMessage() using the c++ api - because otherwise we will have to pay for
		// even more unnecessary allocations. By using cpp, there is hope that the storage for
		// our tmp vector may get reused.
		double time_delta = self->midi_in->getMessage( &self->tmp_data );

		if ( self->tmp_data.empty() ) {
			break;
		}

		// ---------| invariant: there is data to process.

		// copy data into backbuffer.
		size_t required_size =
		    buffer.size +                    // current size
		    self->tmp_data.size() +          // payload size
		    sizeof( le_midi_message_blob ) + // message header size
		    1;                               // stop byte size

		if ( buffer.data.size() < required_size ) {
			buffer.data.resize( required_size );
		}

		auto message        = new ( buffer.data.data() + buffer.size )( le_midi_message_blob ); // placement-new
		message->size       = sizeof( le_midi_message_blob ) + self->tmp_data.size();
		message->time_delta = time_delta;
		memcpy( message->buffer_data, self->tmp_data.data(), self->tmp_data.size() );

		buffer.size += message->size;
		buffer.data[ buffer.size ] = 0; // set stop byte just after buffer
	};
}

// ----------------------------------------------------------------------

// This is an iterator method - this is publicly available.
void le_midi_get_messages( le_midi_o *self, le_midi_api::le_midi_iterator_cb callback, void *user_data ) {

	auto &buffer = self->buffers[ self->front_buffer ];
	if ( buffer.size == 0 || buffer.data.size() == 0 ) {
		return;
	}
	// ---------| invariant: buffer.size is not null
	// iterate over all data

	unsigned char *b = buffer.data.data();

	while ( *b != 0 ) {
		auto msg = reinterpret_cast<le_midi_message_blob const *>( b );
		callback( msg->time_delta, msg->buffer_data, msg->size - sizeof( le_midi_message_blob ), user_data );
		b += msg->size;
	}
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_midi, api ) {
	auto &le_midi_i         = static_cast<le_midi_api *>( api )->le_midi_i;
	le_midi_i.create        = le_midi_create;
	le_midi_i.destroy       = le_midi_destroy;
	le_midi_i.swap          = le_midi_swap;
	le_midi_i.get_messages  = le_midi_get_messages;
	le_midi_i.open_midi_in  = le_midi_open_midi_in;
	le_midi_i.open_midi_out = le_midi_open_midi_out;
}

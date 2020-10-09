#ifndef GUARD_le_midi_H
#define GUARD_le_midi_H

#include "le_core/le_core.h"

struct le_midi_o;

/*

We would like a double-buffered midi approach, where you can query the current stream of messages,
and flip on frame change. This means that there will be at most one frame of input lag, but sequencing
will be perfectly clear, and there won't be any surprises.

The idea is that you do the following:

    app->midi_io.open_midi_in( "Midi Fighter Twister" ); // set up midi input

// in update: 
    app->midi_io.swap()
    app->midi_io.get_messages()


We effectively make the API pull instead of push.

Don't forget to call swap() on le_midi once per frame, so that you get access to all
midi messages which have been queued during the last frame.

get_messages() may be called from multiple threads, as it is a read-only operation.

todo: use a ring-buffer for messages, handle message queue overflow. currently, the queue will grow
with new messages until a call to swap() resets the queue, but that is not very elegant.

It's probably best to just throw away any messages which have not been processed.

*/

// clang-format off
struct le_midi_api {

    typedef void ( *le_midi_iterator_cb )( double delta_t, unsigned char const *message, size_t num_message_bytes, void *user_data );

    struct le_midi_interface_t {
		le_midi_o *    ( * create        ) ( );
		void           ( * destroy       ) ( le_midi_o* self );
		void           ( * swap          ) ( le_midi_o* self );
        bool           ( * open_midi_out ) ( le_midi_o *self, char const *selected_port_name );
        bool           ( * open_midi_in  ) ( le_midi_o *self, char const *selected_port_name );

        void           ( * get_messages  ) ( le_midi_o *self, le_midi_iterator_cb callback, void *user_data );
        bool           ( * send_message  ) ( le_midi_o* self, uint8_t const * msg, size_t msg_size );

        void           ( * get_messages_functional  ) ( le_midi_o *self, void * p_std_function);
	};

	le_midi_interface_t       le_midi_i;
};
// clang-format on

LE_MODULE( le_midi );
LE_MODULE_LOAD_DEFAULT( le_midi );

#ifdef __cplusplus

namespace le_midi {
static const auto &api       = le_midi_api_i;
static const auto &le_midi_i = api -> le_midi_i;
} // namespace le_midi

class LeMidi : NoCopy, NoMove {

	le_midi_o *self;

  public:
	LeMidi()
	    : self( le_midi::le_midi_i.create() ) {
	}

	~LeMidi() {
		le_midi::le_midi_i.destroy( self );
	}

	void swap() {
		le_midi::le_midi_i.swap( self );
	}

	void get_messages( le_midi_api::le_midi_iterator_cb callback, void *user_data ) {
		le_midi::le_midi_i.get_messages( self, callback, user_data );
	}

	void get_messages( void *p_std_function ) {
		le_midi::le_midi_i.get_messages_functional( self, p_std_function );
	}

	bool open_midi_in( char const *selected_port_name ) {
		return le_midi::le_midi_i.open_midi_in( self, selected_port_name );
	}

	bool open_midi_out( char const *selected_port_name ) {
		return le_midi::le_midi_i.open_midi_out( self, selected_port_name );
	}

	bool send_message( uint8_t const *message, size_t msg_size ) {
		return le_midi::le_midi_i.send_message( self, message, msg_size );
	}

	operator auto() {
		return self;
	}
};

#endif // __cplusplus

#endif

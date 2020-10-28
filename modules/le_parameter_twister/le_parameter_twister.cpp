#include "le_parameter_twister.h"
#include "le_core/le_core.h"
#include "le_midi/le_midi.h"
#include <iostream>
#include <iomanip>
#include <functional>
#include <vector>
#include <math.h>

#include <string.h> // for memcpy, memset

#include <le_parameter_store/le_parameter_store.h>

constexpr uint8_t MIDI_HIGH_REZ_CONTROLLER = 0x58;

struct le_parameter_twister_o {
	// members
	LeMidi midi_io;

	uint8_t msg_lsb; // The least significant 7 bits capture the value of the least significant bits
	                 // of a high res midi message.
	                 // Most significant bit signals whether it was set or not.

	std::vector<le_parameter_o *> params;
};

// ----------------------------------------------------------------------

struct endian_test {
	union {
		uint8_t a : 4;
		uint8_t b : 4;

		uint8_t c;
	} data = { 0x0F };
};

static_assert( endian_test().data.a == 0xF, "we assume little endian for layout of midi message" );

struct MidiCCMessage {
	uint8_t channel : 4; // lsb \- important: leave channel before command
	uint8_t command : 4; // msb /
	uint8_t controller;
	uint8_t value;
};

// ----------------------------------------------------------------------

static le_parameter_twister_o *le_parameter_twister_create() {
	auto self = new le_parameter_twister_o();

	self->midi_io.open_midi_in( "Midi Fighter Twister" );
	self->midi_io.open_midi_out( "Midi Fighter Twister" );
	self->msg_lsb = 0;

	self->params.resize( 16, nullptr );

	// Todo: reset values on midifighter

	return self;
}

// ----------------------------------------------------------------------

static void le_parameter_twister_destroy( le_parameter_twister_o *self ) {
	delete self;
}

// ----------------------------------------------------------------------

template <typename T>
T map( T t, T min, T max, T min_range, T max_range ) {
	T offset = ( ( t - min ) / double( max - min ) ) * ( max_range - min_range );
	return min_range + offset;
}

template <>
float map<float>( float t, float min, float max, float min_range, float max_range ) {
	float offset = ( ( t - min ) / double( max - min ) ) * ( max_range - min_range );
	return min_range + offset;
}

// ----------------------------------------------------------------------
template <>
uint32_t map<uint32_t>( uint32_t t, uint32_t min, uint32_t max, uint32_t min_range, uint32_t max_range ) {
	uint32_t offset = std::round( ( ( t - min ) / double( max - min ) ) * ( max_range - min_range ) );
	return min_range + offset;
}

template <>
int32_t map<int32_t>( int32_t t, int32_t min, int32_t max, int32_t min_range, int32_t max_range ) {
	int32_t offset = std::round( ( ( t - min ) / double( max - min ) ) * ( max_range - min_range ) );
	return min_range + offset;
}

// ----------------------------------------------------------------------

static void le_parameter_twister_update( le_parameter_twister_o *self ) {

	// Process midi data which accumulated since we last swapped.
	self->midi_io.swap();

	// Todo: actually do something with the data.

	std::function fp =
	    [ & ]( double, unsigned char const *msg, size_t msg_size ) {
		    // for ( size_t i = 0; i != msg_size; i++ ) {
		    //     std::cout << std::hex << int( msg[ i ] );
		    // }
		    //    std::cout << "\n";

		    if ( msg_size == sizeof( MidiCCMessage ) ) {
			    auto m = reinterpret_cast<MidiCCMessage const *>( msg );

			    // std::cout << "controller: " << int( m->controller ) << std::endl;
			    // std::cout << "channel: " << int( m->channel ) << std::endl;
			    // std::cout << "command: " << int( m->command ) << std::endl;

			    if ( m->channel != 0 || m->command != 0xB ) {
				    return;
			    }

			    if ( m->controller == MIDI_HIGH_REZ_CONTROLLER /* Midi high res controller */ ) {
				    self->msg_lsb = ( 0x7Fu & m->value ) | 0x80u;
			    } else if ( m->controller < 16 ) {
				    uint16_t val = 0;
				    if ( 0x80u & self->msg_lsb ) {
					    val = ( 0x7Fu & m->value ) << 7;
					    // If less significant byte was prepended,
					    // we must place the 7 least significant bits
					    // as the least significant bits of our 14 bit value.
					    val |= ( 0x7Fu & self->msg_lsb );
					    self->msg_lsb = 0;
				    } else {
					    // Otherwise we have a low-res encoder. We still want to
					    // map it to 0..0x3ff, which means we must make sure that
					    // the 128 intervals (0..127) are spread evenly to (0..0x3FFF)
					    val = ( 0x7f & m->value ) << 7 | ( 0x7f & m->value );
				    }
				    // 				    std::cout << "val: " << std::dec << val << "\n"
				    // 				              << std::flush;
				    auto pParam = self->params[ m->controller ];

				    if ( pParam ) {

					    LeParameter param( pParam );

					    using ParamType = le_parameter_store_api::Type;

					    switch ( param.getType() ) {
					    case ParamType::eBool: {
						    bool *p = param.asBool();
						    // TODO: implement BOOL: this should use different type of switch.
						    break;
					    }
					    case ParamType::eFloat: {
						    float *p = param.asFloat();
						    p[ 0 ]   = map<float>( val, 0, 0x3FFF, p[ 1 ], p[ 2 ] );
						    //						    std::cout << std::dec << p[ 0 ] << std::endl;
						    break;
					    }
					    case ParamType::eU32: {
						    uint32_t *p = param.asU32();
						    p[ 0 ]      = map<uint32_t>( val, 0, 0x3FFF, p[ 1 ], p[ 2 ] );
						    //						    std::cout << std::dec << p[ 0 ] << std::endl;
						    break;
					    }
					    case ParamType::eI32: {
						    int32_t *p = param.asI32();
						    p[ 0 ]     = map<int32_t>( val, 0, 0x3FFF, p[ 1 ], p[ 2 ] );
						    //						    std::cout << std::dec << p[ 0 ] << std::endl;
						    break;
					    }
					    default:
						    break;
					    }
				    }
			    }
		    }
	    };

	self->midi_io.get_messages( &fp );
}

// ----------------------------------------------------------------------

static void send_to_midi( LeMidi &midi_io, uint16_t val, uint8_t controller_id ) {
	MidiCCMessage msg;
	msg.channel    = 0x0;
	msg.command    = 0xb;
	msg.controller = MIDI_HIGH_REZ_CONTROLLER;
	msg.value      = val & 0x7F; // lower 7 bits

	midi_io.send_message( reinterpret_cast<uint8_t const *>( &msg ), sizeof( MidiCCMessage ) );

	msg.channel    = 0x0;
	msg.command    = 0xb;
	msg.controller = controller_id;
	msg.value      = ( val >> 7 ) & 0x7F;

	midi_io.send_message( reinterpret_cast<uint8_t const *>( &msg ), sizeof( MidiCCMessage ) );
}

// ----------------------------------------------------------------------

static void set_midi_switch_behaviour( LeMidi &midi_io, uint8_t encoder_id, uint8_t behaviour ) {
	MidiCCMessage msg;

	msg.channel    = 6;   // encoder behaviour control channel
	msg.command    = 0xB; // cc command
	msg.controller = encoder_id;
	msg.value      = behaviour; // request fine control

	midi_io.send_message( reinterpret_cast<uint8_t const *>( &msg ), sizeof( MidiCCMessage ) );
}

// ----------------------------------------------------------------------

static void le_parameter_twister_add_parameter( le_parameter_twister_o *self, le_parameter_o *param, uint8_t encoder_id ) {

	if ( encoder_id >= self->params.size() ) {
		std::cout << "encoder too large: " << std::dec << encoder_id;
		return;
	}
	enum EncoderSwitchBehaviour {
		CC_HOLD = 0,
		CC_TOGGLE,
		NOTE_HOLD,
		NOTE_TOGGLE,
		ENC_RESET_VALUE,
		ENC_FINE_ADJUST,
		ENC_SHIFT_HOLD,
		ENC_SHIFT_TOGGLE,
	};

	// TODO:
	// -----
	// * send value of parameter to encoder.
	// * set encoder to be of type requested by parameter type

	// --------| invariant: encoder_id < self.params.size()

	LeParameter p( param );

	using Type = le_parameter_store_api::Type;

	switch ( p.getType() ) {
	case Type::eBool: {
		set_midi_switch_behaviour( self->midi_io, encoder_id, CC_TOGGLE );
		break;
	}
	case Type::eFloat: {
		set_midi_switch_behaviour( self->midi_io, encoder_id, ENC_FINE_ADJUST );
		// map value to 0..0x3FFF
		float *  p_val          = p.asFloat();
		uint16_t controller_val = std::round( map<float>( p_val[ 0 ], p_val[ 1 ], p_val[ 2 ], 0, 0x3FFF ) );
		send_to_midi( self->midi_io, controller_val, encoder_id );
		break;
	}
	case Type::eI32: {
		set_midi_switch_behaviour( self->midi_io, encoder_id, ENC_FINE_ADJUST );
		// map value to 0..0x3FFF
		int32_t *p_val          = p.asI32();
		uint16_t controller_val = std::round( map<int32_t>( p_val[ 0 ], p_val[ 1 ], p_val[ 2 ], 0, 0x3FFF ) );
		send_to_midi( self->midi_io, controller_val, encoder_id );
		break;
	}
	case Type::eU32: {
		set_midi_switch_behaviour( self->midi_io, encoder_id, ENC_FINE_ADJUST );
		// map value to 0..0x3FFF
		uint32_t *p_val          = p.asU32();
		uint16_t  controller_val = std::round( map<uint32_t>( p_val[ 0 ], p_val[ 1 ], p_val[ 2 ], 0, 0x3FFF ) );
		send_to_midi( self->midi_io, controller_val, encoder_id );
		break;
	}
	case Type::eUnknown: {
		break;
	}
	}

	self->params[ encoder_id ] = param;
}

// ----------------------------------------------------------------------

LE_MODULE_REGISTER_IMPL( le_parameter_twister, api ) {
	auto &le_parameter_twister_i = static_cast<le_parameter_twister_api *>( api )->le_parameter_twister_i;

	le_parameter_twister_i.create        = le_parameter_twister_create;
	le_parameter_twister_i.destroy       = le_parameter_twister_destroy;
	le_parameter_twister_i.update        = le_parameter_twister_update;
	le_parameter_twister_i.add_parameter = le_parameter_twister_add_parameter;
}

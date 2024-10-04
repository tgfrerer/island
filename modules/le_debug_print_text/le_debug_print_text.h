#ifndef GUARD_le_debug_print_text_H
#define GUARD_le_debug_print_text_H

#include "le_core.h"

#include "3rdparty/src/glm/glm/fwd.hpp"

struct le_debug_print_text_o;
struct le_renderpass_o;

// A stateful printer for debug messages
//
// The simplest way to print to screen is to use
// the Global Printer Singleton - it is available to any file that includes this header.
//
// You can print like this: le::DrawDebugPrint::printf("I'm printing %04d", 1);
//
// To see the messages rendered on top of a renderpass, you must do this:
//
// le::DrawDebugPint::drawAllMessages(main_renderpass);
//
// drawing the messages into a renderpass clears the message state and resets the debug message
// printer.
//
//
// You can place the cursor
// Cursor moves with text that has been printed.
//
// You can set color -- style information is on a stack
// you can push / pop from that stack
//
// on draw, all the text that has accumulated through
// the frame gets printed in one go.
// and the accumulated print instructions reset.

//
// this should be as simple as possible to use.
// we might want to use a similar syntax at some point as for
// log -- so that we can use printf to generate messages ... but that's for later

// TODO: add a flag that tells us whether the screen should auto-clear

// clang-format off
struct le_debug_print_text_api {

struct vec2_t; // alias this to glm::vec2 if available

	struct le_debug_print_text_interface_t {

		le_debug_print_text_o * ( * create       ) ( );
		void                    ( * destroy      ) ( le_debug_print_text_o* self );

		void 					( * print        ) ( le_debug_print_text_o* self, char const * text, glm::vec2* optional_cursor );
		void                    ( * printf       ) ( le_debug_print_text_o* self, glm::vec2* optional_cursor, const char *msg, ... );

		bool 					( * has_messages ) ( le_debug_print_text_o* self );
		void 					( * get_cursor   ) ( le_debug_print_text_o* self, glm::vec2* cursor );
		void                    ( * set_scale    ) ( le_debug_print_text_o* self, float scale );

		void                    ( * draw         ) ( le_debug_print_text_o* self, le_renderpass_o* rp );
	};

	le_debug_print_text_o * singleton_obj = nullptr;

	le_debug_print_text_interface_t       le_debug_print_text_i;
};
// clang-format on

LE_MODULE( le_debug_print_text );
LE_MODULE_LOAD_DEFAULT( le_debug_print_text );

#ifdef __cplusplus

namespace le_debug_print_text {
static const auto& api                   = le_debug_print_text_api_i;
static const auto& le_debug_print_text_i = api->le_debug_print_text_i;
} // namespace le_debug_print_text

// use this interface if you want to create an app-owned text printer
// consider using the global interface further below if you want the
// most simple way of interacting with the debug text printer.
class LeDebugTextPrinter : NoCopy, NoMove {

	le_debug_print_text_o* self;

  public:
	LeDebugTextPrinter()
	    : self( le_debug_print_text::le_debug_print_text_i.create() ) {
	}

	~LeDebugTextPrinter() {
		le_debug_print_text::le_debug_print_text_i.destroy( self );
	}

	// Returns whether there are any messages to display
	bool hasMessages() {
		return le_debug_print_text::le_debug_print_text_i.has_messages( self );
	}

	void draw( le_renderpass_o* rp ) {
		le_debug_print_text::le_debug_print_text_i.draw( self, rp );
	}

	void setScale( float scale ) {
		le_debug_print_text::le_debug_print_text_i.set_scale( self, scale );
	}

	// get current state of the cursor position
	void getCursor( glm::vec2* cursor ) {
		le_debug_print_text::le_debug_print_text_i.get_cursor( self, cursor );
	}

	// print given text at the position given at optional_cursor
	void print( char const* text, glm::vec2* optional_cursor = nullptr ) {
		le_debug_print_text::le_debug_print_text_i.print( self, text, optional_cursor );
	}

	template <class... Args>
	void printf( glm::vec2* cursor = nullptr, const char* msg = nullptr, Args&&... args ) {
		le_debug_print_text::le_debug_print_text_i.printf( self, cursor, msg, static_cast<Args&&>( args )... );
	}

	operator auto () {
		return self;
	}
};

namespace le {
using DebugTextPrinter = LeDebugTextPrinter;
}

// ----------------------------------------------------------------------
// Global Singleton interface
//
// -- Prefer this interface
//

namespace le {

namespace DebugPrint {

// returns whether there are any messages to display
inline static bool hasMessages() {
	return le_debug_print_text::le_debug_print_text_i.has_messages(
	    le_debug_print_text_api_i->singleton_obj );
}

inline static void drawAllMessages( le_renderpass_o* rp ) {
	le_debug_print_text::le_debug_print_text_i.draw(
	    le_debug_print_text_api_i->singleton_obj, rp );
}

inline static void getCursor( glm::vec2* cursor ) {
	le_debug_print_text::le_debug_print_text_i.get_cursor(
	    le_debug_print_text_api_i->singleton_obj, cursor );
}

inline static void setScale( float scale ) {
	le_debug_print_text::le_debug_print_text_i.set_scale(
	    le_debug_print_text_api_i->singleton_obj, scale );
}

inline static void print( char const* text, glm::vec2* optional_cursor = nullptr ) {
	le_debug_print_text::le_debug_print_text_i.print(
	    le_debug_print_text_api_i->singleton_obj, text, optional_cursor );
}

template <class... Args>
inline static void printf( glm::vec2* cursor = nullptr, const char* msg = nullptr, Args&&... args ) {
	le_debug_print_text::le_debug_print_text_i.printf(
	    le_debug_print_text_api_i->singleton_obj, cursor, msg, static_cast<Args&&>( args )... );
}

} // namespace DebugPrint
} // namespace le

#endif // __cplusplus

#endif

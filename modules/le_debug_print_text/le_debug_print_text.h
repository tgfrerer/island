#ifndef GUARD_le_debug_print_text_H
#define GUARD_le_debug_print_text_H

#include "le_core.h"

struct le_debug_print_text_o;
struct le_renderpass_o;

/*

----------------------------------------------------------------------
             A stateful debug printer for debug messages
----------------------------------------------------------------------

The simplest way to print to screen is to use the Global Printer
Singleton -- it is available to any file that includes this header.

You can print like this:

    le::DebugPrint::printf("I'm printing %04d", 1);

To see the messages rendered on top of a renderpass, you must do this:

    le::DebugPrint::drawAllMessages(main_renderpass);

Drawing the messages into a renderpass clears the message state and
resets the debug message printer.

Note that there is no implicit synchronisation - this printer is
unaware that other threads might be using it.

Cursor moves with text that has been printed.

You can set color style information; style info is on a stack to which
you can push / pop. Yes, this is stateful. It is also concise and
relatively simple (assuming a single-threaded environment) we might
want to reconsider this architecture if we get into trouble with
threading.

On draw, all the text that has accumulated through the frame gets
printed in one go. and the accumulated print instructions reset.

----------------------------------------------------------------------

*/

// clang-format off

struct le_debug_print_text_api {

	struct float2{
		float x;
		float y;
	};

	struct float_colour_t{
		union{ float r; float x; };
		union{ float g; float y; };
		union{ float b; float z; };
		union{ float a; float w; };
	};

	struct le_debug_print_text_interface_t {

		le_debug_print_text_o * ( * create       ) ( );
		void                    ( * destroy      ) ( le_debug_print_text_o* self );

		void 					( * print        ) ( le_debug_print_text_o* self, char const * text);
		void                    ( * printf       ) ( le_debug_print_text_o* self, const char *msg, ... );

		bool 					( * has_messages ) ( le_debug_print_text_o* self );
		bool 					( * needs_draw   ) ( le_debug_print_text_o* self );

		void                    ( * set_colour   ) ( le_debug_print_text_o* self, const float_colour_t* colour);
		void                    ( * set_bg_colour   ) ( le_debug_print_text_o* self, const float_colour_t* colour);

		float2                  ( * get_cursor    ) ( le_debug_print_text_o* self );
		void                    ( * set_cursor    ) ( le_debug_print_text_o* self, const float2* cursor );

		float                   ( * get_scale    ) ( le_debug_print_text_o* self );
		void                    ( * set_scale    ) ( le_debug_print_text_o* self, float scale );

		void                    ( * push_style   ) ( le_debug_print_text_o* self );
		void                    ( * pop_style    ) ( le_debug_print_text_o* self );

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
	using float2         = le_debug_print_text_api::float2;
	using float_colour_t = le_debug_print_text_api::float_colour_t;

	LeDebugTextPrinter()
	    : self( le_debug_print_text::le_debug_print_text_i.create() ) {
	}

	~LeDebugTextPrinter() {
		le_debug_print_text::le_debug_print_text_i.destroy( self );
	}

	/// returns whether there are any messages to display
	bool needsDraw() {
		return le_debug_print_text::le_debug_print_text_i.needs_draw( self );
	}

	/// returns whether there are any messages to display
	bool hasMessages() {
		return le_debug_print_text::le_debug_print_text_i.has_messages( self );
	}

	/// draw the content of the current printer frame into the given renderpass
	/// we assume that the renderpass is a graphics pass.
	void draw( le_renderpass_o* rp ) {
		le_debug_print_text::le_debug_print_text_i.draw( self, rp );
	}

	void setCursor( float2& cursor ) {
		le_debug_print_text::le_debug_print_text_i.set_cursor( self, &cursor );
	}

	/// returns current cursor position
	float2 getCursor() {
		return le_debug_print_text::le_debug_print_text_i.get_cursor( self );
	}

	/// set foreground colour
	void setColour( float_colour_t colour ) {
		le_debug_print_text::le_debug_print_text_i.set_colour( self, &colour );
	}

	/// set background colour
	void setBgColour( float_colour_t colour ) {
		le_debug_print_text::le_debug_print_text_i.set_bg_colour( self, &colour );
	}

	/// set text scale. 1.0 is default 1:1 pixel scale
	void setScale( float scale ) {
		le_debug_print_text::le_debug_print_text_i.set_scale( self, scale );
	}

	/// get current text scale. 1.0 is default pixel scale.
	float getScale() {
		return le_debug_print_text::le_debug_print_text_i.get_scale( self );
	}

	/// push all style state onto the printer's stack
	void pushStyle() {
		le_debug_print_text::le_debug_print_text_i.push_style( self );
	}

	/// pop style state and restore state to previously pushed style (if any)
	void popStyle() {
		le_debug_print_text::le_debug_print_text_i.pop_style( self );
	}

	/// print text without using any printf-style formatting.
	///
	/// note that this doesn't immediately print the text to screen, but that this
	/// enqueues a text drawing operation in the printer object. To draw all
	/// accumulated text, use the `draw` method.
	void print( char const* text ) {
		le_debug_print_text::le_debug_print_text_i.print( self, text );
	}

	/// print text with printf-style formatting.
	///
	/// note that this doesn't immediately print the text to screen, but that this
	/// enqueues a text drawing operation in the printer object. To draw all
	/// accumulated text, use the `draw` method.
	template <class... Args>
	void printf( const char* msg = nullptr, Args&&... args ) {
		le_debug_print_text::le_debug_print_text_i.printf( self, msg, static_cast<Args&&>( args )... );
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

using float2         = le_debug_print_text_api::float2;
using float_colour_t = le_debug_print_text_api::float_colour_t;

/// returns whether there is any text to display since last draw
inline static bool needsDraw() {
	return le_debug_print_text::le_debug_print_text_i.needs_draw(
	    le_debug_print_text_api_i->singleton_obj );
}

/// returns whether there are any messages to display
inline static bool hasMessages() {
	return le_debug_print_text::le_debug_print_text_i.has_messages(
	    le_debug_print_text_api_i->singleton_obj );
}

inline static void drawAllMessages( le_renderpass_o* rp ) {
	le_debug_print_text::le_debug_print_text_i.draw(
	    le_debug_print_text_api_i->singleton_obj, rp );
}

inline static void setColour( float_colour_t colour ) {
	le_debug_print_text::le_debug_print_text_i.set_colour(
	    le_debug_print_text_api_i->singleton_obj, &colour );
}

inline static void setBgColour( float_colour_t const& colour ) {
	le_debug_print_text::le_debug_print_text_i.set_bg_colour(
	    le_debug_print_text_api_i->singleton_obj, &colour );
}

inline static void setScale( float scale ) {
	le_debug_print_text::le_debug_print_text_i.set_scale(
	    le_debug_print_text_api_i->singleton_obj, scale );
}

inline static float getScale() {
	return le_debug_print_text::le_debug_print_text_i.get_scale(
	    le_debug_print_text_api_i->singleton_obj );
}

inline static float2 getCursor() {
	return le_debug_print_text::le_debug_print_text_i.get_cursor(
	    le_debug_print_text_api_i->singleton_obj );
}

inline static void setCursor( float2 const& cursor ) {
	le_debug_print_text::le_debug_print_text_i.set_cursor(
	    le_debug_print_text_api_i->singleton_obj, &cursor );
}

inline static void push_style() {
	le_debug_print_text::le_debug_print_text_i.push_style(
	    le_debug_print_text_api_i->singleton_obj );
}

inline static void pop_style() {
	le_debug_print_text::le_debug_print_text_i.pop_style(
	    le_debug_print_text_api_i->singleton_obj );
}

inline static void print( char const* text ) {
	le_debug_print_text::le_debug_print_text_i.print(
	    le_debug_print_text_api_i->singleton_obj, text );
}

template <class... Args>
inline static void printf( const char* msg = nullptr, Args&&... args ) {
	le_debug_print_text::le_debug_print_text_i.printf(
	    le_debug_print_text_api_i->singleton_obj, msg, static_cast<Args&&>( args )... );
}

} // namespace DebugPrint
} // namespace le

#endif // __cplusplus

#endif

#pragma once
#include <stdint.h>

struct LeUiEvent {
	enum class NamedGamepadButton : uint32_t {
		eA           = 0,
		eB           = 1,
		eX           = 2,
		eY           = 3,
		eLeftBumper  = 4,
		eRightBumper = 5,
		eBack        = 6,
		eStart       = 7,
		eGuide       = 8,
		eLeftThumb   = 9,
		eRightThumb  = 10,
		eDpadUp      = 11,
		eDpadRight   = 12,
		eDpadDown    = 13,
		eDpadLeft    = 14,
		//
		eLast = eDpadLeft,
		//
		eCross    = eA,
		eCircle   = eB,
		eSquare   = eX,
		eTriangle = eY,
	};

	enum class NamedGamepadAxis : uint32_t {
		eLeftX        = 0,
		eLeftY        = 1,
		eRightX       = 2,
		eRightY       = 3,
		eLeftTrigger  = 4,
		eRightTrigger = 5,
		eLast         = eRightTrigger,
	};

	enum class NamedKey : int32_t {
		eUnknown = -1,
		/* Printable keys */
		eSpace        = 32,
		eApostrophe   = 39, /* ' */
		eComma        = 44, /* , */
		eMinus        = 45, /* - */
		ePeriod       = 46, /* . */
		eSlash        = 47, /* / */
		e0            = 48,
		e1            = 49,
		e2            = 50,
		e3            = 51,
		e4            = 52,
		e5            = 53,
		e6            = 54,
		e7            = 55,
		e8            = 56,
		e9            = 57,
		eSemicolon    = 59, /* ; */
		eEqual        = 61, /* = */
		eA            = 65,
		eB            = 66,
		eC            = 67,
		eD            = 68,
		eE            = 69,
		eF            = 70,
		eG            = 71,
		eH            = 72,
		eI            = 73,
		eJ            = 74,
		eK            = 75,
		eL            = 76,
		eM            = 77,
		eN            = 78,
		eO            = 79,
		eP            = 80,
		eQ            = 81,
		eR            = 82,
		eS            = 83,
		eT            = 84,
		eU            = 85,
		eV            = 86,
		eW            = 87,
		eX            = 88,
		eY            = 89,
		eZ            = 90,
		eLeftBracket  = 91,  /* [ */
		eBackslash    = 92,  /* \ */
		eRightBracket = 93,  /* ] */
		eGraveAccent  = 96,  /* ` */
		eWorld1       = 161, /* non-US #1 */
		eWorld2       = 162, /* non-US #2 */
		/* Function keys */
		eEscape       = 256,
		eEnter        = 257,
		eTab          = 258,
		eBackspace    = 259,
		eInsert       = 260,
		eDelete       = 261,
		eRight        = 262,
		eLeft         = 263,
		eDown         = 264,
		eUp           = 265,
		ePageUp       = 266,
		ePageDown     = 267,
		eHome         = 268,
		eEnd          = 269,
		eCapsLock     = 280,
		eScrollLock   = 281,
		eNumLock      = 282,
		ePrintScreen  = 283,
		ePause        = 284,
		eF1           = 290,
		eF2           = 291,
		eF3           = 292,
		eF4           = 293,
		eF5           = 294,
		eF6           = 295,
		eF7           = 296,
		eF8           = 297,
		eF9           = 298,
		eF10          = 299,
		eF11          = 300,
		eF12          = 301,
		eF13          = 302,
		eF14          = 303,
		eF15          = 304,
		eF16          = 305,
		eF17          = 306,
		eF18          = 307,
		eF19          = 308,
		eF20          = 309,
		eF21          = 310,
		eF22          = 311,
		eF23          = 312,
		eF24          = 313,
		eF25          = 314,
		eKp0          = 320,
		eKp1          = 321,
		eKp2          = 322,
		eKp3          = 323,
		eKp4          = 324,
		eKp5          = 325,
		eKp6          = 326,
		eKp7          = 327,
		eKp8          = 328,
		eKp9          = 329,
		eKpDecimal    = 330,
		eKpDivide     = 331,
		eKpMultiply   = 332,
		eKpSubtract   = 333,
		eKpAdd        = 334,
		eKpEnter      = 335,
		eKpEqual      = 336,
		eLeftShift    = 340,
		eLeftControl  = 341,
		eLeftAlt      = 342,
		eLeftSuper    = 343,
		eRightShift   = 344,
		eRightControl = 345,
		eRightAlt     = 346,
		eRightSuper   = 347,
		eMenu         = 348,
	};

	enum class ButtonAction : int32_t {
		eRelease = 0,
		ePress,
		eRepeat,
	};

	enum class Type : uint32_t {
		eUnknown = 0,
		eKey,
		eCharacter,
		eCursorPosition,
		eCursorEnter,
		eMouseButton,
		eScroll,
		eDrop,
		eGamepad,
	};

	struct KeyEvent {
		NamedKey     key;
		int32_t      scancode;
		ButtonAction action;
		int32_t      mods;
	};

	struct CharacterEvent {
		uint32_t codepoint;
	};

	struct CursorPositionEvent {
		double x;
		double y;
	};

	struct CursorEnterEvent {
		uint32_t entered;
	};

	struct MouseButtonEvent {
		int32_t      button;
		ButtonAction action;
		int32_t      mods;
	};

	struct ScrollEvent {
		double x_offset;
		double y_offset;
	};

	struct DropEvent {
		char const** paths_utf8;
		uint64_t     paths_count;
	};

	struct GamepadEvent {
		float    axes[ 6 ];  // -1 to 1 (inclusive for each axis)
		uint16_t buttons;    // [0] : 0..14 bitset, 0 is least significant bit
		uint16_t gamepad_id; // 0..15

		bool get_button_at( NamedGamepadButton const& b ) const noexcept {
			return uint8_t( b ) < 15 ? ( buttons & ( uint16_t( 1 ) << uint8_t( b ) ) ) : false;
		}

		static bool get_button_at( uint16_t const& explicit_button_state, NamedGamepadButton const& b ) noexcept {
			return uint8_t( b ) < 15 ? ( explicit_button_state & ( uint16_t( 1 ) << uint8_t( b ) ) ) : false;
		}

		bool operator==( GamepadEvent const& rhs ) {
			return axes[ 0 ] == rhs.axes[ 0 ] &&
			       axes[ 1 ] == rhs.axes[ 1 ] &&
			       axes[ 2 ] == rhs.axes[ 2 ] &&
			       axes[ 3 ] == rhs.axes[ 3 ] &&
			       axes[ 4 ] == rhs.axes[ 4 ] &&
			       axes[ 5 ] == rhs.axes[ 5 ] &&
			       buttons == rhs.buttons;
		}

		bool operator!=( GamepadEvent const& rhs ) {
			return !( *this == rhs );
		}
	};

	union {
		KeyEvent            key;
		CharacterEvent      character;
		CursorPositionEvent cursorPosition;
		CursorEnterEvent    cursorEnter;
		MouseButtonEvent    mouseButton;
		ScrollEvent         scroll;
		DropEvent           drop;
		GamepadEvent        gamepad;
	};

	Type event;
};

namespace le {
using UiEvent = LeUiEvent;
}

#pragma once
#include <stdint.h>

// Todo: move this to a frame-work wide internal header file.

struct LeUiEvent {

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
		eKey            = 1 << 0,
		eCharacter      = 1 << 1,
		eCursorPosition = 1 << 2,
		eCursorEnter    = 1 << 3,
		eMouseButton    = 1 << 4,
		eScroll         = 1 << 5,
		eDrop           = 1 << 6,
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

	Type event;

	union {
		KeyEvent            key;
		CharacterEvent      character;
		CursorPositionEvent cursorPosition;
		CursorEnterEvent    cursorEnter;
		MouseButtonEvent    mouseButton;
		ScrollEvent         scroll;
		DropEvent           drop;
	};
};

namespace le {
using UiEvent = LeUiEvent;
}

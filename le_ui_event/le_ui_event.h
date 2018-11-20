#ifndef GUARD_le_ui_event_H
#define GUARD_le_ui_event_H

#include <stdint.h>

// Todo: move this to a frame-work wide internal header file.

struct LeUiEvent {

	enum class ButtonAction : int32_t {
		eRelease = 0,
		ePress   = 1,
		eRepeat  = 2,
	};

	enum class Type {
		eKey,
		eCharacter,
		eCursorPosition,
		eCursorEnter,
		eMouseButton,
		eScroll,
	};

	struct KeyEvent {
		int32_t key;
		int32_t scancode;
		int32_t action;
		int32_t mods;
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
		int32_t button;
		ButtonAction action;
		int32_t mods;
	};

	struct ScrollEvent {
		double x_offset;
		double y_offset;
	};

	Type event;

	union {
		KeyEvent            key;
		CharacterEvent      character;
		CursorPositionEvent cursorPosition;
		CursorEnterEvent    cursorEnter;
		MouseButtonEvent    mouseButton;
		ScrollEvent         scroll;
	};
};

namespace le {
using UiEvent = LeUiEvent;
}

#endif

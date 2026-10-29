#pragma once

#include "stddef.h"
#include <wayland-client.h>
#include "generated/tablet-protocol.h"

struct le_window_o;

// just the public interface for interacting with the tablet window

struct le_window_tablet_wl_interface_t {
	bool ( *init )();                                                 // initialize tablet pointer logic globally - this gets called via int
	void ( *on_after_window_create )( le_window_o* created_window, void* wayland_surface ); // just after a window gets created
	void ( *on_before_window_destroy )( le_window_o* doomed_window ); // just before a window gets destroyed

	struct tablet_manager_o* persistent_state; // we don't care if this leaks on app exit

	// --------- wl tablet interface
	struct zwp_tablet_seat_v2_listener wl_seat_listener_i;
	struct wl_registry_listener        wl_registry_listener_i;
	struct zwp_tablet_v2_listener      wl_tablet_listener_i;
	struct zwp_tablet_tool_v2_listener wl_tool_listener_i;
};
#include "le_window.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>
#include "le_ui_event.h"

#define GLFW_INCLUDE_VULKAN
#define GLFW_INCLUDE_NONE
#include "GLFW/glfw3.h"

#define GLFW_EXPOSE_NATIVE_WAYLAND // todo: we should only expose this if we're actually building against wayland
#include "GLFW/glfw3native.h"

#include <wayland-client.h>
#include "generated/tablet-protocol.h"
#include "le_log.h"

constexpr auto LOGGER_LABEL = "le_window_tablet_wayland";

//	Note that this is a private interface --
//  user interaction should only happen via the LeUiEvent system; consumers
//  Of UI Events should be oblivious of how events were generated.

struct tablet_manager_o {
	uint32_t              g_nextTabletId         = 1;
	uint32_t              g_nextToolId           = 1;
	uint32_t              g_tabletManagerId      = 0;
	uint32_t              g_tabletManagerVersion = 0;
	std::vector<uint32_t> g_seatIds;
};

#include "private/le_window/le_window_tablet_wayland.h"

inline static wl_tablet_interface_t* get_api() {
	return ( wl_tablet_interface_t* )le_window_api_i->private_tablet_i;
}

// helper method to return the peristent state
inline static tablet_manager_o& get_persistent_state() {
	return *( get_api()->persistent_state );
}

/*
 * A tablet has one or more tools
 *
 * - a `tool` is the physical pen, which can have different roles: brush, eraser, pencil,...
 * - a `pad` is extra controls that are either available to the tablet (such as buttons or rotary wheels) or to the pen.
 *
 * See also the wayland protocol: https://wayland.app/protocols/tablet-v2
 *
 * How do we map tools to windows?
 * we should have a central responder. this responder should keep track of which window currently has the focus.
 * only the window that has the focus should receive the event through its event queue.
 *
 * - we need a map of surface -> window
 * - we need to be careful to make sure that the window is still alive when we want to pass an event to the window
 *
 * - this means that we need to subscribe to window creation events from here, so that we can associate a wayland surface with a le_window
 * - this means that we need to subscribe to window destruction events from here, so that we can remove windows from our map once they have been destroyed.
 *
 * - how does glfw deal with mouse focus when it uses wayland?
 *
 */

struct ToolInfo {
	uint32_t id;
	int32_t  event;
	uint64_t serial;
	float    x;
	float    y;
	float    tilt_x;
	float    tilt_y;
	uint32_t pressure;
	uint32_t distance;
};

// Enforce lazy initialization for logger().object
static le::Log& logger() {
	static auto logger = le::Log( LOGGER_LABEL );
	return logger;
};

static void tool_on_tool_type( void* data, struct zwp_tablet_tool_v2* tool, uint32_t toolType ) {
	( void )tool;
	static_cast<ToolInfo*>( data )->event = static_cast<int>( toolType );
}

static void tool_on_hardware_serial( void* data, struct zwp_tablet_tool_v2* tool, uint32_t hi, uint32_t lo ) {
	( void )tool;
	ToolInfo* t = static_cast<ToolInfo*>( data );
	t->serial   = ( static_cast<uint64_t>( hi ) << 32 ) | lo;
}

static void tool_on_hardware_id_wacom( void* data, struct zwp_tablet_tool_v2* tool, uint32_t hi, uint32_t lo ) {
	( void )data;
	( void )tool;
	( void )hi;
	( void )lo;
}

static void tool_on_capability( void* data, struct zwp_tablet_tool_v2* tool, uint32_t capability ) {
	( void )data;
	( void )tool;
	( void )capability;
}

static void tool_on_done( void* data, struct zwp_tablet_tool_v2* tool ) {
	( void )tool;
	const ToolInfo* t = static_cast<ToolInfo*>( data );
	LeUiEvent       e{};

	e.event      = LeUiEvent::Type::eTabletTool;
	e.tabletTool = { t->id, t->event, t->serial };
	// postLeUiEvent( e );
}

static void tool_on_removed( void* data, struct zwp_tablet_tool_v2* tool ) {
	( void )tool;
	delete static_cast<ToolInfo*>( data );
}

static void tool_on_proximity_out( void* data, struct zwp_tablet_tool_v2* tool ) {
	( void )tool;
	LeUiEvent e{};
	e.event           = LeUiEvent::Type::eTabletProximity;
	e.tabletProximity = { static_cast<ToolInfo*>( data )->id, 0 };
	// postLeUiEvent( e );
}

static void tool_on_down( void* data, struct zwp_tablet_tool_v2* tool, uint32_t serial ) {
	( void )tool;
	( void )serial;
	LeUiEvent e{};
	e.event     = LeUiEvent::Type::eTabletTip;
	e.tabletTip = { static_cast<ToolInfo*>( data )->id, 1 };
	// postLeUiEvent( e );
}

static void tool_on_up( void* data, struct zwp_tablet_tool_v2* tool ) {
	( void )tool;
	LeUiEvent e{};
	e.event     = LeUiEvent::Type::eTabletTip;
	e.tabletTip = { static_cast<ToolInfo*>( data )->id, 0 };
	// postLeUiEvent( e );
}

static void tool_on_motion( void* data, struct zwp_tablet_tool_v2* tool, wl_fixed_t x, wl_fixed_t y ) {
	( void )tool;
	ToolInfo* t = static_cast<ToolInfo*>( data );
	t->x        = static_cast<float>( wl_fixed_to_double( x ) ); // surface-local logical pixels
	t->y        = static_cast<float>( wl_fixed_to_double( y ) );
}
static void tool_on_proximity_in( void* data, struct zwp_tablet_tool_v2* tool, uint32_t serial, struct zwp_tablet_v2* tablet, struct wl_surface* surface ) {
	( void )tool;
	( void )serial;
	( void )tablet;
	( void )surface;

	// surface tells us which window should get notified of the event
	LeUiEvent e{};
	e.event           = LeUiEvent::Type::eTabletProximity;
	e.tabletProximity = { static_cast<ToolInfo*>( data )->id, 1 };
	logger().info( "updated proximity in!" );
	;
	// postLeUiEvent( e );
}

static void tool_on_pressure( void* data, struct zwp_tablet_tool_v2* tool, uint32_t pressure ) {
	( void )tool;
	static_cast<ToolInfo*>( data )->pressure = pressure;
}

static void tool_on_distance( void* data, struct zwp_tablet_tool_v2* tool, uint32_t distance ) {
	( void )tool;
	static_cast<ToolInfo*>( data )->distance = distance;
}

static void tool_on_tilt( void* data, struct zwp_tablet_tool_v2* tool, wl_fixed_t tiltX, wl_fixed_t tiltY ) {
	( void )tool;
	ToolInfo* t = static_cast<ToolInfo*>( data );
	t->tilt_x   = static_cast<float>( wl_fixed_to_double( tiltX ) );
	t->tilt_y   = static_cast<float>( wl_fixed_to_double( tiltY ) );
}

static void tool_on_rotation( void* data, struct zwp_tablet_tool_v2* tool, wl_fixed_t degrees ) {
	( void )data;
	( void )tool;
	( void )degrees;
}

static void tool_on_slider( void* data, struct zwp_tablet_tool_v2* tool, int32_t position ) {
	( void )data;
	( void )tool;
	( void )position;
}

static void tool_on_wheel( void* data, struct zwp_tablet_tool_v2* tool,
                           wl_fixed_t degrees, int32_t clicks ) {
	( void )data;
	( void )tool;
	( void )degrees;
	( void )clicks;
}

static void tool_on_button( void* data, struct zwp_tablet_tool_v2* tool,
                            uint32_t serial, uint32_t button, uint32_t state ) {
	( void )tool;
	( void )serial;
	const ToolInfo* t = static_cast<ToolInfo*>( data );
	LeUiEvent       e{};
	e.event        = LeUiEvent::Type::eTabletButton;
	e.tabletButton = { t->id, button, static_cast<int>( state ) };
	// postLeUiEvent( e );
}

static void tool_on_frame( void* data, struct zwp_tablet_tool_v2* tool, uint32_t time ) {
	( void )tool;
	( void )time;
	const ToolInfo* t = static_cast<ToolInfo*>( data );
	LeUiEvent       e{};
	e.event        = LeUiEvent::Type::eTabletMotion;
	e.tabletMotion = {
	    t->id,
	    t->x,
	    t->y,
	    t->pressure,
	    static_cast<int>( t->tilt_x ),
	    static_cast<int>( t->tilt_y ),
	    t->distance,
	};
	//	postLeUiEvent( e );
}

static void tablet_on_name( void* data, struct zwp_tablet_v2* tablet, const char* name ) {
	( void )data;
	( void )tablet;
	( void )name;
	logger().info( "Tablet on Name: %s", name );
}

static void tablet_on_id( void* data, struct zwp_tablet_v2* tablet, uint32_t vid, uint32_t pid ) {
	( void )data;
	( void )tablet;
	( void )vid;
	( void )pid;
	logger().info( "Tablet on ID: vid: %d | pid: %d", vid, pid );
}

static void tablet_on_path( void* data, struct zwp_tablet_v2* tablet, const char* path ) {
	( void )data;
	( void )tablet;
	( void )path;
}

static void tablet_on_done( void* data, struct zwp_tablet_v2* tablet ) {
	( void )tablet;
	LeUiEvent e{};
	e.event          = LeUiEvent::Type::eTabletAttached;
	e.tabletAttached = { static_cast<uint32_t>( reinterpret_cast<uintptr_t>( data ) ) };
	logger().info( "tablet added: %d", e.tabletAttached.id );
}

// TODO: fill out
static void tablet_on_removed( void* data, struct zwp_tablet_v2* tablet ) {
	( void )data;
	( void )tablet;
}

// TODO: fill out
static void tablet_on_bustype( void* data, struct zwp_tablet_v2* tablet, uint32_t busevent ) {
	( void )data;
	( void )tablet;
	( void )busevent;
}

static void seat_on_tablet_added( void* data, struct zwp_tablet_seat_v2* seat,
                                  struct zwp_tablet_v2* tablet ) {
	( void )data;
	( void )seat;
	zwp_tablet_v2_add_listener(
	    tablet, ( zwp_tablet_v2_listener* )( &get_api()->wl_tablet_listener_i ),
	    reinterpret_cast<void*>( static_cast<uintptr_t>( get_persistent_state().g_nextTabletId++ ) ) );
}

static void seat_on_pad_added( void* data, struct zwp_tablet_seat_v2* seat,
                               struct zwp_tablet_pad_v2* pad ) {
	( void )data;
	( void )seat;
	( void )pad; // pad scope is an open decision; slot must not be null
}

static void tablet_registry_on_global( void* data, wl_registry* registry, uint32_t id,
                                       const char* interface, uint32_t version ) {
	( void )data;
	if ( std::strcmp( interface, "zwp_tablet_manager_v2" ) == 0 ) {
		auto& s                  = get_persistent_state();
		s.g_tabletManagerId      = id;
		s.g_tabletManagerVersion = version;
	} else if ( std::strcmp( interface, "wl_seat" ) == 0 ) {
		get_persistent_state().g_seatIds.push_back( id );
	}
}

static void tablet_registry_on_global_remove( void*               data,
                                              struct wl_registry* wl_registry,
                                              uint32_t            name ) {
	( void )data;
	( void )wl_registry;
};

static void seat_on_tool_added( void* data, struct zwp_tablet_seat_v2* seat,
                                struct zwp_tablet_tool_v2* tool ) {
	( void )data;
	( void )seat;
	ToolInfo* t = new ToolInfo{}; // deleted OnToolRemoved
	t->id       = get_persistent_state().g_nextToolId++;
	zwp_tablet_tool_v2_add_listener( tool, &get_api()->wl_tool_listener_i, t );
}

// ----------

// make this a static method
static bool le_window_init_tablet() {

	// Note: This can only be called once a window has been created.
	struct wl_display* display = glfwGetWaylandDisplay();

	if ( nullptr == display ) {
		// a wayland display must have been created at this point otherwise we do not allow you
		// to go further
		return false;
	}

	struct wl_registry* registry = wl_display_get_registry( display );

	wl_registry_add_listener( registry, &get_api()->wl_registry_listener_i, nullptr );

	if ( wl_display_roundtrip( display ) < 0 ) {
		return false;
	}

	auto& state = get_persistent_state();

	if ( !state.g_tabletManagerId ) {
		return false;
	}

	zwp_tablet_manager_v2* manager =
	    static_cast<zwp_tablet_manager_v2*>(
	        wl_registry_bind( registry, state.g_tabletManagerId, &zwp_tablet_manager_v2_interface, state.g_tabletManagerVersion ) );

	// we should probably pass through the window here as the last element in data
	//
	for ( uint32_t seatId : state.g_seatIds ) {
		wl_seat*            seat       = static_cast<wl_seat*>( wl_registry_bind( registry, seatId, &wl_seat_interface, 1 ) );
		zwp_tablet_seat_v2* tabletSeat = zwp_tablet_manager_v2_get_tablet_seat( manager, seat );
		zwp_tablet_seat_v2_add_listener( tabletSeat, &get_api()->wl_seat_listener_i, nullptr );
	}

	wl_display_flush( display );

	return true;
}

// ----------------------------------------------------------------

// once a window has been created, we need to update our map of existing windows
static void on_after_window_create( le_window_o* window ) {
}

// ----------------------------------------------------------------
// if a window was removed, then we need to remove it from our list of existing windows
static void on_before_window_destroy( le_window_o* window ) {
}

// ----------------------------------------------------------------

// this function gets called from "le_window.cpp" on api registration
void le_window_tablet_wayland_register_api( void* api ) {
	auto window_api_i = static_cast<le_window_api*>( api );

	// Function pointers for wayland callbacks. Field order must
	// follow wayland-scanner; every slot must be non-null.

	{
		auto& api = window_api_i->private_tablet_i;

		if ( api == nullptr ) {
			api = new wl_tablet_interface_t{
			    .persistent_state = new tablet_manager_o(),
			};
		}

		{
			auto api_i                      = ( wl_tablet_interface_t* )api;
			api_i->on_after_window_create   = on_after_window_create;
			api_i->on_before_window_destroy = on_before_window_destroy;
			api_i->init                     = le_window_init_tablet;
		}

		{
			auto  api_i             = ( wl_tablet_interface_t* )api;
			auto& seat_listener_api = api_i->wl_seat_listener_i;

			seat_listener_api.tablet_added = seat_on_tablet_added;
			seat_listener_api.tool_added   = seat_on_tool_added;
			seat_listener_api.pad_added    = seat_on_pad_added;
		}

		{
			auto  api_i                 = ( wl_tablet_interface_t* )api;

			auto& registry_listener_api         = api_i->wl_registry_listener_i;
			registry_listener_api.global        = tablet_registry_on_global;
			registry_listener_api.global_remove = tablet_registry_on_global_remove;
		}

		{
			auto  api_i               = ( wl_tablet_interface_t* )api;
			auto& tablet_listener_api = api_i->wl_tablet_listener_i;

			tablet_listener_api.name    = tablet_on_name;
			tablet_listener_api.id      = tablet_on_id;
			tablet_listener_api.path    = tablet_on_path;
			tablet_listener_api.done    = tablet_on_done;
			tablet_listener_api.removed = tablet_on_removed;
			tablet_listener_api.bustype = tablet_on_bustype;
		}
		{
			auto  api_i             = ( wl_tablet_interface_t* )api;
			auto& tool_listener_api = api_i->wl_tool_listener_i;

			//
			// We should be able to update these addresses directly when hot-reloading;
			// as wayland receives the address to the function table (which we keep
			// constant in our function registry) -- here, we update the table entries
			//
			//
			tool_listener_api.type              = tool_on_tool_type;
			tool_listener_api.hardware_serial   = tool_on_hardware_serial;
			tool_listener_api.hardware_id_wacom = tool_on_hardware_id_wacom;
			tool_listener_api.capability        = tool_on_capability;
			tool_listener_api.done              = tool_on_done;
			tool_listener_api.removed           = tool_on_removed;
			tool_listener_api.proximity_in      = tool_on_proximity_in;  // this may change the current surface
			tool_listener_api.proximity_out     = tool_on_proximity_out; // this may change the current surface
			tool_listener_api.down              = tool_on_down;
			tool_listener_api.up                = tool_on_up;
			tool_listener_api.motion            = tool_on_motion;
			tool_listener_api.pressure          = tool_on_pressure;
			tool_listener_api.distance          = tool_on_distance;
			tool_listener_api.tilt              = tool_on_tilt;
			tool_listener_api.rotation          = tool_on_rotation;
			tool_listener_api.slider            = tool_on_slider;
			tool_listener_api.wheel             = tool_on_wheel;
			tool_listener_api.button            = tool_on_button;
			tool_listener_api.frame             = tool_on_frame;
		}
	}
};
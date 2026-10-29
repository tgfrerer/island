#include "le_window.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <string>
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

struct tablet_info_t {
	std::string name;
	std::string path;
	uint32_t    id       = 0;
	uint32_t    vid      = 0;
	uint32_t    pid      = 0;
	uint32_t    busevent = 0;
};

struct tablet_manager_o {
	uint32_t              next_tablet_id         = 1;
	uint32_t              next_tool_id           = 1;
	uint32_t              tablet_manager_id      = 0;
	uint32_t              tablet_manager_version = 0;
	std::vector<uint32_t> seat_ids;

	// table surfaces <-> windows;
	//
	// we assume that there will never be more than a few and any complex
	// data structures to manage this will be overkill.
	//
	std::vector<wl_surface*>  surfaces; // kept in sync with windows
	std::vector<le_window_o*> windows;  // kept in sync with surfaces

	le_window_o* focused_window = nullptr; // null if no window has focus

	std::unordered_map<zwp_tablet_v2*, tablet_info_t> tablets;
};

#include "private/le_window/le_window_tablet_wayland.h"

inline static le_window_tablet_wl_interface_t* get_api() {
	return ( le_window_tablet_wl_interface_t* )le_window_api_i->private_tablet_i;
}

/// helper method to return the peristent state
// consider: we could find a way to protect access to persistent state via mutex
// if we route through this
inline static tablet_manager_o& get_persistent_state() {
	return *( get_api()->persistent_state );
}

struct tool_info_t {
	uint32_t id;
	int32_t  event;
	uint32_t type;
	uint64_t serial;
	float    x;
	float    y;
	float    tilt_x;
	float    tilt_y;
	float    rotation_deg;
	float    pressure;
	uint32_t distance;
};

// Enforce lazy initialization for logger().object
static le::Log& logger() {
	static auto logger = le::Log( LOGGER_LABEL );
	return logger;
};

// ----------

static void push_event( LeUiEvent const&& e ) {
	static auto const& state = get_persistent_state();

	switch ( e.event ) {
		// events that don't have focus requirements
		// these get broadcast to all available windows
	case LeUiEvent::Type::eTabletAttached: // deliberate fallthrough
	case LeUiEvent::Type::eTabletTool: {
		for ( auto& w : state.windows ) {
			le_window_api_i->window_i.enqueue_event( w, &e );
		}
		break;
	}
		// focus-related events - these only get forwarded
		// to the window that has focus
	case LeUiEvent::Type::eTabletProximity:
	case LeUiEvent::Type::eTabletMotion:
	case LeUiEvent::Type::eTabletButton:
	case LeUiEvent::Type::eTabletTip: {
		if ( state.focused_window ) {
			le_window_api_i->window_i.enqueue_event( state.focused_window, &e );
		}
		break;
	}
	default:
		return;
	}
}

// ---------- zwp_tablet_tool_v2_listener interface
// data*: set to tool_info_t* for the current tool

static void tool_on_tool_type( void* data, struct zwp_tablet_tool_v2* tool, uint32_t toolType ) {
	tool_info_t* t = static_cast<tool_info_t*>( data );
	t->type        = toolType;
}

static void tool_on_hardware_serial( void* data, struct zwp_tablet_tool_v2* tool, uint32_t hi, uint32_t lo ) {
	tool_info_t* t = static_cast<tool_info_t*>( data );
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

// Called at the end of an event sequence introducing the tool
// and its capabilities.
static void tool_on_done( void* data, struct zwp_tablet_tool_v2* tool ) {
	const tool_info_t* t = static_cast<tool_info_t*>( data );
	LeUiEvent       e{};
	e.event      = LeUiEvent::Type::eTabletTool;
	e.tabletTool = { t->id, t->type, t->serial };
	push_event( std::move( e ) );
}

static void tool_on_removed( void* data, struct zwp_tablet_tool_v2* tool ) {
	delete static_cast<tool_info_t*>( data );
}

static void tool_on_proximity_out( void* data, struct zwp_tablet_tool_v2* tool ) {
	tool_info_t* t = static_cast<tool_info_t*>( data );
	LeUiEvent    e{};
	e.event           = LeUiEvent::Type::eTabletProximity;
	e.tabletProximity = { t->id, 0 };
	push_event( std::move( e ) );

	// Update focus
	auto& state          = get_persistent_state();
	state.focused_window = nullptr;
	logger().info( "Proximity OUT xxx " );
}

static void tool_on_down( void* data, struct zwp_tablet_tool_v2* tool, uint32_t serial ) {
	tool_info_t* t = static_cast<tool_info_t*>( data );
	LeUiEvent    e{};
	e.event     = LeUiEvent::Type::eTabletTip;
	e.tabletTip = { t->id, 1 };
	push_event( std::move( e ) );
}

static void tool_on_up( void* data, struct zwp_tablet_tool_v2* tool ) {
	tool_info_t* t = static_cast<tool_info_t*>( data );
	LeUiEvent    e{};
	e.event     = LeUiEvent::Type::eTabletTip;
	e.tabletTip = { t->id, 0 };
	push_event( std::move( e ) );
}

static void tool_on_motion( void* data, struct zwp_tablet_tool_v2* tool, wl_fixed_t x, wl_fixed_t y ) {
	tool_info_t* t = static_cast<tool_info_t*>( data );
	t->x        = static_cast<float>( wl_fixed_to_double( x ) ); // surface-local logical pixels
	t->y        = static_cast<float>( wl_fixed_to_double( y ) );
}

static void tool_on_proximity_in( void* data, struct zwp_tablet_tool_v2* tool, uint32_t serial, struct zwp_tablet_v2* tablet, struct wl_surface* surface ) {

	tool_info_t* t = static_cast<tool_info_t*>( data );
	// surface tells us which window should get notified of the event
	LeUiEvent e{};
	e.event           = LeUiEvent::Type::eTabletProximity;
	e.tabletProximity = { t->id, 1 };
	logger().info( "Proximity in **** " );

	auto& state = get_persistent_state();

	auto surf_id = std::find( state.surfaces.begin(), state.surfaces.end(), surface );

	if ( surf_id != state.surfaces.end() ) {
		// element was found
		int32_t found_surface_index = surf_id - state.surfaces.begin();
		if ( found_surface_index < state.windows.size() ) {
			state.focused_window = state.windows[ found_surface_index ];
		} else {
			state.focused_window = nullptr;
		}
	} else {
		state.focused_window = nullptr;
	}

	push_event( std::move( e ) );
}

static void tool_on_pressure( void* data, struct zwp_tablet_tool_v2* tool, uint32_t pressure ) {
	tool_info_t* t = static_cast<tool_info_t*>( data );
	t->pressure    = float( pressure ) / float( 65535 );
}

static void tool_on_distance( void* data, struct zwp_tablet_tool_v2* tool, uint32_t distance ) {
	tool_info_t* t = static_cast<tool_info_t*>( data );
	t->distance    = distance;
}

static void tool_on_tilt( void* data, struct zwp_tablet_tool_v2* tool, wl_fixed_t tiltX, wl_fixed_t tiltY ) {
	tool_info_t* t = static_cast<tool_info_t*>( data );
	t->tilt_x   = static_cast<float>( wl_fixed_to_double( tiltX ) );
	t->tilt_y   = static_cast<float>( wl_fixed_to_double( tiltY ) );
}

static void tool_on_rotation( void* data, struct zwp_tablet_tool_v2* tool, wl_fixed_t degrees ) {
	tool_info_t* t  = static_cast<tool_info_t*>( data );
	t->rotation_deg = static_cast<float>( wl_fixed_to_double( degrees ) );
	logger().info( "rotation: %d", degrees );
}

static void tool_on_slider( void* data, struct zwp_tablet_tool_v2* tool, int32_t position ) {
	// noop for now
}

static void tool_on_frame( void* data, struct zwp_tablet_tool_v2* tool, uint32_t time ) {
	const tool_info_t* t = static_cast<tool_info_t*>( data );
	LeUiEvent       e{};
	e.event        = LeUiEvent::Type::eTabletMotion;
	e.tabletMotion = {
	    t->id, // tool id
	    t->x,
	    t->y,
	    t->pressure,
	    t->rotation_deg,
	    static_cast<int>( t->tilt_x ),
	    static_cast<int>( t->tilt_y ),
	    t->distance,
	};
	push_event( std::move( e ) );
}

static void tool_on_wheel( void* data, struct zwp_tablet_tool_v2* tool, wl_fixed_t degrees, int32_t clicks ) {
	// noop
}

static void tool_on_button( void* data, struct zwp_tablet_tool_v2* tool, uint32_t serial, uint32_t button, uint32_t state ) {
	const tool_info_t* t = static_cast<tool_info_t*>( data );
	LeUiEvent       e{};
	e.event        = LeUiEvent::Type::eTabletButton;
	e.tabletButton = { t->id, button, state };
	push_event( std::move( e ) );
}

// ---------- zwp_tablet_v2_listener interface
// data*: set via zwp_tablet_v2_add_listener to tablet_info_t*

static void tablet_on_name( void* data, struct zwp_tablet_v2* tablet, const char* name ) {
	auto tablet_info  = ( tablet_info_t* )data;
	tablet_info->name = name;
	logger().info( "Tablet %d on Name: %s", tablet_info->id, name );
}

static void tablet_on_id( void* data, struct zwp_tablet_v2* tablet, uint32_t vid, uint32_t pid ) {
	auto tablet_info = ( tablet_info_t* )data;
	tablet_info->vid = vid;
	tablet_info->pid = pid;

	logger().info( "Tablet %d on_id: vid: %d | pid: %d", tablet_info->id, tablet_info->vid, tablet_info->pid );
}

static void tablet_on_path( void* data, struct zwp_tablet_v2* tablet, const char* path ) {
	auto tablet_info  = ( tablet_info_t* )data;
	tablet_info->path = path;
	logger().info( "Tablet %d path: %s", tablet_info->id, tablet_info->path.c_str() );
}

static void tablet_on_bustype( void* data, struct zwp_tablet_v2* tablet, uint32_t busevent ) {
	auto tablet_info      = ( tablet_info_t* )data;
	tablet_info->busevent = busevent;
}

static void tablet_on_removed( void* data, struct zwp_tablet_v2* tablet ) {
	auto tablet_info = ( tablet_info_t* )data;
	logger().info( "Tablet %d REMOVE", tablet_info->id );
	uint32_t num_elements_erased = get_persistent_state().tablets.erase( tablet );
	assert( num_elements_erased == 1 );
}

static void tablet_on_done( void* data, struct zwp_tablet_v2* tablet ) {
	auto      tablet_info = ( tablet_info_t* )data;
	LeUiEvent e{};
	e.event             = LeUiEvent::Type::eTabletAttached;
	e.tabletAttached.id = tablet_info->id;
	logger().info( "Tablet %d DONE", e.tabletAttached.id );
	push_event( std::move( e ) );
}

// --------- zwp_tablet_seat_v2_listener interface implementation
// data* set with zwp_tablet_seat_v2_add_listener, but unused for now

static void seat_on_tablet_added( void* data, struct zwp_tablet_seat_v2* seat, struct zwp_tablet_v2* tablet ) {

	auto& state = get_persistent_state();

	// insert a new element at tablet
	auto& t = state.tablets[ tablet ];

	t.id = state.next_tablet_id++;

	logger().info( "Tablet %d ADD", t.id );

	zwp_tablet_v2_add_listener( tablet, &get_api()->wl_tablet_listener_i, &state.tablets.at( tablet ) );
}

static void seat_on_pad_added( void* data, struct zwp_tablet_seat_v2* seat, struct zwp_tablet_pad_v2* pad ) {
	( void )data;
	( void )seat;
	( void )pad;
}

static void seat_on_tool_added( void* data, struct zwp_tablet_seat_v2* seat, struct zwp_tablet_tool_v2* tool ) {
	tool_info_t* tool_info = new tool_info_t{}; // deleted OnToolRemoved
	tool_info->id          = get_persistent_state().next_tool_id++;
	zwp_tablet_tool_v2_add_listener( tool, &get_api()->wl_tool_listener_i, tool_info );
}

// --------- wl_registry_listener interface
// data* set with wl_registry_add_listener, but unused for now

static void tablet_registry_on_global( void* data, wl_registry* registry, uint32_t id, const char* interface, uint32_t version ) {
	if ( std::strcmp( interface, "zwp_tablet_manager_v2" ) == 0 ) {
		auto& s                  = get_persistent_state();
		s.tablet_manager_id      = id;
		s.tablet_manager_version = version;
	} else if ( std::strcmp( interface, "wl_seat" ) == 0 ) {
		get_persistent_state().seat_ids.push_back( id );
	}
}

static void tablet_registry_on_global_remove( void* data, struct wl_registry* wl_registry, uint32_t name ) {
	( void )data;
	( void )wl_registry;
};

// ----------

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

	if ( !state.tablet_manager_id ) {
		return false;
	}

	zwp_tablet_manager_v2* manager = static_cast<zwp_tablet_manager_v2*>(
	    wl_registry_bind( registry, state.tablet_manager_id, &zwp_tablet_manager_v2_interface, state.tablet_manager_version ) );

	// we should probably pass through the window here as the last element in data
	//
	for ( uint32_t seatId : state.seat_ids ) {
		wl_seat*            seat       = static_cast<wl_seat*>( wl_registry_bind( registry, seatId, &wl_seat_interface, 1 ) );
		zwp_tablet_seat_v2* tabletSeat = zwp_tablet_manager_v2_get_tablet_seat( manager, seat );
		zwp_tablet_seat_v2_add_listener( tabletSeat, &get_api()->wl_seat_listener_i, nullptr );
	}

	wl_display_flush( display );

	return true;
}

// ----------------------------------------------------------------

// once a window has been created, we need to update our map of existing windows
static void on_after_window_create( le_window_o* window, void* wayland_surface ) {
	logger().info( "window create: island window: %p, wayland surface: %p", window, wayland_surface );

	auto& state = get_persistent_state();

	// make sure that both window and surfaces have not been seen before
	assert( state.surfaces.end() == std::ranges::find( state.surfaces.begin(), state.surfaces.end(), wayland_surface ) );
	assert( state.windows.end() == std::ranges::find( state.windows.begin(), state.windows.end(), window ) );

	state.surfaces.push_back( static_cast<wl_surface*>( wayland_surface ) );
	state.windows.push_back( window );
}

// ----------------------------------------------------------------
// if a window was removed, then we need to remove it from our list of existing windows
// we also remove the associated surface
static void on_before_window_destroy( le_window_o* window ) {
	logger().info( "window destroy %x", window );

	auto& state = get_persistent_state();

	auto it_windows  = state.windows.begin();
	auto it_surfaces = state.surfaces.begin();

	for ( ; it_windows != state.windows.end(); ) {
		if ( *it_windows == window ) {
			it_windows  = state.windows.erase( it_windows );
			it_surfaces = state.surfaces.erase( it_surfaces );
			continue;
		}
		it_windows++, it_surfaces++;
	}
}

// ----------------------------------------------------------------

// this function gets called from "le_window.cpp" on api registration
void le_window_tablet_wayland_register_api( void* p_api ) {
	le_window_api* window_api = static_cast<le_window_api*>( p_api );

	// Function pointers for wayland callbacks. Field order must
	// follow wayland-scanner; every slot must be non-null.

	{

		if ( window_api->private_tablet_i == nullptr ) {
			window_api->private_tablet_i = new le_window_tablet_wl_interface_t{
			    .persistent_state = new tablet_manager_o(),
			};
		}

		auto api_i = ( le_window_tablet_wl_interface_t* )window_api->private_tablet_i;

		// The following interfaces are exposed to wayland.
		//
		// Because we publish the addresses of the structs
		// holding the function pointers to wayland, we can
		// directly update the function pointers here when
		// hot-reloading.
		//
		// This assumes that wayland does not cache the
		// addresses internally.

		{
			api_i->on_after_window_create   = on_after_window_create;
			api_i->on_before_window_destroy = on_before_window_destroy;
			api_i->init                     = le_window_init_tablet;
		}

		{
			auto& seat_listener_api = api_i->wl_seat_listener_i;

			seat_listener_api.tablet_added = seat_on_tablet_added;
			seat_listener_api.tool_added   = seat_on_tool_added;
			seat_listener_api.pad_added    = seat_on_pad_added;
		}

		{

			auto& registry_listener_api         = api_i->wl_registry_listener_i;
			registry_listener_api.global        = tablet_registry_on_global;
			registry_listener_api.global_remove = tablet_registry_on_global_remove;
		}

		{
			auto& tablet_listener_api = api_i->wl_tablet_listener_i;

			tablet_listener_api.name    = tablet_on_name;
			tablet_listener_api.id      = tablet_on_id;
			tablet_listener_api.path    = tablet_on_path;
			tablet_listener_api.done    = tablet_on_done;
			tablet_listener_api.removed = tablet_on_removed;
			tablet_listener_api.bustype = tablet_on_bustype;
		}
		{
			auto& tool_listener_api = api_i->wl_tool_listener_i;

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
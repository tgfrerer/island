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

// TODO: get rid of globals, because they will not survive reloading this module.

static uint32_t g_nextTabletId         = 1;
static uint32_t g_nextToolId           = 1;
static uint32_t g_tabletManagerId      = 0;
static uint32_t g_tabletManagerVersion = 0;

static std::vector<uint32_t> g_seatIds;

/*
 * A tablet has one or more tools
 *
 * - a `tool` is the physical pen, which can have different roles: brush, eraser, pencil,...
 * - a `pad` is extra controls that are either available to the tablet (such as buttons or rotary wheels) or to the pen.
 *
 * See also the wayland protocol: https://wayland.app/protocols/tablet-v2
 *
 * How do we map tools to windows?
 * we should have a central responder. this responder should keep track of which
 *
 *
 * Event routing: currently, we are routing events to the window that receives them.
 * this means that we need to keep track of which surface currently has the focus
 * and pass pointer events only to the window which is associated with the surface
 * that is in focus.
 *
 * - we need a map of surface -> window
 * - we need to be careful to make sure that the window is still alive when we want to pass an event to the window
 *
 * - this means that we need to subscribe to window creation events from here, so that we can associate a wayland surface with a le_window
 * - this means that we need to subscribe to window destruction events from here, so that we can remove windows from our map once they have been destroyed.
 *
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

static void tool_on_hardware_serial( void* data, struct zwp_tablet_tool_v2* tool,
                                     uint32_t hi, uint32_t lo ) {
	( void )tool;
	ToolInfo* t = static_cast<ToolInfo*>( data );
	t->serial   = ( static_cast<uint64_t>( hi ) << 32 ) | lo;
}

static void toolOnHardwareIdWacom( void* data, struct zwp_tablet_tool_v2* tool,
                                   uint32_t hi, uint32_t lo ) {
	( void )data;
	( void )tool;
	( void )hi;
	( void )lo;
}

static void toolOnCapability( void* data, struct zwp_tablet_tool_v2* tool, uint32_t capability ) {
	( void )data;
	( void )tool;
	( void )capability;
}

static void toolOnDone( void* data, struct zwp_tablet_tool_v2* tool ) {
	( void )tool;
	const ToolInfo* t = static_cast<ToolInfo*>( data );
	LeUiEvent       e{};

	e.event      = LeUiEvent::Type::eTabletTool;
	e.tabletTool = { t->id, t->event, t->serial };
	// postLeUiEvent( e );
}

static void toolOnRemoved( void* data, struct zwp_tablet_tool_v2* tool ) {
	( void )tool;
	delete static_cast<ToolInfo*>( data );
}

static void toolOnProximityOut( void* data, struct zwp_tablet_tool_v2* tool ) {
	( void )tool;
	LeUiEvent e{};
	e.event           = LeUiEvent::Type::eTabletProximity;
	e.tabletProximity = { static_cast<ToolInfo*>( data )->id, 0 };
	// postLeUiEvent( e );
}

static void toolOnDown( void* data, struct zwp_tablet_tool_v2* tool, uint32_t serial ) {
	( void )tool;
	( void )serial;
	LeUiEvent e{};
	e.event     = LeUiEvent::Type::eTabletTip;
	e.tabletTip = { static_cast<ToolInfo*>( data )->id, 1 };
	// postLeUiEvent( e );
}

static void toolOnUp( void* data, struct zwp_tablet_tool_v2* tool ) {
	( void )tool;
	LeUiEvent e{};
	e.event     = LeUiEvent::Type::eTabletTip;
	e.tabletTip = { static_cast<ToolInfo*>( data )->id, 0 };
	// postLeUiEvent( e );
}

static void toolOnMotion( void* data, struct zwp_tablet_tool_v2* tool,
                          wl_fixed_t x, wl_fixed_t y ) {
	( void )tool;
	ToolInfo* t = static_cast<ToolInfo*>( data );
	t->x        = static_cast<float>( wl_fixed_to_double( x ) ); // surface-local logical pixels
	t->y        = static_cast<float>( wl_fixed_to_double( y ) );
}
static void toolOnProximityIn( void* data, struct zwp_tablet_tool_v2* tool, uint32_t serial,
                               struct zwp_tablet_v2* tablet, struct wl_surface* surface ) {
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

static void toolOnPressure( void* data, struct zwp_tablet_tool_v2* tool, uint32_t pressure ) {
	( void )tool;
	static_cast<ToolInfo*>( data )->pressure = pressure;
}

static void toolOnDistance( void* data, struct zwp_tablet_tool_v2* tool, uint32_t distance ) {
	( void )tool;
	static_cast<ToolInfo*>( data )->distance = distance;
}

static void toolOnTilt( void* data, struct zwp_tablet_tool_v2* tool,
                        wl_fixed_t tiltX, wl_fixed_t tiltY ) {
	( void )tool;
	ToolInfo* t = static_cast<ToolInfo*>( data );
	t->tilt_x   = static_cast<float>( wl_fixed_to_double( tiltX ) );
	t->tilt_y   = static_cast<float>( wl_fixed_to_double( tiltY ) );
}

static void toolOnRotation( void* data, struct zwp_tablet_tool_v2* tool, wl_fixed_t degrees ) {
	( void )data;
	( void )tool;
	( void )degrees;
}

static void toolOnSlider( void* data, struct zwp_tablet_tool_v2* tool, int32_t position ) {
	( void )data;
	( void )tool;
	( void )position;
}

static void toolOnWheel( void* data, struct zwp_tablet_tool_v2* tool,
                         wl_fixed_t degrees, int32_t clicks ) {
	( void )data;
	( void )tool;
	( void )degrees;
	( void )clicks;
}

static void toolOnButton( void* data, struct zwp_tablet_tool_v2* tool,
                          uint32_t serial, uint32_t button, uint32_t state ) {
	( void )tool;
	( void )serial;
	const ToolInfo* t = static_cast<ToolInfo*>( data );
	LeUiEvent       e{};
	e.event        = LeUiEvent::Type::eTabletButton;
	e.tabletButton = { t->id, button, static_cast<int>( state ) };
	// postLeUiEvent( e );
}

static void toolOnFrame( void* data, struct zwp_tablet_tool_v2* tool, uint32_t time ) {
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

static void tabletOnName( void* data, struct zwp_tablet_v2* tablet, const char* name ) {
	( void )data;
	( void )tablet;
	( void )name;
}

static void tabletOnId( void* data, struct zwp_tablet_v2* tablet, uint32_t vid, uint32_t pid ) {
	( void )data;
	( void )tablet;
	( void )vid;
	( void )pid;
}

static void tabletOnPath( void* data, struct zwp_tablet_v2* tablet, const char* path ) {
	( void )data;
	( void )tablet;
	( void )path;
}

static void tabletOnDone( void* data, struct zwp_tablet_v2* tablet ) {
	( void )tablet;
	LeUiEvent e{};
	e.event          = LeUiEvent::Type::eTabletAttached;
	e.tabletAttached = { static_cast<uint32_t>( reinterpret_cast<uintptr_t>( data ) ) };
	// postLeUiEvent( e );
}

// TODO: fill out
static void tabletOnRemoved( void* data, struct zwp_tablet_v2* tablet ) {
	( void )data;
	( void )tablet;
}

// TODO: fill out
static void tabletOnBusevent( void* data, struct zwp_tablet_v2* tablet, uint32_t busevent ) {
	( void )data;
	( void )tablet;
	( void )busevent;
}

static void seatOnTabletAdded( void* data, struct zwp_tablet_seat_v2* seat,
                               struct zwp_tablet_v2* tablet ) {
	( void )data;
	( void )seat;
	zwp_tablet_v2_add_listener(
	    tablet, ( zwp_tablet_v2_listener* )( &le_window_api_i->wl_tablet_listener_i ),
	    reinterpret_cast<void*>( static_cast<uintptr_t>( g_nextTabletId++ ) ) );
}

static void seatOnPadAdded( void* data, struct zwp_tablet_seat_v2* seat,
                            struct zwp_tablet_pad_v2* pad ) {
	( void )data;
	( void )seat;
	( void )pad; // pad scope is an open decision; slot must not be null
}

static void tabletRegistryOnGlobal( void* data, wl_registry* registry, uint32_t id,
                                    const char* interface, uint32_t version ) {
	( void )data;
	if ( std::strcmp( interface, "zwp_tablet_manager_v2" ) == 0 ) {
		g_tabletManagerId      = id;
		g_tabletManagerVersion = version;
	} else if ( std::strcmp( interface, "wl_seat" ) == 0 ) {
		g_seatIds.push_back( id );
	}
}

static void seatOnToolAdded( void* data, struct zwp_tablet_seat_v2* seat,
                             struct zwp_tablet_tool_v2* tool ) {
	( void )data;
	( void )seat;
	ToolInfo* t = new ToolInfo{}; // deleted OnToolRemoved
	t->id       = g_nextToolId++;
	zwp_tablet_tool_v2_add_listener( tool, ( zwp_tablet_tool_v2_listener* )( &le_window_api_i->wl_tool_listener_i ), t );
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

	wl_registry_add_listener( registry, ( wl_registry_listener* )( &le_window_api_i->wl_registry_listener_i ), nullptr );

	if ( wl_display_roundtrip( display ) < 0 )
		return false;
	if ( !g_tabletManagerId )
		return false;

	zwp_tablet_manager_v2* manager =
	    static_cast<zwp_tablet_manager_v2*>(
	        wl_registry_bind( registry, g_tabletManagerId, &zwp_tablet_manager_v2_interface, g_tabletManagerVersion ) );

	auto seatListenerApi = ( zwp_tablet_seat_v2_listener* )&le_window_api_i->wl_seat_listener_i;

	for ( uint32_t seatId : g_seatIds ) {
		wl_seat*            seat       = static_cast<wl_seat*>( wl_registry_bind( registry, seatId, &wl_seat_interface, 1 ) );
		zwp_tablet_seat_v2* tabletSeat = zwp_tablet_manager_v2_get_tablet_seat( manager, seat );
		zwp_tablet_seat_v2_add_listener( tabletSeat, seatListenerApi, nullptr );
	}

	wl_display_flush( display );

	return true;
}

// ----------------------------------------------------------------

// this function gets called from "le_window.cpp" on api registration
void le_window_tablet_wayland_register_api( void* api ) {
	auto window_api_i = static_cast<le_window_api*>( api );

	// Function pointers for wayland callbacks. Field order must
	// follow wayland-scanner; every slot must be non-null.

	{
		auto& seat_listener_api = window_api_i->wl_seat_listener_i;

		static_assert(
		    sizeof( seat_listener_api ) == sizeof( zwp_tablet_seat_v2_listener ),
		    "api must have the same field size" );

		seat_listener_api.fn[ 0 ] = ( void* )seatOnTabletAdded;
		seat_listener_api.fn[ 1 ] = ( void* )seatOnToolAdded;
		seat_listener_api.fn[ 2 ] = ( void* )seatOnPadAdded;
	}

	{
		auto& registry_listener_api = window_api_i->wl_registry_listener_i;
		registry_listener_api.fn    = ( void* )tabletRegistryOnGlobal;
	}

	{
		auto& tablet_listener_api = window_api_i->wl_tablet_listener_i;

		tablet_listener_api.fn[ 0 ] = ( void* )tabletOnName;
		tablet_listener_api.fn[ 1 ] = ( void* )tabletOnId;
		tablet_listener_api.fn[ 2 ] = ( void* )tabletOnPath;
		tablet_listener_api.fn[ 3 ] = ( void* )tabletOnDone;
		tablet_listener_api.fn[ 4 ] = ( void* )tabletOnRemoved;
		tablet_listener_api.fn[ 5 ] = ( void* )tabletOnBusevent;
	}

	{
		auto& tool_listener_api = window_api_i->wl_tool_listener_i;

		//
		// We should be able to update these addresses directly when hot-reloading;
		// as wayland receives the address to the function table (which we keep
		// constant in our function registry) -- here, we update the table entries
		//
		//
		tool_listener_api.fn[ 0 ]  = ( void* )tool_on_tool_type;
		tool_listener_api.fn[ 1 ]  = ( void* )tool_on_hardware_serial;
		tool_listener_api.fn[ 2 ]  = ( void* )toolOnHardwareIdWacom;
		tool_listener_api.fn[ 3 ]  = ( void* )toolOnCapability;
		tool_listener_api.fn[ 4 ]  = ( void* )toolOnDone;
		tool_listener_api.fn[ 5 ]  = ( void* )toolOnRemoved;
		tool_listener_api.fn[ 6 ]  = ( void* )toolOnProximityIn;  // this may change the current surface
		tool_listener_api.fn[ 7 ]  = ( void* )toolOnProximityOut; // this may change the current surface
		tool_listener_api.fn[ 8 ]  = ( void* )toolOnDown;
		tool_listener_api.fn[ 9 ]  = ( void* )toolOnUp;
		tool_listener_api.fn[ 10 ] = ( void* )toolOnMotion;
		tool_listener_api.fn[ 11 ] = ( void* )toolOnPressure;
		tool_listener_api.fn[ 12 ] = ( void* )toolOnDistance;
		tool_listener_api.fn[ 13 ] = ( void* )toolOnTilt;
		tool_listener_api.fn[ 14 ] = ( void* )toolOnRotation;
		tool_listener_api.fn[ 15 ] = ( void* )toolOnSlider;
		tool_listener_api.fn[ 16 ] = ( void* )toolOnWheel;
		tool_listener_api.fn[ 17 ] = ( void* )toolOnButton;
		tool_listener_api.fn[ 18 ] = ( void* )toolOnFrame;
	}
	{
		auto& api = window_api_i->wl_tablet_i;
		api.init  = le_window_init_tablet;
	}
};
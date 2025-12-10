#pragma once 
// generic operations on path - this is a shared interface with le_2d
struct le_path_operations_interface_t {
	void ( *move_to )( void* user_data, float2 const* p );
	void ( *line_to )( void* user_data, float2 const* p );
	void ( *quad_bezier_to )( void* user_data, float2 const* c1, float2 const* p );
	void ( *cubic_bezier_to )( void* user_data, float2 const* c1, float2 const* c2, float2 const* p );
	void ( *arc_to )( void* user_data, float2 const* p, float2 const* radii, float phi, bool large_arc, bool sweep );
	void ( *close )( void* user_data );
};


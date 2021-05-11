// Outputs
struct VSOutput {
	float4 position  : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

// arguments (via push constants)
struct Params {
	float2 u_mouse;
	float2 u_resolution;
	float u_time;
};

#if defined(_DXC)
[[vk::push_constant]] Params pc;
#else
[[vk::push_constant]] ConstantBuffer<Params> pc;
#endif


float4 main(VSOutput input) : SV_TARGET {

	float2 aspect_ratio = float2((pc.u_resolution.x / pc.u_resolution.y), 1);

	float2 st = input.texCoord.xy;
	float2 m  = pc.u_mouse;

	// Scale by aspect ratio in case we encounter a non-square canvas
	st *= aspect_ratio;
	m  *= aspect_ratio;

	float dist_to_mouse = distance( st, m );

	float highlight = smoothstep( 1-(51/pc.u_resolution.x), 1-(50/pc.u_resolution.x), 1 - dist_to_mouse );

	float3 color = float3( input.texCoord.xy, 1 * abs(sin(pc.u_time)) );
	color     += float3( highlight * 0.5 );

	return float4( color, 1 );
}
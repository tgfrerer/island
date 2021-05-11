
// inputs 
struct VSInput
{
	[[vk::location(0)]]float3 pos : POSITION0;
	[[vk::location(1)]]float4 color : COLOR0;
	uint vertex_id : SV_VertexID;
};

// outputs 
struct VSOutput
{
	float4 position : SV_POSITION;
	float2 texCoord: TEXCOORD0;
	float4 vertexColor : COLOR0;
};

// arguments
cbuffer Mvp : register(b0)
{
	float4x4 modelMatrix;
	float4x4 viewMatrix;
	float4x4 projectionMatrix;
};

#if 0 // Example push constants
	struct PushConstants {
		float4 color;
		uint frame_counter;
	};
	#if defined(_DXC)
	[[vk::push_constant]] PushConstants pushConstants;
	#else
	[[vk::push_constant]] ConstantBuffer<PushConstants> pushConstants;
	#endif
#endif

#if 0 // Example specialization constant
	[[vk::constant_id(0)]] const float s_const = 0.5f;
#endif 

VSOutput main(VSInput input) {

	VSOutput output = (VSOutput)0;

	output.texCoord    = float2((input.vertex_id << 1) & 2, input.vertex_id & 2);
	output.vertexColor = input.color;

	output.position = mul(projectionMatrix,mul(viewMatrix,mul(modelMatrix,float4(input.pos,1))));

	return output;
}

// Inputs // Note: no inputs via locations!
struct VSInput
{
	uint vertex_id : SV_VertexID;
};

// Outputs
struct VSOutput {
	float4 position  : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

VSOutput main(VSInput input) {

	VSOutput output = VSOutput(0);
	
	output.texCoord = float2((input.vertex_id << 1) & 2, input.vertex_id & 2);
	output.position  = float4(output.texCoord * 2.0f + -1.0f, 0.0f, 1.0f);

	return output;
}

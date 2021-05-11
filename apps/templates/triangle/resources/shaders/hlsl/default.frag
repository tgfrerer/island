// vertex shader stage output
struct VSOutput
{
	float4 Pos : SV_POSITION;
	float2 texCoord: TEXCOORD0;
	float4 vertexColor : COLOR0;
};


float4 main(VSOutput inData) : SV_TARGET {
	float4 outColor = inData.vertexColor;
	outColor *= 1.0;
	return outColor;
}
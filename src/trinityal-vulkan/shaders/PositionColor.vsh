// Copyright © 2026 Ithax contributors.

struct VSOut
{
	float4 position : SV_Position;
	float3 color : COLOR0;
};

VSOut main( float3 position : POSITION, float3 color : COLOR0 )
{
	VSOut output;
	output.position = float4( position, 1.0f );
	output.color = color;
	return output;
}

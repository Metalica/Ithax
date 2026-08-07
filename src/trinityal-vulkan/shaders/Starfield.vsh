// Copyright © 2026 Ithax contributors.

cbuffer SceneConstants : register(b0)
{
	float4x4 mvp;
};

struct VSOut
{
	float4 position : SV_Position;
	[[vk::builtin("PointSize")]] float pointSize : PSIZE;
};

VSOut main( float3 position : POSITION )
{
	VSOut output;
	output.position = mul( mvp, float4( position, 1.0f ) );
	output.pointSize = 2.0f;
	return output;
}

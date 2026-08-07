// Copyright © 2026 Ithax contributors.

cbuffer SceneConstants : register(b0)
{
	float4x4 mvp;
};

struct VSOut
{
	float4 position : SV_Position;
	float3 color : COLOR0;
	float2 uv : TEXCOORD0;
};

VSOut main( float3 position : POSITION, float4 color : COLOR0, float2 uv : TEXCOORD0 )
{
	VSOut output;
	output.position = mul( mvp, float4( position, 1.0f ) );
	output.color = color.rgb;
	output.uv = uv;
	return output;
}

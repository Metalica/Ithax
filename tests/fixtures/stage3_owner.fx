float4 Stage3Vertex(): SV_Position
{
    return float4(0.0, 0.0, 0.0, 1.0);
}

float4 Stage3Pixel(): SV_Target
{
    return float4(0.25, 0.5, 0.75, 1.0);
}

technique stage3_owner
{
    pass stage3_pass
    {
        vertexshader = compile vs_3_0 Stage3Vertex();
        pixelshader = compile ps_3_0 Stage3Pixel();
    }
}

cbuffer MatrixBuffer : register(b0)
{
    row_major matrix world_view_projection;
};

struct VS_INPUT
{
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
    float3 normal : NORMAL;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

PS_INPUT VSMain(VS_INPUT input)
{
    PS_INPUT output;
    output.position = mul(float4(input.position, 1.0f), world_view_projection);
    output.uv = input.uv;
    return output;
}

Texture2D desktop_texture : register(t0);
SamplerState wrap_sampler : register(s0);

float4 PSMain(PS_INPUT input) : SV_TARGET
{
    return desktop_texture.Sample(wrap_sampler, input.uv);
}

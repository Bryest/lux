cbuffer SceneConstants : register(b0)
{
    float4x4 mvp;
};

struct VSIn
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD;
};

struct PSIn
{
    float4 position : SV_POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD;
};

PSIn VSMain(VSIn input)
{
    PSIn output;
    output.position = mul(mvp, float4(input.position, 1.0f));
    output.normal   = input.normal;
    output.uv       = input.uv;
    return output;
}

float4 PSMain(PSIn input) : SV_TARGET
{
    // Visualize normals as color — Phase 4a (textures come next)
    return float4(input.normal * 0.5f + 0.5f, 1.0f);
}

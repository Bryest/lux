cbuffer SceneConstants : register(b0)
{
    float4x4 mvp;
};

struct VSIn
{
    float3 position : POSITION;
    float4 color    : COLOR;
};

struct PSIn
{
    float4 position : SV_POSITION;
    float4 color    : COLOR;
};

PSIn VSMain(VSIn input)
{
    PSIn output;
    output.position = mul(mvp, float4(input.position, 1.0f));
    output.color    = input.color;
    return output;
}

float4 PSMain(PSIn input) : SV_TARGET
{
    return input.color;
}

cbuffer ShadowConstants : register(b0)
{
    float4x4 lightVP;
    float    _pad[48];
};

float4 VSMain(float3 pos : POSITION) : SV_POSITION
{
    return mul(lightVP, float4(pos, 1.0f));
}

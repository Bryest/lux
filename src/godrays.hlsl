// screen-space god rays — layered on top of volumetric lighting

cbuffer GodRayConstants : register(b0)
{
    float2 sunPos;   // sun screen position in UV [0,1]
    float  exposure;
    float  decay;
    float  density;
    float  weight;
    float2 _pad;
};

Texture2D    g_tex     : register(t0);
SamplerState g_sampler : register(s0);

struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD; };

VSOut VSMain(uint id : SV_VertexID)
{
    float2 uv = float2((id << 1) & 2, id & 2);
    VSOut o;
    o.pos = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    o.uv  = uv;
    return o;
}

// Pass 1: depth → sky/geometry occlusion mask
float4 PSOcclusion(VSOut i) : SV_TARGET
{
    float  d    = g_tex.SampleLevel(g_sampler, i.uv, 0).r;
    float  sky  = (d > 0.9999f) ? 1.0f : 0.0f;
    float2 diff = (i.uv - sunPos) * float2(16.0f / 9.0f, 1.0f);
    float  disc = saturate(1.0f - length(diff) * 18.0f) * 3.0f;
    float  v    = saturate(sky + disc);
    return float4(v, v, v, 1.0f);
}

// Pass 2: radial blur toward sun → additive god rays
static const int kSamples = 64;

float4 PSComposite(VSOut i) : SV_TARGET
{
    float2 uv    = i.uv;
    float2 delta = (uv - sunPos) * (density / (float)kSamples);
    float  illum = 1.0f;
    float  acc   = 0.0f;

    [unroll(64)]
    for (int s = 0; s < kSamples; ++s)
    {
        uv   -= delta;
        acc  += g_tex.SampleLevel(g_sampler, uv, 0).r * illum * weight;
        illum *= decay;
    }
    float v = acc * exposure;
    return float4(v, v * 0.88f, v * 0.55f, 1.0f); // warm sun tint
}

// ray-marched volumetric lighting — works in all camera directions

cbuffer VolumetricConstants : register(b0)
{
    float4x4 invViewProj;
    float4x4 lightVP;
    float4   cameraPos;
    float4   lightDir;
    float4   lightColor;
    float    scatterCoeff;
    float    maxDist;
    float2   _pad0;
    float4   _pad1[4];
};

Texture2D    g_depth     : register(t0);
Texture2D    g_shadowMap : register(t1);
SamplerState g_sampler   : register(s0); // point clamp

struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD; };

VSOut VSMain(uint id : SV_VertexID)
{
    float2 uv = float2((id << 1) & 2, id & 2);
    VSOut o;
    o.pos = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    o.uv  = uv;
    return o;
}

// Henyey-Greenstein phase — forward-scattering sun halo (g=0.76)
float MiePhase(float cosTheta)
{
    const float g  = 0.76f;
    const float g2 = g * g;
    float d = 1.0f + g2 - 2.0f * g * cosTheta;
    return (1.0f - g2) / (4.0f * 3.14159265f * pow(max(d, 0.0001f), 1.5f));
}

static const int kSteps = 48;

float4 PSMain(VSOut i) : SV_TARGET
{
    float  d  = g_depth.SampleLevel(g_sampler, i.uv, 0).r;

    // Reconstruct world-space end point from UV + depth
    float2 ndc = i.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
    float4 wp4 = mul(invViewProj, float4(ndc, d, 1.0f));
    float3 wp  = wp4.xyz / wp4.w;

    float3 cam    = cameraPos.xyz;
    float3 toWp   = wp - cam;
    float  dist   = length(toWp);
    float3 rayDir = toWp / max(dist, 0.0001f);
    float  rayLen = min(dist, maxDist);

    // Per-pixel jitter to break up banding
    float  jitter = frac(sin(dot(i.uv, float2(127.1f, 311.7f))) * 43758.5453f);
    float  stepSz = rayLen / (float)kSteps;
    float3 pos    = cam + rayDir * jitter * stepSz;

    float  cosTheta = dot(rayDir, normalize(lightDir.xyz));
    float  phase    = MiePhase(cosTheta);
    float3 acc      = 0.0f;

    for (int s = 0; s < kSteps; ++s)
    {
        pos += rayDir * stepSz;

        float4 lsPos = mul(lightVP, float4(pos, 1.0f));
        float3 proj  = lsPos.xyz / lsPos.w;

        float lit;
        if (any(abs(proj.xy) > 1.0f) || proj.z < 0.0f || proj.z > 1.0f) {
            lit = 1.0f; // outside shadow frustum — assume fully lit
        } else {
            float2 suv = float2(proj.x * 0.5f + 0.5f, -proj.y * 0.5f + 0.5f);
            float  occ = g_shadowMap.SampleLevel(g_sampler, suv, 0).r;
            lit = (proj.z - 0.005f < occ) ? 1.0f : 0.0f;
        }

        acc += lit * phase * stepSz;
    }

    float3 color = acc * lightColor.xyz * scatterCoeff;
    return float4(color, 1.0f);
}

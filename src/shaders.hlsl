cbuffer SceneConstants : register(b0)
{
    float4x4 mvp;
    float4x4 model;
    float4x4 lightVP;    // light view-projection for shadow mapping
    float4   lightDir;   // xyz = direction toward light (world space)
    float4   cameraPos;  // xyz = camera world position
    float4   lightColor; // xyz = color * intensity
};

Texture2D    g_albedo           : register(t0);
Texture2D    g_normal           : register(t1);
Texture2D    g_metallicRoughness: register(t2); // G=roughness, B=metallic
Texture2D    g_shadowMap        : register(t3);
SamplerState g_sampler          : register(s0); // linear wrap
SamplerState g_shadowSampler    : register(s1); // point clamp

static const float PI = 3.14159265f;

struct VSIn
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD;
    float4 tangent  : TANGENT;
};

struct PSIn
{
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal   : NORMAL;
    float4 tangent  : TANGENT;
    float2 uv       : TEXCOORD1;
};

PSIn VSMain(VSIn input)
{
    PSIn output;
    float4 wp       = mul(model, float4(input.position, 1.0f));
    output.position = mul(mvp, float4(input.position, 1.0f));
    output.worldPos = wp.xyz;
    output.normal   = normalize(mul((float3x3)model, input.normal));
    output.tangent  = float4(normalize(mul((float3x3)model, input.tangent.xyz)), input.tangent.w);
    output.uv       = input.uv;
    return output;
}

// GGX Normal Distribution Function — models microfacet roughness
float D_GGX(float NdotH, float roughness)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / max(PI * d * d, 0.0001f);
}

// Smith-Schlick Geometry Function — models self-shadowing of microfacets
float G_SchlickGGX(float NdotX, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;
    return NdotX / max(NdotX * (1.0f - k) + k, 0.0001f);
}
float G_Smith(float NdotV, float NdotL, float roughness)
{
    return G_SchlickGGX(NdotV, roughness) * G_SchlickGGX(NdotL, roughness);
}

// Fresnel-Schlick — models how reflectivity increases at glancing angles
float3 F_Schlick(float HdotV, float3 F0)
{
    return F0 + (1.0f - F0) * pow(saturate(1.0f - HdotV), 5.0f);
}

// PCF shadow — 3x3 kernel, returns 1=lit 0=shadow
float ShadowFactor(float3 worldPos)
{
    float4 lsPos = mul(lightVP, float4(worldPos, 1.0f));
    float3 proj  = lsPos.xyz / lsPos.w;

    // Outside shadow frustum → fully lit
    if (any(abs(proj.xy) > 1.0f) || proj.z < 0.0f || proj.z > 1.0f) return 1.0f;

    float2 uv = float2(proj.x * 0.5f + 0.5f, -proj.y * 0.5f + 0.5f);
    float  depth = proj.z - 0.002f; // bias to prevent shadow acne

    float shadow = 0.0f;
    float texel  = 1.0f / 4096.0f;
    [unroll] for (int y = -1; y <= 1; ++y)
    [unroll] for (int x = -1; x <= 1; ++x)
        shadow += (depth > g_shadowMap.SampleLevel(g_shadowSampler, uv + float2(x,y)*texel, 0).r) ? 0.0f : 1.0f;
    return shadow / 9.0f;
}

// ACES filmic tone mapping — compresses HDR range to [0,1]
float3 ACESFilm(float3 x)
{
    return saturate((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f));
}

float4 PSMain(PSIn input) : SV_TARGET
{
    // --- Normal mapping ---
    float3 N = normalize(input.normal);
    float3 T = normalize(input.tangent.xyz - dot(input.tangent.xyz, N) * N);
    float3 B = cross(N, T) * input.tangent.w;
    float3 ns = g_normal.Sample(g_sampler, input.uv).rgb * 2.0f - 1.0f;
    N = normalize(ns.x * T + ns.y * B + ns.z * N);

    // --- Material ---
    float4 albedoSample = g_albedo.Sample(g_sampler, input.uv);
    clip(albedoSample.a - 0.5f); // alpha cutout — discards transparent pixels (leaves, chains)
    // Albedo textures are sRGB — convert to linear for physically correct math
    float3 albedo    = pow(albedoSample.rgb, 2.2f);
    float2 mr        = g_metallicRoughness.Sample(g_sampler, input.uv).gb;
    float  roughness = max(mr.x, 0.05f); // clamp to avoid division-by-zero at roughness=0
    float  metallic  = mr.y;

    // --- Cook-Torrance BRDF ---
    float3 L    = normalize(lightDir.xyz);
    float3 V    = normalize(cameraPos.xyz - input.worldPos);
    float3 H    = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0f);
    float NdotV = max(dot(N, V), 0.0f);
    float NdotH = max(dot(N, H), 0.0f);
    float HdotV = max(dot(H, V), 0.0f);

    // F0: base reflectance — 0.04 for dielectrics, albedo for metals
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);

    float  D = D_GGX(NdotH, roughness);
    float  G = G_Smith(NdotV, NdotL, roughness);
    float3 F = F_Schlick(HdotV, F0);

    float3 specular = D * G * F / max(4.0f * NdotV * NdotL, 0.0001f);
    float3 kD       = (1.0f - F) * (1.0f - metallic); // metals absorb all diffuse
    float3 diffuse  = kD * albedo / PI;

    float  shadow = ShadowFactor(input.worldPos);
    float3 color  = (diffuse + specular) * lightColor.xyz * NdotL * shadow;

    // Ambient is not shadowed
    color += albedo * 0.03f;

    // --- Tone mapping + gamma correction ---
    color = ACESFilm(color);
    color = pow(max(color, 0.0f), 1.0f / 2.2f); // linear → sRGB

    return float4(color, 1.0f);
}

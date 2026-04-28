cbuffer SceneConstants : register(b0)
{
    float4x4 mvp;
    float4x4 model;
    float4   lightDir;  // xyz = direction toward light (world space)
    float4   cameraPos; // xyz = camera world position
};

Texture2D    g_albedo  : register(t0);
Texture2D    g_normal  : register(t1);
SamplerState g_sampler : register(s0);

struct VSIn
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD;
    float4 tangent  : TANGENT; // xyz = tangent, w = bitangent sign
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
    float4 wp        = mul(model, float4(input.position, 1.0f));
    output.position  = mul(mvp, float4(input.position, 1.0f));
    output.worldPos  = wp.xyz;
    output.normal    = normalize(mul((float3x3)model, input.normal));
    output.tangent   = float4(normalize(mul((float3x3)model, input.tangent.xyz)), input.tangent.w);
    output.uv        = input.uv;
    return output;
}

float4 PSMain(PSIn input) : SV_TARGET
{
    // Build TBN matrix to transform normal map from tangent → world space
    float3 N = normalize(input.normal);
    float3 T = normalize(input.tangent.xyz - dot(input.tangent.xyz, N) * N); // re-orthogonalize
    float3 B = cross(N, T) * input.tangent.w;

    float3 normalSample = g_normal.Sample(g_sampler, input.uv).rgb;
    normalSample = normalSample * 2.0f - 1.0f; // [0,1] → [-1,1]
    N = normalize(normalSample.x * T + normalSample.y * B + normalSample.z * N);

    float3 L = normalize(lightDir.xyz);
    float3 V = normalize(cameraPos.xyz - input.worldPos);
    float3 R = reflect(-L, N);

    float3 baseColor = g_albedo.Sample(g_sampler, input.uv).rgb;

    float  ambient  = 0.08f;
    float  diffuse  = max(dot(N, L), 0.0f);
    float  specular = pow(max(dot(R, V), 0.0f), 48.0f) * 0.6f;

    float3 color = baseColor * (ambient + diffuse) + float3(1.0f, 1.0f, 1.0f) * specular;
    return float4(color, 1.0f);
}

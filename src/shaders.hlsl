cbuffer SceneConstants : register(b0)
{
    float4x4 mvp;
    float4x4 model;
    float4   lightDir;  // xyz = direction toward light (world space)
    float4   cameraPos; // xyz = camera world position
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
    float3 worldPos : TEXCOORD0;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD1;
};

PSIn VSMain(VSIn input)
{
    PSIn output;
    float4 wp       = mul(model, float4(input.position, 1.0f));
    output.position = mul(mvp, float4(input.position, 1.0f));
    output.worldPos = wp.xyz;
    output.normal   = normalize(mul((float3x3)model, input.normal));
    output.uv       = input.uv;
    return output;
}

float4 PSMain(PSIn input) : SV_TARGET
{
    float3 N = normalize(input.normal);
    float3 L = normalize(lightDir.xyz);
    float3 V = normalize(cameraPos.xyz - input.worldPos);
    float3 R = reflect(-L, N);

    float3 baseColor = float3(0.85f, 0.55f, 0.35f); // warm clay

    // Phong lighting model: ambient + diffuse + specular
    // ambient  — constant base light so shadows aren't pitch black
    // diffuse  — dot(N,L): bright where surface faces the light, dark where it faces away
    // specular — pow(dot(R,V), 48): mirror-like highlight; 48 = shininess (higher = tighter spot)
    float  ambient  = 0.08f;
    float  diffuse  = max(dot(N, L), 0.0f);
    float  specular = pow(max(dot(R, V), 0.0f), 48.0f) * 0.6f;

    float3 color = baseColor * (ambient + diffuse) + float3(1.0f, 1.0f, 1.0f) * specular;
    return float4(color, 1.0f);
}

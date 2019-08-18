#include "../CPUAndGPUCommon.h"

#define GRootSignature \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
    "CBV(b0), " \
    "CBV(b1, visibility = SHADER_VISIBILITY_PIXEL)"

ConstantBuffer<FPerDrawConstantData> GPerDrawCB : register(b0);
ConstantBuffer<FPerFrameConstantData> GPerFrameCB : register(b1);

[RootSignature(GRootSignature)]
void MainVS(
    in float3 InPosition : _Position,
    in float3 InNormal : _Normal,
    out float4 OutPosition : SV_Position,
    out float3 OutPositionWS : _Position,
    out float3 OutNormalWS : _Normal)
{
    OutPosition = mul(float4(InPosition, 1.0f), GPerDrawCB.ObjectToClip);
    OutPositionWS = mul(float4(InPosition, 1.0f), GPerDrawCB.ObjectToWorld);
    OutNormalWS = mul(InNormal, (float3x3)GPerDrawCB.ObjectToWorld);
}

#define GPI 3.14159265359f

// Trowbridge-Reitz GGX normal distribution function.
float DistributionGGX(float3 N, float3 H, float Alpha)
{
    float Alpha2 = Alpha * Alpha;
    float NoH = saturate(dot(N, H));
    float K = NoH * NoH * (Alpha2 - 1.0f) + 1.0f;
    return Alpha2 / (GPI * K * K);
}

float GeometrySchlickGGX(float X, float K)
{
    return X / (X * (1.0f - K) + K);
}

float GeometrySmith(float3 N, float3 V, float3 L, float K)
{
    float NoV = saturate(dot(N, V));
    float NoL = saturate(dot(N, L));
    return GeometrySchlickGGX(NoV, K) * GeometrySchlickGGX(NoL, K);
}

float3 FresnelSchlick(float HoV, float3 F0)
{
    return F0 + (float3(1.0f, 1.0f, 1.0f) - F0) * pow(1.0f - HoV, 5.0f);
}

[RootSignature(GRootSignature)]
void MainPS(
    in float4 InPosition : SV_Position,
    in float3 InPositionWS : _Position,
    in float3 InNormalWS : _Normal,
    out float4 OutColor : SV_Target0)
{
    float3 V = normalize(GPerFrameCB.ViewerPosition.xyz - InPositionWS);
    float3 N = normalize(InNormalWS);

    float3 Albedo = GPerDrawCB.Albedo;
    float Roughness = GPerDrawCB.Roughness;
    float Metallic = GPerDrawCB.Metallic;
    float AO = GPerDrawCB.AO;

    float Alpha = Roughness * Roughness;
    float K = Alpha + 1.0f;
    K = (K * K) / 8.0f;
    float3 F0 = float3(0.04f, 0.04f, 0.04f);
    F0 = lerp(F0, Albedo, Metallic);

    float3 Lo = float3(0.0f, 0.0f, 0.0f);
    for (int LightIdx = 0; LightIdx < 4; ++LightIdx)
    {
        float3 LightVector = GPerFrameCB.LightPositions[LightIdx].xyz - InPositionWS;

        float3 L = normalize(LightVector);
        float3 H = normalize(L + V);

        float Distance = length(LightVector);
        float Attenuation = 1.0f / (Distance * Distance);
        float3 Radiance = GPerFrameCB.LightColors[LightIdx].rgb * Attenuation;

        float3 F = FresnelSchlick(saturate(dot(H, V)), F0);

        float NDF = DistributionGGX(N, H, Alpha);
        float G = GeometrySmith(N, V, L, K);

        float3 Numerator = NDF * G * F;
        float Denominator = 4.0f * saturate(dot(N, V)) * saturate(dot(N, L));
        float3 Specular = Numerator / max(Denominator, 0.001f);

        float3 KS = F;
        float3 KD = float3(1.0f, 1.0f, 1.0f) - KS;
        KD *= 1.0f - Metallic;

        float NoL = saturate(dot(N, L));
        Lo += (KD * Albedo / GPI + Specular) * Radiance * NoL;
    }

    float3 Ambient = float3(0.03f, 0.03f, 0.03f) * Albedo * AO;
    float3 Color = Ambient + Lo;

    float Gamma = 1.0f / 2.2f;
    Color = Color / (Color + float3(1.0f, 1.0f, 1.0f));
    Color = pow(Color, float3(Gamma, Gamma, Gamma));

    OutColor = float4(Color, 1.0f);
}

#include "../CPUAndGPUCommon.h"
#include "Common.hlsli"

#define GRootSignature \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
    "CBV(b0), " \
	"DescriptorTable(CBV(b1), SRV(t0), SRV(t1), SRV(t2), visibility = SHADER_VISIBILITY_PIXEL), " \
	"StaticSampler(" \
		"s0, " \
		"filter = FILTER_MIN_MAG_MIP_LINEAR, " \
		"visibility = SHADER_VISIBILITY_PIXEL, " \
		"addressU = TEXTURE_ADDRESS_BORDER, " \
		"addressV = TEXTURE_ADDRESS_BORDER, " \
		"addressW = TEXTURE_ADDRESS_BORDER)"

ConstantBuffer<FPerDrawConstantData> GPerDrawCB : register(b0);
ConstantBuffer<FPerFrameConstantData> GPerFrameCB : register(b1);
TextureCube GIrradianceMap : register(t0);
TextureCube GPrefilteredEnvMap : register(t1);
Texture2D GBRDFIntegrationMap : register(t2);
SamplerState GSampler : register(s0);

// Trowbridge-Reitz GGX normal distribution function.
float DistributionGGX(float3 N, float3 H, float Roughness)
{
	float Alpha = Roughness * Roughness;
	float Alpha2 = Alpha * Alpha;
	float NoH = dot(N, H);
	float NoH2 = NoH * NoH;
	float K = NoH2 * Alpha2 + (1.0f - NoH2);
	return Alpha2 / (PI * K * K);
}

float3 FresnelSchlick(float CosTheta, float3 F0)
{
	return F0 + (1.0f - F0) * pow(1.0f - CosTheta, 5.0f);
}

float3 FresnelSchlickRoughness(float CosTheta, float3 F0, float Roughness)
{
	return F0 + (max(1.0f - Roughness, F0) - F0) * pow(1.0f - CosTheta, 5.0f);
}

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

[RootSignature(GRootSignature)]
void MainPS(
	in float4 InPosition : SV_Position,
	in float3 InPositionWS : _Position,
	in float3 InNormalWS : _Normal,
	out float4 OutColor : SV_Target0)
{
	float3 V = normalize(GPerFrameCB.ViewerPosition.xyz - InPositionWS);
	float3 N = normalize(InNormalWS);
	float NoV = saturate(dot(N, V));

	float3 Albedo = GPerDrawCB.Albedo;
	float Roughness = GPerDrawCB.Roughness;
	float Metallic = GPerDrawCB.Metallic;
	float AO = GPerDrawCB.AO;

	float3 F0 = float3(0.04f, 0.04f, 0.04f);
	F0 = lerp(F0, Albedo, Metallic);

	float3 Lo = 0.0f;
	for (int LightIdx = 0; LightIdx < 4; ++LightIdx)
	{
		float3 LightVector = GPerFrameCB.LightPositions[LightIdx].xyz - InPositionWS;

		float3 L = normalize(LightVector);
		float3 H = normalize(L + V);
		float NoL = saturate(dot(N, L));
		float HoV = saturate(dot(H, V));

		float Attenuation = 1.0f / dot(LightVector, LightVector);
		float3 Radiance = GPerFrameCB.LightColors[LightIdx].rgb * Attenuation;

		float3 F = FresnelSchlick(HoV, F0);

		float NDF = DistributionGGX(N, H, Roughness);
		float G = GeometrySmith(NoL, NoV, (Roughness + 1.0f) * 0.5f);

		float3 Specular = (NDF * G * F) / max(4.0f * NoV * NoL, 0.001f);

		float3 KS = F;
		float3 KD = 1.0f - KS;
		KD *= 1.0f - Metallic;

		Lo += (KD * (Albedo / PI) + Specular) * Radiance * NoL;
	}

	float3 R = reflect(-V, N);

	float3 F = FresnelSchlickRoughness(NoV, F0, Roughness);

	float3 KD = 1.0f - F;
	KD *= 1.0f - Metallic;

	float3 Irradiance = GIrradianceMap.SampleLevel(GSampler, N, 0.0f).rgb;
	float3 Diffuse = Irradiance * Albedo;
	float3 PrefilteredColor = GPrefilteredEnvMap.SampleLevel(GSampler, R, Roughness * 5.0f).rgb;

	float2 EnvBRDF = GBRDFIntegrationMap.SampleLevel(GSampler, float2(min(NoV, 0.999f), Roughness), 0.0f).rg;

	float3 Specular = PrefilteredColor * (F * EnvBRDF.x + EnvBRDF.y);

	float3 Ambient = (KD * Diffuse + Specular) * AO;

	float3 Color = Ambient + Lo;
	Color = Color / (Color + 1.0f);
	Color = pow(Color, 1.0f / 2.2f);

	OutColor = float4(Color, 1.0f);
}

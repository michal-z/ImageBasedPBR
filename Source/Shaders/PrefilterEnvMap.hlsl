#include "../CPUAndGPUCommon.h"
#include "Common.hlsli"

#define GRootSignature \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
    "CBV(b0), " \
    "DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_PIXEL), " \
    "StaticSampler(" \
		"s0, " \
		"filter = FILTER_MIN_MAG_LINEAR_MIP_POINT, " \
		"visibility = SHADER_VISIBILITY_PIXEL, " \
		"addressU = TEXTURE_ADDRESS_CLAMP, " \
		"addressV = TEXTURE_ADDRESS_CLAMP, " \
		"addressW = TEXTURE_ADDRESS_CLAMP)"

ConstantBuffer<FPerDrawConstantData> GPerDrawCB : register(b0);
TextureCube GEnvMap : register(t0);
SamplerState GSampler : register(s0);


[RootSignature(GRootSignature)]
void MainVS(
	in float3 InPosition : _Position,
	in float3 InNormal : _Normal,
	out float4 OutPosition : SV_Position,
	out float3 OutPositionOS : _Position)
{
	OutPosition = mul(float4(InPosition, 1.0f), GPerDrawCB.ObjectToClip);
	OutPositionOS = InPosition;
}

[RootSignature(GRootSignature)]
void MainPS(
	in float4 InPosition : SV_Position,
	in float3 InPositionOS : _Position,
	out float4 OutColor : SV_Target0)
{
	float Roughness = GPerDrawCB.Roughness;
	float3 N = normalize(InPositionOS);
	float3 R = N;
	float3 V = R;

	float3 PrefilteredColor = 0.0f;
	float TotalWeight = 0.0f;
	const uint NumSamples = 4 * 1024;

	for (uint SampleIdx = 0; SampleIdx < NumSamples; ++SampleIdx)
	{
		float2 Xi = Hammersley(SampleIdx, NumSamples);
		float3 H = ImportanceSampleGGX(Xi, Roughness, N);
		float3 L = normalize(2.0f * dot(V, H) * H - V);
		float NoL = saturate(dot(N, L));
		if (NoL > 0.0f)
		{
			PrefilteredColor += GEnvMap.SampleLevel(GSampler, L, 0).rgb * NoL;
			TotalWeight += NoL;
		}
	}

	OutColor = float4(PrefilteredColor / TotalWeight, 1.0f);
}

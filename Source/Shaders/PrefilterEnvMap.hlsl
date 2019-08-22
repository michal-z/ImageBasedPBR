#include "../CPUAndGPUCommon.h"

#define GRootSignature \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
    "CBV(b0), " \
    "DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_PIXEL), " \
    "StaticSampler(" \
		"s0, " \
		"filter = FILTER_MIN_MAG_LINEAR_MIP_POINT, " \
		"visibility = SHADER_VISIBILITY_PIXEL, " \
		"addressU = TEXTURE_ADDRESS_BORDER, " \
		"addressV = TEXTURE_ADDRESS_BORDER, " \
		"addressW = TEXTURE_ADDRESS_BORDER)"

ConstantBuffer<FPerDrawConstantData> GPerDrawCB : register(b0);

TextureCube GEnvMap : register(t0);
SamplerState GSampler : register(s0);

#define PI 3.14159265359f

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

float RadicalInverse_VdC(uint Bits)
{
	Bits = (Bits << 16u) | (Bits >> 16u);
	Bits = ((Bits & 0x55555555u) << 1u) | ((Bits & 0xAAAAAAAAu) >> 1u);
	Bits = ((Bits & 0x33333333u) << 2u) | ((Bits & 0xCCCCCCCCu) >> 2u);
	Bits = ((Bits & 0x0F0F0F0Fu) << 4u) | ((Bits & 0xF0F0F0F0u) >> 4u);
	Bits = ((Bits & 0x00FF00FFu) << 8u) | ((Bits & 0xFF00FF00u) >> 8u);
	return (float)Bits * 2.3283064365386963e-10; // / 0x100000000
}

float2 Hammersley(uint Idx, uint N)
{
	return float2(Idx / (float)N, RadicalInverse_VdC(Idx));
}

float3 ImportanceSampleGGX(float2 Xi, float Roughness, float3 N)
{
	float Alpha = Roughness * Roughness;
	float Phi = 2.0f * PI * Xi.x;
	float CosTheta = sqrt((1.0f - Xi.y) / (1.0f + (Alpha * Alpha - 1.0f) * Xi.y));
	float SinTheta = sqrt(1.0f - CosTheta * CosTheta);

	float3 H;
	H.x = SinTheta * cos(Phi);
	H.y = SinTheta * sin(Phi);
	H.z = CosTheta;

	float3 UpVector = abs(N.y) < 0.999f ? float3(0.0f, 1.0f, 0.0f) : float3(0.0f, 0.0f, 1.0f);
	float3 TangentX = normalize(cross(UpVector, N));
	float3 TangentY = cross(N, TangentX);

	// Tangent to world space.
	return TangentX * H.x + TangentY * H.y + N * H.z;
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
	const uint NumSamples = 4096u;

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

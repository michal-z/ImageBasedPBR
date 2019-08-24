#include "../CPUAndGPUCommon.h"
#include "Common.hlsli"

#define GRootSignature \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
    "CBV(b0, visibility = SHADER_VISIBILITY_VERTEX), " \
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
	float3 N = normalize(InPositionOS);

	// This is Right-Handed coordinate system and works for upper-left UV coordinate systems.
	float3 UpVector = abs(N.y) < 0.999f ? float3(0.0f, 1.0f, 0.0f) : float3(0.0f, 0.0f, 1.0f);
	float3 TangentX = normalize(cross(UpVector, N));
	float3 TangentY = cross(N, TangentX);

	int NumSamples = 0;
	float3 Irradiance = 0.0f;

	for (float Phi = 0.0f; Phi < (2.0f * PI); Phi += 0.025f)
	{
		for (float Theta = 0.0f; Theta < (0.5f * PI); Theta += 0.025f)
		{
			// Point on a hemisphere.
			float3 H = float3(sin(Theta) * cos(Phi), sin(Theta) * sin(Phi), cos(Theta));

			// Transform from tangent space to world space.
			float3 SampleVector = TangentX * H.x + TangentY * H.y + N * H.z;

			Irradiance += GEnvMap.SampleLevel(GSampler, SampleVector, 0).rgb * cos(Theta) * sin(Theta);

			NumSamples++;
		}
	}

	Irradiance = PI * Irradiance * (1.0f / NumSamples);

	OutColor = float4(Irradiance, 1.0f);
}

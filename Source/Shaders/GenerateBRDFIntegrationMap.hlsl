#include "../CPUAndGPUCommon.h"
#include "Common.hlsli"

#define GRootSignature \
    "RootFlags(0), " \
	"DescriptorTable(UAV(u0))"

RWTexture2D<float4> GBRDFIntegrationMap : register(u0);


float2 IntegrateBRDF(float Roughness, float NoV)
{
	float3 V;
	V.x = 0.0f;
	V.y = NoV; // cos
	V.z = sqrt(1.0f - NoV * NoV); // sin

	float3 N = float3(0.0f, 1.0f, 0.0f);

	float A = 0.0f;
	float B = 0.0f;
	const uint NumSamples = 4 * 1024;

	for (uint SampleIdx = 0; SampleIdx < NumSamples; ++SampleIdx)
	{
		float2 Xi = Hammersley(SampleIdx, NumSamples);
		float3 H = ImportanceSampleGGX(Xi, Roughness, N);
		float3 L = normalize(2.0f * dot(V, H) * H - V);

		float NoL = saturate(L.y);
		float NoH = saturate(H.y);
		float VoH = saturate(dot(V, H));

		if (NoL > 0.0f)
		{
			float G = GeometrySmith(NoL, NoV, Roughness);
			float G_Vis = G * VoH / (NoH * NoV);
			float Fc = pow(1.0f - VoH, 5.0f);
			A += (1.0f - Fc) * G_Vis;
			B += Fc * G_Vis;
		}
	}

	return float2(A, B) / NumSamples;
}

[RootSignature(GRootSignature)]
[numthreads(8, 8, 1)]
void MainCS(uint3 ThreadID : SV_DispatchThreadID)
{
	float Width, Height;
	GBRDFIntegrationMap.GetDimensions(Width, Height);

	float Roughness = (ThreadID.y + 1) / Height;
	float NoV = (ThreadID.x + 1) / Width;

	float2 Result = IntegrateBRDF(Roughness, NoV);

	GBRDFIntegrationMap[ThreadID.xy] = float4(Result, 0.0f, 1.0f);
}

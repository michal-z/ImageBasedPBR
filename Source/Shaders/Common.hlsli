#pragma once

#define PI 3.14159265359f

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

float GeometrySchlickGGX(float CosTheta, float Roughness)
{
	float K = (Roughness * Roughness) * 0.5f;
	return CosTheta / (CosTheta * (1.0f - K) + K);
}

float GeometrySmith(float NoL, float NoV, float Roughness)
{
	return GeometrySchlickGGX(NoV, Roughness) * GeometrySchlickGGX(NoL, Roughness);
}

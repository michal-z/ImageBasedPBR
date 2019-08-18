#pragma once

#ifdef __cplusplus
#include "DirectXMath/DirectXMath.h"
typedef XMFLOAT4X4 float4x4;
typedef XMFLOAT4X3 float4x3;
typedef XMFLOAT2 float2;
typedef XMFLOAT3 float3;
typedef XMFLOAT4 float4;
#endif

struct FPerDrawConstantData
{
	float4x4 ObjectToClip;
	float4x3 ObjectToWorld;
	float3 Albedo;
	float Metallic;
	float Roughness;
	float AO;
	float Padding[30];
};
#ifdef __cplusplus
static_assert(sizeof(FPerDrawConstantData) == 256, "sizeof(FPerDrawConstantData) != 256");
#endif

struct FPerFrameConstantData
{
	float4 LightPositions[4];
	float4 LightColors[4];
	float4 ViewerPosition;
};

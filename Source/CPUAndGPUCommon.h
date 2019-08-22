#pragma once

#ifdef __cplusplus
#include "DirectXMath/DirectXMath.h"
typedef XMFLOAT4X4 float4x4;
typedef XMFLOAT4X3 float4x3;
typedef XMFLOAT2 float2;
typedef XMFLOAT3 float3;
typedef XMFLOAT4 float4;
#endif

#ifdef __cplusplus
#define SALIGN alignas(256)
#else
#define SALIGN
#endif

struct SALIGN FPerDrawConstantData
{
	float4x4 ObjectToClip;
	float4x3 ObjectToWorld;
	float3 Albedo;
	float Metallic;
	float Roughness;
	float AO;
};

struct SALIGN FPerFrameConstantData
{
	float4 LightPositions[4];
	float4 LightColors[4];
	float4 ViewerPosition;
};

#ifdef __cplusplus
#undef SALIGN
#endif

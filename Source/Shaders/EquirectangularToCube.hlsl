#include "../CPUAndGPUCommon.h"

#define GRootSignature \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
    "CBV(b0, visibility = SHADER_VISIBILITY_VERTEX), " \
    "DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_PIXEL), " \
    "StaticSampler(s0, " \
    "filter = FILTER_MIN_MAG_LINEAR_MIP_POINT, " \
    "visibility = SHADER_VISIBILITY_PIXEL, " \
    "addressU = TEXTURE_ADDRESS_BORDER, " \
    "addressV = TEXTURE_ADDRESS_BORDER)"

ConstantBuffer<FPerDrawConstantData> GPerDrawCB : register(b0);

Texture2D GEquirectangularMap : register(t0);
SamplerState GSampler : register(s0);

float2 SampleSphericalMap(float3 V)
{
	float2 UV = float2(atan2(V.x, V.z), asin(V.y));
	UV *= float2(0.1591f, 0.3183f);
	UV += 0.5f;
	return UV;
}

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
	float2 UV = SampleSphericalMap(normalize(InPositionOS));
	OutColor = GEquirectangularMap.Sample(GSampler, UV);
}

#include "../CPUAndGPUCommon.h"

#define GRootSignature \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
    "CBV(b0, visibility = SHADER_VISIBILITY_VERTEX), " \
    "DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_PIXEL), " \
    "StaticSampler(s0, " \
    "filter = FILTER_MIN_MAG_MIP_LINEAR, " \
    "visibility = SHADER_VISIBILITY_PIXEL, " \
    "addressU = TEXTURE_ADDRESS_BORDER, " \
    "addressV = TEXTURE_ADDRESS_BORDER, " \
    "addressW = TEXTURE_ADDRESS_BORDER)"

ConstantBuffer<FPerDrawConstantData> GPerDrawCB : register(b0);

TextureCube GCubeMap : register(t0);
SamplerState GSampler : register(s0);

[RootSignature(GRootSignature)]
void MainVS(
    in float3 InPosition : _Position,
    in float3 InNormal : _Normal,
    out float4 OutPosition : SV_Position,
    out float3 OutTexcoords : _Texcoords)
{
    OutPosition = mul(float4(InPosition, 1.0f), GPerDrawCB.ObjectToClip).xyww;
    OutTexcoords = InPosition;
}

[RootSignature(GRootSignature)]
void MainPS(
    in float4 InPosition : SV_Position,
    in float3 InTexcoords : _Texcoords,
    out float4 OutColor : SV_Target0)
{
    float3 EnvColor = GCubeMap.Sample(GSampler, InTexcoords).rgb;

	EnvColor = EnvColor / (EnvColor + 1.0f);
	EnvColor = pow(EnvColor, 1.0f / 2.2f);

	OutColor = float4(EnvColor, 1.0f);
}

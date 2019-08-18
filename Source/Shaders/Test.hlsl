#define GRootSignature "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT)"

[RootSignature(GRootSignature)]
void MainVS(
    in float3 InPosition : _Position,
    in float3 InNormal : _Normal,
    out float4 OutPosition : SV_Position)
{
    OutPosition = float4(InPosition, 1.0f);
}

[RootSignature(GRootSignature)]
void MainPS(
    in float4 InPosition : SV_Position,
    out float4 OutColor : SV_Target0)
{
    OutColor = float4(0.0f, 0.6f, 0.0f, 1.0f);
}

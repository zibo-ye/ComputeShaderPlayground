//StructuredBuffer<float4> srv : register(t0);
RWStructuredBuffer<float4> uav0 : register(u0);
RWStructuredBuffer<float4> uav1 : register(u1);

//[RootSignature("RootFlags(0), SRV(t0), UAV(u0)")]
[RootSignature("RootFlags(0), UAV(u0), UAV(u1)")]
[numthreads(64, 1, 1)]
void main(
	uint3 inGroupThreadID : SV_GroupThreadID,
	uint3 inGroupID : SV_GroupID,
	uint3 inDispatchThreadID : SV_DispatchThreadID,
	uint inGroupIndex : SV_GroupIndex)
{
    uav1[inDispatchThreadID.x] = uav0[inDispatchThreadID.x];
}

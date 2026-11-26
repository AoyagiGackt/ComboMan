// パーティクルのグループを空にする（全部の粒を死んだ状態にし、空き一覧に全部の枠を積む）
// グループを作った直後と、まとめて出し直す前（EmitBurst・EmitScatterLoop）に使う
#include "ParticleCompute.hlsli"

cbuffer InitConstants : register(b0)
{
    uint gMaxParticles; // グループの枠の数
};

RWStructuredBuffer<GPUParticleState> gParticles : register(u0);
RWStructuredBuffer<ParticleForGPU> gInstancing : register(u1);
RWStructuredBuffer<uint> gFreeList : register(u2);
RWStructuredBuffer<int> gCounter : register(u3);

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    if (idx >= gMaxParticles)
    {
        return;
    }
    gParticles[idx] = (GPUParticleState) 0;
    gInstancing[idx] = (ParticleForGPU) 0;
    // 番号の小さい枠から使われるよう、大きい番号ほど山の下に積む
    gFreeList[idx] = gMaxParticles - 1u - idx;
    if (idx == 0u)
    {
        gCounter[kFreeCountIndex] = int(gMaxParticles);
        gCounter[kDrawCountIndex] = 0;
    }
}

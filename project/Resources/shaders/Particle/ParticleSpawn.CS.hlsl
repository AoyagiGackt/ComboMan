// ゲームが出してほしいと頼んだ粒を、空き一覧から枠を取って書き込む（1スレッドで1粒）
// CPU から送るのは、このフレームに頼まれた粒の一覧だけ（粒全体の状態は送らない）
#include "ParticleCompute.hlsli"

cbuffer SpawnConstants : register(b0)
{
    uint gRequestCount; // 頼まれた粒の数
};

StructuredBuffer<GPUParticleState> gRequests : register(t0);
RWStructuredBuffer<GPUParticleState> gParticles : register(u0);
RWStructuredBuffer<uint> gFreeList : register(u2);
RWStructuredBuffer<int> gCounter : register(u3);

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    if (idx >= gRequestCount)
    {
        return;
    }
    uint slot = 0u;
    // 空きが無ければこの粒は出さない
    if (!PopFreeSlot(gCounter, gFreeList, slot))
    {
        return;
    }
    // 経過時間は頼まれたまま使う（ふつうは 0。最初から寿命の途中にいる粒をばらまく時だけ途中の値）
    GPUParticleState p = gRequests[idx];
    p.alive = 1u;
    gParticles[slot] = p;
}

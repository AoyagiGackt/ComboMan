// エミッター: 決めた間隔ごとに、中心のまわりへ放射状に粒を出す（1スレッドで1粒、枠は空き一覧から取る）
// 出す時刻の判定だけ CPU が行い、粒の中身はすべて GPU が乱数で決める
#include "ParticleCompute.hlsli"

cbuffer CBEmitter : register(b0)
{
    float3 gTranslate; //  0
    float gRadius; // 12 -> 16
    uint gCount; // 16
    float gFrequency; // 20
    float gFrequencyTime; // 24
    uint gEmit; // 28 -> 32
    float gLifeTime; // 32
    uint gSeed; // 36
    uint gTime; // 40
    float gPad; // 44 -> 48
    float4 gColor; // 48 -> 64
};

RWStructuredBuffer<GPUParticleState> gParticles : register(u0);
RWStructuredBuffer<uint> gFreeList : register(u2);
RWStructuredBuffer<int> gCounter : register(u3);

static const float kTwoPi = 6.28318530f;
static const float kSpeedMin = 0.5f; // 飛び出す速さ
static const float kSpeedMax = 2.0f;
static const float kScaleMin = 0.15f; // 粒の大きさ
static const float kScaleMax = 0.45f;

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    if (idx >= gCount)
    {
        return;
    }
    uint slot = 0u;
    if (!PopFreeSlot(gCounter, gFreeList, slot))
    {
        return;
    }
    uint4 rng = XoshiroSeed(gSeed, gTime, idx);
    float angle = Rand01(rng) * kTwoPi;
    float speed = RandRange(rng, kSpeedMin, kSpeedMax);
    float r = Rand01(rng) * gRadius;
    float scaleV = RandRange(rng, kScaleMin, kScaleMax);
    float3 dir = float3(cos(angle), sin(angle), 0.0f);

    GPUParticleState p = (GPUParticleState) 0;
    p.position = gTranslate + dir * r;
    p.velocity = dir * speed;
    p.scale = float3(scaleV, scaleV, scaleV);
    p.color = gColor;
    p.lifeTime = gLifeTime;
    p.alive = 1u;
    gParticles[slot] = p;
}

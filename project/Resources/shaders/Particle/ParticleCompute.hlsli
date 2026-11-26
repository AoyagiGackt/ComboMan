// GPU パーティクルの計算用シェーダー（初期化・発生・エミッター・更新）で共通に使う型と乱数

// 1粒の状態（C++ の GPUParticleState と同じ並び。80 バイト）
struct GPUParticleState
{
    float3 position; // 12
    float lifeTime; //  4 -> 16
    float3 velocity; // 12
    float currentTime; //  4 -> 32
    float4 color; // 16 -> 48
    float3 scale; // 12
    float rotateZ; //  4 -> 64
    uint alive; //  4
    uint curveFlag; //  4  ParticleMotion の番号
    float2 pad; //  8 -> 80
};

// 描画のために頂点シェーダーが読む1粒分（C++ の ParticleForGPU と同じ並び）
struct ParticleForGPU
{
    float4x4 WVP;
    float4x4 World;
    float4 color;
    float4 uvRect; // xy=UV のずらし zw=UV の倍率（コマ送り・左右反転）
    float4 params; // x=焼け具合 y=熱さ z=1なら丸く切り抜かない
};

// 空き一覧の数え上げ（C++ の kFreeCountIndex と同じ番号）
static const uint kFreeCountIndex = 0;
// このフレームに生きている粒の数（ParticleUpdate.CS が数え、ParticleBuildArgs.CS が描画の引数にして 0 へ戻す）
static const uint kDrawCountIndex = 1;

// -------------------------------------------------------
//  空き一覧（空いている枠の番号を積んだ山）
//  取り出す: 数を 1 減らし、減らす前の数が 1 以上なら、その 1 つ下の段の番号を使う
//  返す    : 数を 1 増やし、増やす前の数の段へ番号を書く
//  取り出しと返却は同じディスパッチの中で混ぜない（間に UAV バリアを挟む）
// -------------------------------------------------------

// 空き一覧から枠を1つ取り出す。空きが無ければ false（減らした数は戻す）
bool PopFreeSlot(RWStructuredBuffer<int> counter, RWStructuredBuffer<uint> freeList, out uint slot)
{
    int before = 0;
    InterlockedAdd(counter[kFreeCountIndex], -1, before);
    if (before <= 0)
    {
        InterlockedAdd(counter[kFreeCountIndex], 1);
        slot = 0u;
        return false;
    }
    slot = freeList[before - 1];
    return true;
}

// 枠を空き一覧へ返す
void PushFreeSlot(RWStructuredBuffer<int> counter, RWStructuredBuffer<uint> freeList, uint slot)
{
    int before = 0;
    InterlockedAdd(counter[kFreeCountIndex], 1, before);
    freeList[before] = slot;
}

// -------------------------------------------------------
//  xoshiro128+ の乱数（スレッドごとに独立した列）
// -------------------------------------------------------
uint Rotl32(uint x, int k)
{
    return (x << k) | (x >> (32 - k));
}

// splitmix32 風のかき混ぜで、種・時刻・番号から初期状態を作る
uint4 XoshiroSeed(uint seed, uint time, uint idx)
{
    uint a = seed ^ (idx * 2654435761u);
    uint b = time ^ (idx * 2246822519u);
    a ^= a >> 16;
    a *= 0x45d9f3bu;
    a ^= a >> 16;
    b ^= b >> 16;
    b *= 0x45d9f3bu;
    b ^= b >> 16;
    return uint4(a, b, a ^ 0x9e3779b9u, b ^ 0x6c62272eu);
}

uint XoshiroNext(inout uint4 s)
{
    uint result = s.x + s.w;
    uint t = s.y << 9;
    s.z ^= s.x;
    s.w ^= s.y;
    s.y ^= s.z;
    s.x ^= s.w;
    s.z ^= t;
    s.w = Rotl32(s.w, 11);
    return result;
}

// [0, 1) の乱数
float Rand01(inout uint4 s)
{
    return float(XoshiroNext(s) >> 8) * (1.0f / 16777216.0f);
}

// [lo, hi) の乱数
float RandRange(inout uint4 s, float lo, float hi)
{
    return lo + (hi - lo) * Rand01(s);
}

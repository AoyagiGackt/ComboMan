// 生きている粒の数から、間接描画（ExecuteIndirect）の引数を作る
// ParticleUpdate.CS の後に1スレッドだけ走らせ、数えた数は次のフレームのために 0 へ戻す
#include "ParticleCompute.hlsli"

cbuffer BuildArgsConstants : register(b0)
{
    uint gIndexCountPerInstance; // 板1枚のインデックス数
};

RWStructuredBuffer<int> gCounter : register(u3);
// D3D12_DRAW_INDEXED_ARGUMENTS と同じ並び（1回の描画のインデックス数, 繰り返す数, 最初のインデックス, 頂点の足し数, 最初の繰り返し番号）
RWStructuredBuffer<uint> gDrawArgs : register(u5);

static const uint kArgIndexCount = 0;
static const uint kArgInstanceCount = 1;
static const uint kArgStartIndex = 2;
static const uint kArgBaseVertex = 3;
static const uint kArgStartInstance = 4;

[numthreads(1, 1, 1)]
void main()
{
    uint aliveCount = uint(max(gCounter[kDrawCountIndex], 0));
    gDrawArgs[kArgIndexCount] = gIndexCountPerInstance;
    gDrawArgs[kArgInstanceCount] = aliveCount;
    gDrawArgs[kArgStartIndex] = 0u;
    gDrawArgs[kArgBaseVertex] = 0u;
    gDrawArgs[kArgStartInstance] = 0u;
    gCounter[kDrawCountIndex] = 0;
}

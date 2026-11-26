/**
 * @file WeaponTrail.PS.hlsl
 * @brief 刃の軌跡の帯に筋の画像を掛け、明るさの倍率で芯を白く飛ばす（加算で描く）
 */
cbuffer TrailParams : register(b0)
{
    float4x4 viewProj;
    float intensity; ///< 明るさの倍率（1を超えると芯が白く飛び、ブルームで光る）
    float uOffset;
    float2 pad;
};

Texture2D<float4> gStreak : register(t0);
SamplerState gSampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

static const float kDiscardAlpha = 0.001f; ///< これ以下の濃さは描かない
static const float kCoreWhiteStart = 0.6f; ///< 筋の濃さがこれを超えた所から芯を白へ寄せる

float4 main(PSInput input) : SV_TARGET
{
    // 筋の画像は白黒。白いほど濃く、濃い所ほど白い芯に寄せて光らせる
    float streak = gStreak.Sample(gSampler, input.uv).r;
    float alpha = input.color.a * streak;
    if (alpha <= kDiscardAlpha)
    {
        discard;
    }
    float core = saturate((streak - kCoreWhiteStart) / (1.0f - kCoreWhiteStart));
    float3 rgb = lerp(input.color.rgb, float3(1.0f, 1.0f, 1.0f), core * saturate(intensity - 1.0f));
    return float4(rgb * intensity, alpha);
}

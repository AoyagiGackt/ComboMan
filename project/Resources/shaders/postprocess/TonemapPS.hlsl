/**
 * @file TonemapPS.hlsl
 * @brief 1 を超える明るさを持つシーンの色を、画面に出せる 0〜1 へ丸めるトーンマップ
 *
 * 2 つの曲線を gFilmicBlend の割合で混ぜる。
 *   肩だけ丸める曲線: gShoulderStart までの明るさはそのまま、それより明るい所だけを 1 へ向けてなめらかに寄せる。
 *                     いちばん明るい色の成分で丸めるので、色合いは変わらない。
 *   映画風の曲線    : 中間を少し持ち上げ、明るい所を強く寝かせる。全体の見た目が変わる。
 * 入力も出力も線形の値（描き込み先が SRGB の変換を受け持つ）。
 */
struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};

cbuffer TonemapParams : register(b0)
{
    float gExposure;      ///< 丸める前に掛ける明るさの倍率
    float gFilmicBlend;   ///< 映画風の曲線を混ぜる割合（0〜1）
    float gShoulderStart; ///< 肩だけ丸める曲線で、丸め始める明るさ
    float gPadding;
};

Texture2D<float4> gHDRTexture : register(t0);
SamplerState gSampler : register(s0);

static const float kEpsilon = 1e-5f;

/** @brief 映画風の曲線の近似（入力は線形の明るさ） */
float3 FilmicCurve(float3 x)
{
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

/** @brief 丸め始める明るさまではそのまま、それより上を 1 へ向けて寄せる（いちばん明るい成分で丸めて色合いを保つ） */
float3 ShoulderCurve(float3 color)
{
    float peak = max(max(color.r, color.g), color.b);
    float range = max(1.0f - gShoulderStart, kEpsilon);
    // 丸め始めを超えた分を、幅で割った量にする（超えていなければ 0 で、そのままの明るさになる）
    float excess = max(peak - gShoulderStart, 0.0f) / range;
    float mappedPeak = min(peak, gShoulderStart) + range * excess / (1.0f + excess);
    return color * (mappedPeak / max(peak, kEpsilon));
}

float4 main(VSOutput input) : SV_TARGET
{
    float3 hdr = max(gHDRTexture.Sample(gSampler, input.uv).rgb, 0.0f) * gExposure;
    float3 ldr = lerp(ShoulderCurve(hdr), FilmicCurve(hdr), gFilmicBlend);
    return float4(saturate(ldr), 1.0f);
}

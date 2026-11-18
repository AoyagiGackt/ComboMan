#include "Particle.hlsli"

Texture2D<float4> gTexture : register(t1);
SamplerState gSampler : register(s0);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

static const float kErodeSoftness = 0.18f; // 焼けて消える境目のぼかし幅
static const float kHeatPower = 1.6f; // 濃い所ほど熱く見せる強さ（大きいほど芯だけが白い）
static const float3 kHeatColor = float3(1.0f, 0.93f, 0.72f); // 一番熱い所の色
static const float kHeatGlow = 1.35f; // 熱い所の明るさ
static const float kHeatWhiten = 0.75f; // 熱い所の色を、粒の色から一番熱い色へ寄せる割合

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    // UV 中心 (0.5, 0.5) からの距離で円形マスク（コマ送り前の UV で測る）
    float2 uv = input.maskcoord - float2(0.5f, 0.5f);
    float dist = length(uv) * 2.0f; // 0=中心, 1=エッジ
    bool useMask = input.params.z < 0.5f;
    if (useMask && dist > 1.0f)
        discard;

    float4 textureColor = gTexture.Sample(gSampler, input.texcoord);
    output.color = input.color * textureColor;

    // 濃さ（明るさ×不透明度）。焼け具合と熱さはこれを基準にする
    float density = textureColor.a * max(textureColor.r, max(textureColor.g, textureColor.b));

    // 熱さ: 濃い所ほど白く熱い色へ寄せ、薄い所は粒の色のまま（炎の芯と縁）
    if (input.params.y > 0.0f)
    {
        float heat = saturate(pow(density, kHeatPower) * input.params.y);
        float3 hot = lerp(input.color.rgb, kHeatColor, kHeatWhiten) * kHeatGlow;
        output.color.rgb = lerp(input.color.rgb, hot, heat);
    }

    // 焼け具合: 薄い所から削れて消える（炎がちぎれる・煙がほどける）
    if (input.params.x > 0.0f)
    {
        output.color.a *= smoothstep(input.params.x, input.params.x + kErodeSoftness, density);
    }

    // エッジをソフトにフェード
    if (useMask)
        output.color.a *= 1.0f - smoothstep(0.6f, 1.0f, dist);

    if (output.color.a == 0.0f)
        discard;

    return output;
}

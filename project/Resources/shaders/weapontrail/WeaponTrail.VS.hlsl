/**
 * @file WeaponTrail.VS.hlsl
 * @brief 刃の軌跡の帯の頂点をクリップ空間へ移し、色と筋の画像の位置を渡す
 */
cbuffer TrailParams : register(b0)
{
    float4x4 viewProj;
    float intensity; ///< 明るさの倍率（ピクセルシェーダーで使う）
    float uOffset; ///< 筋の画像を軌跡に沿って流すずれ
    float2 pad;
};

struct VSInput
{
    float4 position : POSITION0;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.position = mul(input.position, viewProj);
    output.color = input.color;
    output.uv = float2(input.uv.x + uOffset, input.uv.y);
    return output;
}

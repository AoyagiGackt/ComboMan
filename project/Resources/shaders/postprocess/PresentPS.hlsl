/**
 * @file PresentPS.hlsl
 * @brief フレームの最後に、シーンの色バッファ（浮動小数点）をスワップチェーンへ写す
 *
 * 色バッファとスワップチェーンは同じ大きさなので、画素の番号でそのまま読む。
 * 1 を超える明るさはここで切り捨てる（トーンマップを掛けた場面では、もう 1 以下にそろっている）。
 * 描き込み先は SRGB なので、線形の値のまま返せば保存の時に変換される。
 */
struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};

Texture2D<float4> gSceneColor : register(t0);

float4 main(PSInput input) : SV_TARGET
{
    float3 color = gSceneColor.Load(int3(input.position.xy, 0)).rgb;
    return float4(saturate(color), 1.0f);
}

// スプライトのピクセルシェーダー（バインドレス版）
// テクスチャを記述子テーブルで受け取らず、ルート定数で受け取った番号でデスクリプタヒープから直接引く（シェーダーモデル 6.6）
#include "../object3d/Object3d.hlsli"

SamplerState gSampler : register(s0);

struct Material
{
    float4 color;
    int enableLighting;
    int shadingType;
    int useCubemap;
    int useTexture;
    float4x4 uvTransform;
};
ConstantBuffer<Material> gMaterial : register(b0);

// 使うテクスチャのデスクリプタヒープ上の番号（SrvManager の番号と同じ）
struct TextureSlot
{
    uint index;
};
ConstantBuffer<TextureSlot> gTextureSlot : register(b1);

float4 main(VertexShaderOutput input) : SV_TARGET0
{
    float2 uv = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform).xy;
    float4 textureColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
    if (gMaterial.useTexture != 0) {
        Texture2D<float4> spriteTexture = ResourceDescriptorHeap[gTextureSlot.index];
        textureColor = spriteTexture.Sample(gSampler, uv);
    }
    float4 color = gMaterial.color * textureColor;
    if (color.a <= 0.0f) {
        discard;
    }
    return color;
}

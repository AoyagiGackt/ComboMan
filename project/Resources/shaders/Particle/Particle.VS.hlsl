#include "Particle.hlsli"

struct ParticleForGPU
{
    float4x4 WVP;
    float4x4 World;
    float4 color;
    float4 uvRect; // xy=UV のずらし zw=UV の倍率（コマ送り・左右反転）
    float4 params; // x=焼け具合 y=熱さ z=1なら丸く切り抜かない
};

StructuredBuffer<ParticleForGPU> gParticleData : register(t0);

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

VertexShaderOutput main(VertexShaderInput input, uint instanceId : SV_InstanceID)
{
    VertexShaderOutput output;
	
	// インスタンスデータ（行列と色）を取得
    ParticleForGPU particle = gParticleData[instanceId];

	// 座標変換
    output.position = mul(input.position, particle.WVP);
    // 透視投影の w はカメラから見た奥行きそのもの
    output.viewDepth = output.position.w;
    // 死んでいる枠（透明）は画面の外へ追い出して、ピクセルシェーダーを走らせずに捨てる
    if (particle.color.a <= 0.0f)
    {
        output.position = float4(2.0f, 2.0f, 2.0f, 1.0f);
    }
    output.texcoord = input.texcoord * particle.uvRect.zw + particle.uvRect.xy;
    output.maskcoord = input.texcoord;
    output.params = particle.params;
    output.normal = normalize(mul(input.normal, (float3x3) particle.World));
	
    output.color = particle.color;
	
    return output;
}
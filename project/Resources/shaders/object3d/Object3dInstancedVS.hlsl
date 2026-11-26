// 同じモデル・同じマテリアルの物をまとめて1回で描く時の頂点シェーダー
// 1体ごとの変換行列は、定数バッファではなく2本目の頂点バッファ（1体ごとに進む）から受け取る
// 並びと計算は Object3dVS.hlsl と同じ（C++ の Object3d::TransformationMatrix をそのまま並べた物）

#include "Object3d.hlsli"

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float3 tangent : TANGENT0;
};

// 1体ごとのデータ（行列は行を4つの float4 で受け取る）
struct InstanceInput
{
    float4 wvp0 : INSTANCE_WVP0;
    float4 wvp1 : INSTANCE_WVP1;
    float4 wvp2 : INSTANCE_WVP2;
    float4 wvp3 : INSTANCE_WVP3;
    float4 world0 : INSTANCE_WORLD0;
    float4 world1 : INSTANCE_WORLD1;
    float4 world2 : INSTANCE_WORLD2;
    float4 world3 : INSTANCE_WORLD3;
    float4 worldInverseTranspose0 : INSTANCE_WIT0;
    float4 worldInverseTranspose1 : INSTANCE_WIT1;
    float4 worldInverseTranspose2 : INSTANCE_WIT2;
    float4 worldInverseTranspose3 : INSTANCE_WIT3;
    float4 lightVP0 : INSTANCE_LIGHT0;
    float4 lightVP1 : INSTANCE_LIGHT1;
    float4 lightVP2 : INSTANCE_LIGHT2;
    float4 lightVP3 : INSTANCE_LIGHT3;
};

VertexShaderOutput main(VertexShaderInput input, InstanceInput instance)
{
    // 行を並べて行列に戻す（C++ 側も行の順に並んでいる）
    const float4x4 wvp = float4x4(instance.wvp0, instance.wvp1, instance.wvp2, instance.wvp3);
    const float4x4 world = float4x4(instance.world0, instance.world1, instance.world2, instance.world3);
    const float4x4 worldInverseTranspose = float4x4(instance.worldInverseTranspose0, instance.worldInverseTranspose1,
        instance.worldInverseTranspose2, instance.worldInverseTranspose3);
    const float4x4 lightVP = float4x4(instance.lightVP0, instance.lightVP1, instance.lightVP2, instance.lightVP3);

    VertexShaderOutput output;
    output.position = mul(input.position, wvp);
    output.texcoord = input.texcoord;
    output.worldPos = mul(input.position, world).xyz;
    output.lightSpacePos = mul(input.position, lightVP);

    float3 N = normalize(mul(input.normal, (float3x3) worldInverseTranspose));
    float3 T = normalize(mul(input.tangent, (float3x3) world));
    T = normalize(T - N * dot(N, T)); // 接線を法線に直交させ直す
    float3 B = cross(N, T);

    output.normal = N;
    output.tangent = T;
    output.bitangent = B;
    return output;
}

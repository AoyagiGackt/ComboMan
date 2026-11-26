/**
 * @file ParticleManagerPipeline.cpp
 * @brief ParticleManagerのルートシグネチャ・パイプラインステート・ジオメトリ生成を実装するファイル
 * @note ParticleManager のパイプラインの作成だけを ParticleManager.cpp から分けて置いている（クラスは ParticleManager のまま）
 */
#include "ParticleManager.h"
#include "EngineAssert.h"
#include "PipelineHelper.h"
#include <cstring>
#include <d3dx12.h>
#include <initializer_list>
#include <vector>

using namespace Microsoft::WRL;

namespace engine::graphics {
namespace {

/** @brief 全段から見えるルートの SRV（バッファを直接指す）を作る */
D3D12_ROOT_PARAMETER MakeRootSrvParam(UINT shaderRegister) {
    D3D12_ROOT_PARAMETER parameter {};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameter.Descriptor.ShaderRegister = shaderRegister;
    return parameter;
}

/** @brief 計算用シェーダーのルートシグネチャを、並べたルートパラメータから作る */
ComPtr<ID3D12RootSignature> CreateComputeRootSignature(ID3D12Device* device, std::initializer_list<D3D12_ROOT_PARAMETER> parameters) {
    const std::vector<D3D12_ROOT_PARAMETER> list(parameters);
    ComPtr<ID3D12RootSignature> signature = pipelineHelper::CreateRootSignature(device, list.data(), static_cast<UINT>(list.size()),
        nullptr, 0, D3D12_ROOT_SIGNATURE_FLAG_NONE);
    ENGINE_ASSERT(signature);
    return signature;
}

// シェーダーのレジスタ番号（ParticleCompute.hlsli を使う計算用シェーダーで共通）
constexpr UINT kRegisterB0 = 0;
constexpr UINT kRegisterB1 = 1;
constexpr UINT kRegisterB2 = 2;
constexpr UINT kRegisterT0 = 0;
constexpr UINT kRegisterParticles = 0; // u0
constexpr UINT kRegisterInstances = 1; // u1
constexpr UINT kRegisterFreeList = 2; // u2
constexpr UINT kRegisterCounter = 3; // u3
constexpr UINT kRegisterDrawList = 4; // u4（ParticleUpdate.CS が生きている粒の番号を書く）
constexpr UINT kRegisterDrawArgs = 5; // u5（ParticleBuildArgs.CS が間接描画の引数を書く）
constexpr UINT kRegisterDrawListSrv = 3; // t3（頂点シェーダーが生きている粒の番号を読む）

} // namespace

// ══════════════════════════════════════════════════════
// パイプライン生成
// ══════════════════════════════════════════════════════

/** @brief パーティクル描画用のルートシグネチャを生成する */
void ParticleManager::CreateRootSignature() {
    ID3D12Device* device = dxCommon_->GetDevice();

    D3D12_DESCRIPTOR_RANGE rangeT0[1] { };
    rangeT0[0].BaseShaderRegister = 0;
    rangeT0[0].NumDescriptors = 1;
    rangeT0[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rangeT0[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE rangeT1[1] { };
    rangeT1[0].BaseShaderRegister = 1;
    rangeT1[0].NumDescriptors = 1;
    rangeT1[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rangeT1[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE rangeT2[1] { };
    rangeT2[0].BaseShaderRegister = 2;
    rangeT2[0].NumDescriptors = 1;
    rangeT2[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rangeT2[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // slot 0: t0 (VS が読む instancing StructuredBuffer)
    // slot 1: t1 (PS が読む Texture2D)
    // slot 2: b0 (PS が読むソフトパーティクルの定数。Draw で必ず設定する)
    // slot 3: t2 (PS が読む、写し取った深度)
    // slot 4: t3 (VS が読む、生きている粒の番号の一覧。バッファを直接指す)
    D3D12_ROOT_PARAMETER rootParameters[5] { };
    rootParameters[pipelineHelper::particleRootSlot::kInstances] = pipelineHelper::MakeSrvTableParam(rangeT0, D3D12_SHADER_VISIBILITY_VERTEX);
    rootParameters[pipelineHelper::particleRootSlot::kTexture] = pipelineHelper::MakeSrvTableParam(rangeT1, D3D12_SHADER_VISIBILITY_PIXEL);
    rootParameters[pipelineHelper::particleRootSlot::kSoftConstants] = pipelineHelper::MakeCbvParam(kRegisterB0, D3D12_SHADER_VISIBILITY_PIXEL);
    rootParameters[pipelineHelper::particleRootSlot::kSceneDepth] = pipelineHelper::MakeSrvTableParam(rangeT2, D3D12_SHADER_VISIBILITY_PIXEL);
    rootParameters[pipelineHelper::particleRootSlot::kDrawList] = MakeRootSrvParam(kRegisterDrawListSrv);
    rootParameters[pipelineHelper::particleRootSlot::kDrawList].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_STATIC_SAMPLER_DESC sampler = pipelineHelper::MakePixelSampler(D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP);

    rootSignature_ = pipelineHelper::CreateRootSignature(device, rootParameters, _countof(rootParameters),
        &sampler, 1, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    ENGINE_ASSERT(rootSignature_);
}

/**
 * @brief 4つの計算用シェーダー（空にする・頼まれた粒を出す・エミッター・動かす）のルートシグネチャとパイプラインを作る
 * @note ルートパラメータの並びは PipelineHelper.h の particleComputeSlot と同じにする
 */
void ParticleManager::CreateComputePipelines() {
    ID3D12Device* device = dxCommon_->GetDevice();
    using pipelineHelper::MakeCbvParam;
    using pipelineHelper::MakeUavParam;
    constexpr D3D12_SHADER_VISIBILITY kAll = D3D12_SHADER_VISIBILITY_ALL;

    // 空にする: b0 定数, u0 粒, u1 描画用, u2 空き一覧, u3 空きの数
    initPass_.rootSignature = CreateComputeRootSignature(device, {
        MakeCbvParam(kRegisterB0, kAll), MakeUavParam(kRegisterParticles), MakeUavParam(kRegisterInstances),
        MakeUavParam(kRegisterFreeList), MakeUavParam(kRegisterCounter) });
    initPass_.pipeline = pipelineHelper::CreateComputePipeline(dxCommon_, initPass_.rootSignature.Get(),
        L"Resources/shaders/Particle/ParticleInit.CS.hlsl");

    // 頼まれた粒を出す: b0 定数, t0 頼まれた粒, u0 粒, u2 空き一覧, u3 空きの数
    spawnPass_.rootSignature = CreateComputeRootSignature(device, {
        MakeCbvParam(kRegisterB0, kAll), MakeRootSrvParam(kRegisterT0), MakeUavParam(kRegisterParticles),
        MakeUavParam(kRegisterFreeList), MakeUavParam(kRegisterCounter) });
    spawnPass_.pipeline = pipelineHelper::CreateComputePipeline(dxCommon_, spawnPass_.rootSignature.Get(),
        L"Resources/shaders/Particle/ParticleSpawn.CS.hlsl");

    // エミッター: b0 エミッター, u0 粒, u2 空き一覧, u3 空きの数
    emitPass_.rootSignature = CreateComputeRootSignature(device, {
        MakeCbvParam(kRegisterB0, kAll), MakeUavParam(kRegisterParticles),
        MakeUavParam(kRegisterFreeList), MakeUavParam(kRegisterCounter) });
    emitPass_.pipeline = pipelineHelper::CreateComputePipeline(dxCommon_, emitPass_.rootSignature.Get(),
        L"Resources/shaders/Particle/EmitParticle.CS.hlsl");

    // 動かす: b0 定数, b1 出し直し, u0 粒, u1 描画用, u2 空き一覧, u3 空きの数, b2 場, u4 生きている粒の一覧
    updatePass_.rootSignature = CreateComputeRootSignature(device, {
        MakeCbvParam(kRegisterB0, kAll), MakeCbvParam(kRegisterB1, kAll), MakeUavParam(kRegisterParticles),
        MakeUavParam(kRegisterInstances), MakeUavParam(kRegisterFreeList), MakeUavParam(kRegisterCounter),
        MakeCbvParam(kRegisterB2, kAll), MakeUavParam(kRegisterDrawList) });
    updatePass_.pipeline = pipelineHelper::CreateComputePipeline(dxCommon_, updatePass_.rootSignature.Get(),
        L"Resources/shaders/Particle/ParticleUpdate.CS.hlsl");

    // 間接描画の引数を作る: b0 定数, u3 空きの数と生きている数, u5 間接描画の引数
    buildArgsPass_.rootSignature = CreateComputeRootSignature(device, {
        MakeCbvParam(kRegisterB0, kAll), MakeUavParam(kRegisterCounter), MakeUavParam(kRegisterDrawArgs) });
    buildArgsPass_.pipeline = pipelineHelper::CreateComputePipeline(dxCommon_, buildArgsPass_.rootSignature.Get(),
        L"Resources/shaders/Particle/ParticleBuildArgs.CS.hlsl");
}

/** @brief 間接描画（インデックス付きの描画1回）の命令の形を作る */
void ParticleManager::CreateDrawCommandSignature() {
    // 引数はインデックス付きの描画1回分だけ。ルートの値は書き換えないので、ルートシグネチャは渡さない
    D3D12_INDIRECT_ARGUMENT_DESC argument {};
    argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
    D3D12_COMMAND_SIGNATURE_DESC desc {};
    desc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
    desc.NumArgumentDescs = 1;
    desc.pArgumentDescs = &argument;
    const HRESULT hr = dxCommon_->GetDevice()->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(&drawCommandSignature_));
    ENGINE_ASSERT(SUCCEEDED(hr));
}

/** @brief 粒子1枚分の板ポリゴンの頂点・インデックスバッファを作る */
void ParticleManager::CreateQuadGeometry() {
    /** @brief パーティクル描画の頂点位置とテクスチャ座標 */
    struct Vertex {
        float pos[4];
        float uv[2];
        float nrm[3];
    };

    Vertex verts[4] = {
        { { -0.5f, 0.5f, 0.f, 1.f }, { 0.f, 0.f }, { 0.f, 0.f, -1.f } },
        { { 0.5f, 0.5f, 0.f, 1.f }, { 1.f, 0.f }, { 0.f, 0.f, -1.f } },
        { { 0.5f, -0.5f, 0.f, 1.f }, { 1.f, 1.f }, { 0.f, 0.f, -1.f } },
        { { -0.5f, -0.5f, 0.f, 1.f }, { 0.f, 1.f }, { 0.f, 0.f, -1.f } },
    };
    const uint32_t indices[6] = { 0, 1, 2, 0, 2, 3 };

    quadVertexBuffer_ = dxCommon_->CreateBufferResource(sizeof(verts));
    void* mapped = nullptr;
    quadVertexBuffer_->Map(0, nullptr, &mapped);
    memcpy(mapped, verts, sizeof(verts));
    quadVertexBuffer_->Unmap(0, nullptr);

    quadVBV_.BufferLocation = quadVertexBuffer_->GetGPUVirtualAddress();
    quadVBV_.SizeInBytes = sizeof(verts);
    quadVBV_.StrideInBytes = sizeof(Vertex);

    quadIndexBuffer_ = dxCommon_->CreateBufferResource(sizeof(indices));
    quadIndexBuffer_->Map(0, nullptr, &mapped);
    memcpy(mapped, indices, sizeof(indices));
    quadIndexBuffer_->Unmap(0, nullptr);

    quadIBV_.BufferLocation = quadIndexBuffer_->GetGPUVirtualAddress();
    quadIBV_.SizeInBytes = sizeof(indices);
    quadIBV_.Format = DXGI_FORMAT_R32_UINT;
}

//  グラフィックスパイプラインステート

/** @brief 粒子描画用のグラフィックスパイプラインステートを作る */
void ParticleManager::CreatePipelineState() {
    ID3D12Device* device = dxCommon_->GetDevice();

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = pipelineHelper::MakeFullscreenPsoDesc();
    psoDesc.pRootSignature = rootSignature_.Get();
    psoDesc.InputLayout = { pipelineHelper::kEffectMeshInputLayout, _countof(pipelineHelper::kEffectMeshInputLayout) };

    psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
    psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    // RT のアルファをパーティクルのフェードアウトで上書きしない（黒くなる原因の修正）
    psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
    psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;

    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

    // 同じシェーダーを2種類のブレンドPSOの生成が済むまで保持する。
    const auto vs = dxCommon_->CompileShader(L"Resources/shaders/Particle/Particle.VS.hlsl", L"vs_6_0");
    const auto ps = dxCommon_->CompileShader(L"Resources/shaders/Particle/Particle.PS.hlsl", L"ps_6_0");
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    graphicsPipelineState_ = pipelineHelper::CreateGraphicsPipeline(device, psoDesc);

    // Alpha blend variant: DestBlend を INV_SRC_ALPHA に変えるだけ
    psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    graphicsPipelineStateAlpha_ = pipelineHelper::CreateGraphicsPipeline(device, psoDesc);
}

} // namespace engine::graphics

/**
 * @file PostEffectFullscreenPass.cpp
 * @brief 全画面ポストエフェクト共通の資源と描画（PostEffectFullscreenPass）の実装
 */
#include "PostEffectFullscreenPass.h"
#include "PipelineHelper.h"
#include "ResourceAlignment.h"
#include "EngineAssert.h"
#include "WinApp.h"

namespace engine::graphics {
using namespace Microsoft::WRL;

/** @brief 全画面ポストエフェクトの共通資源（ルートシグネチャ・PSO・定数バッファ）を作り、定数バッファの書き込み先を返す */
void* PostEffectFullscreenPass::InitializeCommon(DirectXCommon* dxCommon, SrvManager* srvManager, const wchar_t* psShaderPath) {
    dxCommon_ = dxCommon;
    ID3D12Device* device = dxCommon->GetDevice();

    // シーンをそのまま描き込むので、描画先の PSO と同じシーンの色バッファの形式にする
    pipelineHelper::CreateRenderTarget(device, srvManager, WinApp::kClientWidth, WinApp::kClientHeight,
        pipelineHelper::kSceneColorTargetFormats, pipelineHelper::kOffscreenClearColor, { sceneTexture_, rtvHeap_, rtvHandle_, srvIndex_ });
    // シーンの描き込み先の状態は、描画パスの宣言から RenderPassGraph が切り替える（作った直後は描画先の状態）
    dxCommon->GetResourceStateTracker().Register(sceneTexture_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

    // 効果ごとの値を書き込む定数バッファ（呼び出し側へ書き込み先を返す）

    cbResource_ = dxCommon->CreateBufferResource(kConstantBufferAlignment);
    cbResource_->Map(0, nullptr, &cbData_);

    const D3D12_DESCRIPTOR_RANGE srvRange = pipelineHelper::MakeSrvRange(0); // t0

    D3D12_ROOT_PARAMETER rootParams[2] = { };
    rootParams[0] = pipelineHelper::MakeCbvParam(0, D3D12_SHADER_VISIBILITY_PIXEL);
    rootParams[1] = pipelineHelper::MakeSrvTableParam(&srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

    D3D12_STATIC_SAMPLER_DESC sampler = pipelineHelper::MakeLinearClampSampler();

    rootSignature_ = pipelineHelper::CreateRootSignature(device, rootParams, 2,
        &sampler, 1, D3D12_ROOT_SIGNATURE_FLAG_NONE);
    ENGINE_ASSERT(rootSignature_);

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = pipelineHelper::MakeFullscreenPsoDesc();
    psoDesc.pRootSignature = rootSignature_.Get();
    // InputLayout なし: VS が SV_VertexID から頂点座標を生成する

    pipelineState_ = pipelineHelper::CreateGraphicsPipeline(dxCommon, psoDesc,
        L"Resources/shaders/postprocess/FullscreenVS.hlsl", psShaderPath);

    return cbData_;
}

/** @brief GPUが使用していない共通資源を解放する。呼び出し側で使用完了を保証する */
void PostEffectFullscreenPass::FinalizeCommon() {
    pipelineHelper::ReleaseEffectResources(cbResource_, cbData_, pipelineState_, rootSignature_);
    rtvHeap_.Reset();
    if (dxCommon_ != nullptr) {
        dxCommon_->GetResourceStateTracker().Unregister(sceneTexture_.Get());
    }
    sceneTexture_.Reset();
}

/**
 * @brief オフスクリーンRTV（＋共有深度バッファ）をレンダーターゲットに設定しクリアする
 * @note 描画先の状態への切り替えは、このパスの宣言から RenderPassGraph が入れる
 */
void PostEffectFullscreenPass::BeginScene() {
    auto* cmdList = dxCommon_->GetCommandList();

    // 共有の深度バッファを再利用することで、3D 深度テストが正しく機能する
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = dxCommon_->GetDsvHandle();
    cmdList->OMSetRenderTargets(1, &rtvHandle_, FALSE, &dsv);
    dxCommon_->SetActiveDepthTarget(DirectXCommon::DepthTarget::kScene);

    constexpr const float (&kClearColor)[4] = pipelineHelper::kOffscreenClearColor;
    cmdList->ClearRenderTargetView(rtvHandle_, kClearColor, 0, nullptr);

    pipelineHelper::SetClientViewportAndScissor(cmdList);
}

/**
 * @brief シーン描画を終了する
 * @note 読む状態への切り替えは、合成するパスの宣言から RenderPassGraph が入れるので、ここでは何もしない
 */
void PostEffectFullscreenPass::EndScene() {
}

/** @brief バックバッファをレンダーターゲットに戻し、オフスクリーン結果をフルスクリーンPSで合成する */
void PostEffectFullscreenPass::Apply(SrvManager* srvManager) {
    auto* cmdList = dxCommon_->GetCommandList();

    // バックバッファをアクティブなレンダーターゲットとして復元する（フルスクリーン四角形に深度は不要）
    pipelineHelper::BindBackBufferTarget(dxCommon_, cmdList);

    pipelineHelper::SetTrianglePipeline(cmdList, rootSignature_.Get(), pipelineState_.Get());

    cmdList->SetGraphicsRootConstantBufferView(pipelineHelper::fullscreenRootSlot::kConstants, cbResource_->GetGPUVirtualAddress());
    cmdList->SetGraphicsRootDescriptorTable(pipelineHelper::fullscreenRootSlot::kTexture, srvManager->GetGPUDescriptorHandle(srvIndex_));

    // フルスクリーン三角形を描画する（頂点バッファなし、VS が SV_VertexID を使用）
    pipelineHelper::DrawFullscreenTriangle(cmdList);
}

} // namespace engine::graphics

/**
 * @file DirectXCommonPresent.cpp
 * @brief シーンの色バッファ（浮動小数点）の作成と、フレームの最後にスワップチェーンへ写す処理の実装
 * @note クラスは DirectXCommon のまま、色バッファと画面へ出す前の写しの処理だけをこのファイルに分けている
 */
#include "DirectXCommon.h"
#include "PipelineHelper.h"
#include "EngineAssert.h"
#include <iterator>

namespace engine {
namespace {
/** @brief 写すパスのルート引数の番号（SRV のテーブル1つだけ） */
constexpr UINT kPresentSceneColorSlot = 0;
/** @brief 写すパスが読むテクスチャの数 */
constexpr UINT kPresentSrvCount = 1;
/** @brief 色バッファを消去する色（PreDraw の黒と同じにして、消去の最適化を効かせる） */
constexpr float kSceneColorClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
} // namespace

/** @brief スワップチェーンと同じ大きさのシーンの色バッファと、その RTV・SRV を作る */
void DirectXCommon::CreateSceneColorBuffer() {
    // 大きさはスワップチェーンに合わせる（縦横比の余白も含めて同じ座標で描けるようにする）
    DXGI_SWAP_CHAIN_DESC1 swapDesc = { };
    HRESULT hr = swapChain_->GetDesc1(&swapDesc);
    ENGINE_ASSERT(SUCCEEDED(hr));

    // 本体は描画先の状態で作り、フレームの間はずっと描画先の状態で置いておく
    D3D12_RESOURCE_DESC desc = graphics::pipelineHelper::MakeRenderTextureDesc(swapDesc.Width, swapDesc.Height, kSceneColorFormat);
    D3D12_CLEAR_VALUE clearValue = { };
    clearValue.Format = kSceneColorFormat;
    std::copy(std::begin(kSceneColorClearColor), std::end(kSceneColorClearColor), clearValue.Color);
    sceneColorResource_.Reset();
    sceneColorResource_ = graphics::pipelineHelper::CreateDefaultResource(device_.Get(), desc,
        D3D12_RESOURCE_STATE_RENDER_TARGET, &clearValue);
    sceneColorResource_->SetName(L"SceneColor");

    // RTV と、写すパスで読む SRV のヒープは最初の1回だけ作り、作り直す時は中身だけ書き直す
    if (!sceneColorRtvHeap_) {
        sceneColorRtvHeap_ = CreateDescriptorHeap(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
    }
    if (!presentSrvHeap_) {
        presentSrvHeap_ = CreateDescriptorHeap(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kPresentSrvCount, true);
    }
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = { };
    rtvDesc.Format = kSceneColorFormat;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    device_->CreateRenderTargetView(sceneColorResource_.Get(), &rtvDesc, sceneColorRtvHeap_->GetCPUDescriptorHandleForHeapStart());

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = { };
    srvDesc.Format = kSceneColorFormat;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    device_->CreateShaderResourceView(sceneColorResource_.Get(), &srvDesc, presentSrvHeap_->GetCPUDescriptorHandleForHeapStart());
}

/** @brief シーンの色バッファをスワップチェーンへ写すパイプラインを作る */
void DirectXCommon::CreatePresentPipeline() {
    // t0 の色バッファを1枚読むだけ（同じ大きさなので画素の番号で直接読み、サンプラーは使わない）
    const D3D12_DESCRIPTOR_RANGE range = graphics::pipelineHelper::MakeSrvRange(0);
    const D3D12_ROOT_PARAMETER parameters[] = {
        graphics::pipelineHelper::MakeSrvTableParam(&range, D3D12_SHADER_VISIBILITY_PIXEL),
    };
    presentRootSignature_ = graphics::pipelineHelper::CreateRootSignature(device_.Get(), parameters,
        static_cast<UINT>(std::size(parameters)), nullptr, 0, D3D12_ROOT_SIGNATURE_FLAG_NONE);
    ENGINE_ASSERT(presentRootSignature_);

    // 描き込み先はスワップチェーンの SRGB の形式（線形の値を書けば SRGB へ変換して保存される）
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = graphics::pipelineHelper::MakeFullscreenPsoDesc();
    desc.pRootSignature = presentRootSignature_.Get();
    desc.RTVFormats[0] = kSwapChainRtvFormat;
    presentPipeline_ = graphics::pipelineHelper::CreateGraphicsPipeline(this, desc,
        L"Resources/shaders/postprocess/FullscreenVS.hlsl", L"Resources/shaders/postprocess/PresentPS.hlsl");
}

/** @brief シーンの色バッファをスワップチェーンへ写し、スワップチェーンを画面へ出す状態にする */
void DirectXCommon::CopySceneColorToSwapChain(UINT backBufferIndex) {
    ID3D12Resource* swapChainBuffer = swapChainResources_[backBufferIndex].Get();
    ID3D12GraphicsCommandList* cmd = commandList_.Get();

    // 色バッファは読む状態へ、スワップチェーンは描画先の状態へ
    constexpr UINT kBarrierCount = 2;
    const D3D12_RESOURCE_BARRIER toCopy[kBarrierCount] = {
        MakeTransitionBarrier(sceneColorResource_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
        MakeTransitionBarrier(swapChainBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET),
    };
    cmd->ResourceBarrier(kBarrierCount, toCopy);

    // スワップチェーン全体へ全画面三角形を1枚描く
    DXGI_SWAP_CHAIN_DESC1 swapDesc = { };
    swapChain_->GetDesc1(&swapDesc);
    const D3D12_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>(swapDesc.Width), static_cast<float>(swapDesc.Height), 0.0f, 1.0f };
    const D3D12_RECT scissor = { 0, 0, static_cast<LONG>(swapDesc.Width), static_cast<LONG>(swapDesc.Height) };
    cmd->OMSetRenderTargets(1, &rtvHandles_[backBufferIndex], FALSE, nullptr);
    graphics::pipelineHelper::SetViewportAndScissor(cmd, viewport, scissor);
    // このパスだけ専用の SRV ヒープを使う（次のフレームは SrvManager::PreDraw がいつものヒープに戻す）
    ID3D12DescriptorHeap* heaps[] = { presentSrvHeap_.Get() };
    cmd->SetDescriptorHeaps(1, heaps);
    graphics::pipelineHelper::SetTrianglePipeline(cmd, presentRootSignature_.Get(), presentPipeline_.Get());
    cmd->SetGraphicsRootDescriptorTable(kPresentSceneColorSlot, presentSrvHeap_->GetGPUDescriptorHandleForHeapStart());
    graphics::pipelineHelper::DrawFullscreenTriangle(cmd);

    // 色バッファは次のフレームのために描画先の状態へ戻し、スワップチェーンは画面へ出す状態にする
    const D3D12_RESOURCE_BARRIER toPresent[kBarrierCount] = {
        MakeTransitionBarrier(sceneColorResource_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
        MakeTransitionBarrier(swapChainBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT),
    };
    cmd->ResourceBarrier(kBarrierCount, toPresent);
}

} // namespace engine

/**
 * @file DirectXCommon.cpp
 * @brief DirectXCommonの初期化・終了処理、フレーム制御（PreDraw/PostDraw）、リソースバリア・GPU同期の実装
 * @note デバイス・コマンド・スワップチェーン等の生成処理はDirectXCommonInit.cppに分割されている
 */
#include "DirectXCommon.h"
#include "PipelineHelper.h"
#include "AssetPack.h"
#include "EngineAssert.h"
#include "EngineConstants.h"
#include "FrameProfiler.h"
#include "Input.h"
#include "Logger.h"
#include "StringUtility.h"
#include "WinApp.h"
#include <format>
#include <string>

using Microsoft::WRL::ComPtr;

namespace engine {
namespace {

// 状態をリソース自身へ保存し、解放後に同じアドレスが再利用されても
// 古い追跡情報を引き継がないようにする
constexpr GUID kTrackedResourceStateGuid = {
    0x56f0ea61, 0x3f6b, 0x4f54, { 0xa8, 0x91, 0xc8, 0x15, 0x29, 0x6f, 0x6e, 0x31 }
};

/** @brief リソースに記録しておいた現在のリソース状態を読み出す（記録が無ければ false） */
bool ReadTrackedState(ID3D12Resource* resource, D3D12_RESOURCE_STATES& state) {
    UINT size = sizeof(state);
    return resource
        && SUCCEEDED(resource->GetPrivateData(kTrackedResourceStateGuid, &size, &state))
        && size == sizeof(state);
}

/** @brief リソースの現在のリソース状態をリソース自身へ記録する */
void WriteTrackedState(ID3D12Resource* resource, D3D12_RESOURCE_STATES state) {
    if (resource) {
        resource->SetPrivateData(kTrackedResourceStateGuid, sizeof(state), &state);
    }
}

} // namespace

// Initialize関数
// ══════════════════════════════════════════════════════
// 初期化とフレーム制御
// ══════════════════════════════════════════════════════

/** @brief FPS固定・デバイス・コマンド・スワップチェーンなど DirectX12 の基盤を順に初期化する */
void DirectXCommon::Initialize(WinApp* winApp) {
    ENGINE_ASSERT(winApp);
    winApp_ = winApp;

    // FPS固定初期化
    InitializeFixFPS();
    // デバイス初期化
    InitializeDevice();
    frameUploadAllocator_.Initialize(this, kFrameCount);
    bundleRecorder_.Initialize(device_.Get(), kFrameCount);
    // DeferredComPtr が消える時の預け先にする
    DeferredReleaseQueue::SetCurrent(&deferredReleaseQueue_);
    // コマンド関連初期化
    CreateCommand();
    // ディスクリプタヒープ作成
    CreateDescriptorHeaps();
    // スワップチェーン作成
    CreateSwapChain();
    // レンダーターゲットビュー作成
    CreateRTV();
    // シーンを描く色バッファ作成
    CreateSceneColorBuffer();
    // 深度バッファ作成
    CreateDepthBuffer();
    CreateBackBufferDepthBuffer();
    // フェンス作成
    CreateFence();
    // DXCコンパイラ初期化
    InitializeDXC();
    // パイプラインの保存と再利用を始める（これより後に作るパイプラインはすべてここを通る）
    graphics::PipelineCache::GetInstance()->Initialize(this);
    // 色バッファをスワップチェーンへ写すパイプライン作成（シェーダーを使うのでコンパイラの後）
    CreatePresentPipeline();
}

/** @brief DirectX12基盤の終了処理 */
void DirectXCommon::Finalize() {
    // GPU の処理がすべて終わるまで待つ
    WaitForGpu();
    // もう命令は残っていないので、預かっていた資源を全部手放し、以後は預けずにその場で手放させる
    deferredReleaseQueue_.ReleaseAll();
    DeferredReleaseQueue::SetCurrent(nullptr);
    // このプレイで新しく作ったパイプラインをファイルへ保存し、覚えていたパイプラインを手放す
    graphics::PipelineCache::GetInstance()->Finalize();

#ifdef _DEBUG
    if (infoQueue_ && diagnosticsCallbackCookie_ != 0) {
        infoQueue_->UnregisterMessageCallback(diagnosticsCallbackCookie_);
        diagnosticsCallbackCookie_ = 0;
    }
#endif

    // スワップチェーンのフルスクリーン解除（解放前に必須）
    BOOL fullscreen = FALSE;

    if (swapChain_) {
        swapChain_->GetFullscreenState(&fullscreen, nullptr);

        if (fullscreen) {
            swapChain_->SetFullscreenState(FALSE, nullptr);
        }
    }

    // イベントハンドルを閉じる
    if (fenceEvent_) {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }
}

/** @brief 描画前処理 */
void DirectXCommon::PreDraw() {
    UINT backBufferIndex = swapChain_->GetCurrentBackBufferIndex();

    // このバックバッファ用のアロケータをGPUが使い終えるまで待つ（前回このインデックスを使った時のフェンス値まで）
    // 毎フレーム無条件に全待ちする代わりに、実際に必要な時だけ止めることでCPU/GPUを並行して動かす
    if (frameFenceValues_[backBufferIndex] != 0
        && fence_->GetCompletedValue() < frameFenceValues_[backBufferIndex]) {
        fence_->SetEventOnCompletion(frameFenceValues_[backBufferIndex], fenceEvent_);
        const graphics::ScopedFramePhase phase(graphics::FramePhase::kGpuWait);
        WaitForSingleObject(fenceEvent_, INFINITE);
    }

    // GPU が使い終えたフレームで捨てられた資源を、ここで本当に手放す
    deferredReleaseQueue_.ReleaseCompleted(fence_->GetCompletedValue());

    // GPUの使用が完了した定数領域を再利用する。
    frameUploadAllocator_.BeginFrame(backBufferIndex);
    bundleRecorder_.BeginFrame(backBufferIndex);

    // コマンドアロケータとリストをリセット
    HRESULT hr = commandAllocators_[backBufferIndex]->Reset();
    ENGINE_ASSERT(SUCCEEDED(hr));
    hr = commandList_->Reset(commandAllocators_[backBufferIndex].Get(), nullptr);
    ENGINE_ASSERT(SUCCEEDED(hr));

    // 描画先はシーンの色バッファ（いつも描画先の状態で置いてあるのでバリアは要らない）と深度
    const D3D12_CPU_DESCRIPTOR_HANDLE sceneColorRtv = GetCurrentBackBufferHandle();
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = GetBackBufferDsvHandle();
    commandList_->OMSetRenderTargets(1, &sceneColorRtv, false, &dsvHandle);
    SetActiveDepthTarget(DepthTarget::kBackBuffer);

    // 縦横比が異なる画面のレターボックス部分は黒でクリアする。
    static constexpr float kClearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    commandList_->ClearRenderTargetView(sceneColorRtv, kClearColor, 0, nullptr);

    // 深度クリア
    commandList_->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    commandList_->ClearDepthStencilView(GetDsvHandle(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    // ビューポートとシザー矩形の設定
    // 縦横比を保ち、バックバッファに収まる最大サイズで描画する。
    D3D12_VIEWPORT viewport = GetCenteredClientViewport();
    commandList_->RSSetViewports(1, &viewport);

    D3D12_RECT scissorRect = GetCenteredClientScissorRect();
    commandList_->RSSetScissorRects(1, &scissorRect);
}

/** @brief 描画後処理 */
void DirectXCommon::PostDraw() {
    HRESULT hr;

    // 描き終えた色バッファをスワップチェーンへ写し、画面へ出す状態にする
    UINT backBufferIndex = swapChain_->GetCurrentBackBufferIndex();
    CopySceneColorToSwapChain(backBufferIndex);

    // コマンドリストを閉じる
    hr = commandList_->Close();
    ENGINE_ASSERT(SUCCEEDED(hr));

    // GPUコマンド実行（FPS待機より先に投入してGPUを遊ばせない）
    ID3D12CommandList* commandLists[] = { commandList_.Get() };
    commandQueue_->ExecuteCommandLists(1, commandLists);

    // このフレームで使ったアロケータの完了目印としてフェンス値を積む
    // （ここでは待たない。実際に待つのは、次にこのバックバッファを使い回す時＝PreDraw側）
    ++fenceValue_;
    hr = commandQueue_->Signal(fence_.Get(), fenceValue_);
    if (FAILED(hr)) {
        const HRESULT reason = device_->GetDeviceRemovedReason();
        Logger::LogError(std::format(
            "GPU command submission failed. context={} signal=0x{:08X} reason=0x{:08X}",
            diagnosticContext_, static_cast<unsigned long>(hr), static_cast<unsigned long>(reason)));
    }
    ENGINE_ASSERT(SUCCEEDED(hr));
    frameFenceValues_[backBufferIndex] = fenceValue_;
    // このフレームに捨てられた資源は、GPU がこの値まで進んだら手放す
    deferredReleaseQueue_.EndFrame(fenceValue_);

    // 頼まれていれば、描き終えた絵を画面へ出す前に写し取って保存する
    CaptureRequestedScreenshot(backBufferIndex);

    // フリップ (画面更新)
    // VSyncオフ時はSyncInterval=0。ティアリング許可済みならALLOW_TEARINGを付けて真の非同期表示にする
    const UINT syncInterval = vsyncEnabled_ ? 1 : 0;
    const UINT presentFlags = (!vsyncEnabled_ && tearingSupported_) ? DXGI_PRESENT_ALLOW_TEARING : 0;
    {
        const graphics::ScopedFramePhase phase(graphics::FramePhase::kPresent);
        hr = swapChain_->Present(syncInterval, presentFlags);
    }

    if (FAILED(hr)) {
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            HRESULT reason = device_->GetDeviceRemovedReason();
            Logger::LogError(std::format(
                "GPU device removed! Present hr=0x{:08X} reason=0x{:08X} "
                "(ドライバクラッシュ・TDR・GPUの物理的な切断などが考えられます)",
                static_cast<unsigned long>(hr), static_cast<unsigned long>(reason)));
        }
        ENGINE_ASSERT(SUCCEEDED(hr));
    }

    // FPS固定（GPU が動いている間に CPU 側で余った時間を使って待機）。速回しの時は待たない
    if (frameLimitEnabled_) {
        UpdateFixFPS();
    }
}



// ══════════════════════════════════════════════════════
// リサイズと同期
// ══════════════════════════════════════════════════════

/** @brief ウィンドウサイズの変更に合わせてスワップチェーンのバックバッファを作り直す（最小化中は何もしない） */
void DirectXCommon::OnResize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0 || !swapChain_) {
        return;
    } // 最小化中などは無視

    DXGI_SWAP_CHAIN_DESC1 desc = { };
    swapChain_->GetDesc1(&desc);
    if (desc.Width == width && desc.Height == height) {
        return;
    }

    // GPUがバックバッファの参照を終えるまで待ってから解放する
    WaitForGpu();
    swapChainResources_[0].Reset();
    swapChainResources_[1].Reset();

    // フォーマットは DXGI_FORMAT_UNKNOWN を渡して既存のもの（作成時のUNORM）を維持する
    // Flagsは作成時（tearingSupported_に応じたALLOW_TEARING）と同じ値にする必要がある
    const UINT flags = tearingSupported_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    HRESULT hr = swapChain_->ResizeBuffers(2, width, height, DXGI_FORMAT_UNKNOWN, flags);
    ENGINE_ASSERT(SUCCEEDED(hr));

    CreateRTV();
    CreateSceneColorBuffer();
    CreateBackBufferDepthBuffer();
}

/** @brief 内部解像度(WinApp::kClientWidth/Height)の絵を実際のバックバッファへ */
D3D12_VIEWPORT DirectXCommon::GetBackBufferViewport() const {
    uint32_t backBufferWidth = winApp_->kClientWidth;
    uint32_t backBufferHeight = winApp_->kClientHeight;
    if (swapChain_) {
        DXGI_SWAP_CHAIN_DESC1 desc = { };
        if (SUCCEEDED(swapChain_->GetDesc1(&desc)) && desc.Width > 0 && desc.Height > 0) {
            backBufferWidth = desc.Width;
            backBufferHeight = desc.Height;
        }
    }

    // 内部解像度のアスペクト比を保ったまま、バックバッファ内に収まる最大サイズへ拡大する
    const float clientAspect = static_cast<float>(winApp_->kClientWidth) / static_cast<float>(winApp_->kClientHeight);
    const float backAspect = static_cast<float>(backBufferWidth) / static_cast<float>(backBufferHeight);

    float width = static_cast<float>(backBufferWidth);
    float height = static_cast<float>(backBufferHeight);
    if (backAspect > clientAspect) {
        // バックバッファの方が横長 → 左右に余白（ピラーボックス）
        width = height * clientAspect;
    } else if (backAspect < clientAspect) {
        // バックバッファの方が縦長 → 上下に余白（レターボックス）
        height = width / clientAspect;
    }

    D3D12_VIEWPORT viewport = { };
    viewport.TopLeftX = (static_cast<float>(backBufferWidth) - width) * 0.5f;
    viewport.TopLeftY = (static_cast<float>(backBufferHeight) - height) * 0.5f;
    viewport.Width = width;
    viewport.Height = height;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    return viewport;
}

/** @brief GetBackBufferViewport() に対応するシザー矩形を取得する */
D3D12_RECT DirectXCommon::GetBackBufferScissorRect() const {
    const D3D12_VIEWPORT viewport = GetBackBufferViewport();
    D3D12_RECT rect = { };
    rect.left = static_cast<LONG>(viewport.TopLeftX);
    rect.top = static_cast<LONG>(viewport.TopLeftY);
    rect.right = static_cast<LONG>(viewport.TopLeftX + viewport.Width);
    rect.bottom = static_cast<LONG>(viewport.TopLeftY + viewport.Height);
    return rect;
}

/** @brief 縦横比を保って画面内の最大サイズへ拡大し、中央に配置するビューポートを取得する。 */
D3D12_VIEWPORT DirectXCommon::GetCenteredClientViewport() const {
    return GetBackBufferViewport();
}

/** @brief GetCenteredClientViewport() に対応するシザー矩形を取得する */
D3D12_RECT DirectXCommon::GetCenteredClientScissorRect() const {
    return GetBackBufferScissorRect();
}

/** @brief バックバッファの RTV と深度を描画先にし、中央寄せのビューポートとシザーを設定する */
void DirectXCommon::SetBackBufferRenderTarget() {
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = GetCurrentBackBufferHandle();
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = GetBackBufferDsvHandle();
    commandList_->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    SetActiveDepthTarget(DepthTarget::kBackBuffer);
    const D3D12_VIEWPORT viewport = GetCenteredClientViewport();
    const D3D12_RECT scissor = GetCenteredClientScissorRect();
    commandList_->RSSetViewports(1, &viewport);
    commandList_->RSSetScissorRects(1, &scissor);
}


/** @brief 今の描画先（シーンの色バッファ）の RTV ハンドルを取得 */
D3D12_CPU_DESCRIPTOR_HANDLE DirectXCommon::GetCurrentBackBufferHandle() {
    return sceneColorRtvHeap_->GetCPUDescriptorHandleForHeapStart();
}

/** @brief 今の描画先（シーンの色バッファ）のリソースを取得 */
ID3D12Resource* DirectXCommon::GetCurrentBackBufferResource() {
    return sceneColorResource_.Get();
}

// ヘルパー関数（バッファ作成用）
// ══════════════════════════════════════════════════════
// 共通リソース操作
// ══════════════════════════════════════════════════════

/** @brief 指定サイズのアップロード用バッファ（定数バッファ等）を生成する */
Microsoft::WRL::ComPtr<ID3D12Resource> DirectXCommon::CreateBufferResource(size_t sizeInBytes) {
    return graphics::pipelineHelper::CreateUploadBuffer(device_.Get(), sizeInBytes);
}

/** @brief 任意のコマンドキュー・フェンスに対してフェンス値を積み、GPU完了まで待機する */
void DirectXCommon::WaitForFence(
    ID3D12CommandQueue* queue, ID3D12Fence* fence, UINT64& fenceValue, HANDLE fenceEvent) {
    ++fenceValue;
    queue->Signal(fence, fenceValue);

    if (fence->GetCompletedValue() < fenceValue) {
        fence->SetEventOnCompletion(fenceValue, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }
}

/** @brief コマンドキューにフェンス値を積み、GPUがそこまでの処理を完了するまでCPU側で待機する */
void DirectXCommon::WaitForGpu() {
    WaitForFence(commandQueue_.Get(), fence_.Get(), fenceValue_, fenceEvent_);
    // 出した命令は全部終わったので、フェンスの値を付けて待たせていた物を手放す
    // （このフレームに捨てられ、まだ値の付いていない物は、記録中の命令が使うかもしれないので残す）
    deferredReleaseQueue_.ReleaseCompleted(fence_->GetCompletedValue());
}

/** @brief リソースの状態遷移バリア（TYPE_TRANSITION）を構築するだけで発行はしない */
D3D12_RESOURCE_BARRIER DirectXCommon::MakeTransitionBarrier(
    ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after, UINT subresource) {
    ENGINE_ASSERT(resource);

    // 全サブリソース遷移だけを追跡する
    // 個別Mipの状態は一つの値で表せないため呼び出し側の指定をそのまま使う
    if (subresource == D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) {
        D3D12_RESOURCE_STATES trackedState;
        if (ReadTrackedState(resource, trackedState) && trackedState != before) {
            Logger::LogWarning(std::format(
                "Resource state mismatch. requested_before=0x{:X} tracked_before=0x{:X} after=0x{:X}",
                static_cast<unsigned>(before), static_cast<unsigned>(trackedState), static_cast<unsigned>(after)));

            // 既に目的状態なら同一状態バリアを避けるため呼び出し側の値を残す
            // それ以外は追跡済みの実状態から遷移する
            if (trackedState != after) {
                before = trackedState;
            }
        }
        WriteTrackedState(resource, after);
    }

    D3D12_RESOURCE_BARRIER barrier = { };
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = subresource;
    return barrier;
}

/** @brief リソースの状態遷移バリアを構築し、その場で1つだけ発行する */
void DirectXCommon::TransitionBarrier(
    ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after, UINT subresource) {
    D3D12_RESOURCE_BARRIER barrier = MakeTransitionBarrier(resource, before, after, subresource);
    commandList->ResourceBarrier(1, &barrier);
}

/** @brief 単機能クラス専用のデスクリプタヒープ（RTV/DSV等）を生成する */
Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> DirectXCommon::CreateDescriptorHeap(
    ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT numDescriptors, bool shaderVisible) {
    D3D12_DESCRIPTOR_HEAP_DESC desc = { };
    desc.Type = type;
    desc.NumDescriptors = numDescriptors;
    desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
    HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap));
    ENGINE_ASSERT(SUCCEEDED(hr));
    return heap;
}

} // namespace engine

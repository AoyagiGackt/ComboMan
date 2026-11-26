/**
 * @file DirectXCommon.h
 * @brief DirectX12のデバイス生成、コマンド管理、スワップチェーンなどの基盤機能を管理するファイル
 */
#pragma once
#include <Windows.h>
#include "DeferredReleaseQueue.h"
#include "FrameUploadAllocator.h"
#include "ParallelBundleRecorder.h"
#include "ResourceStateTracker.h"
#include <cassert>
#include <chrono>
#include <d3d12.h>
#include <d3dx12.h>
#include <dxcapi.h>
#include <dxgi1_6.h>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <wrl/client.h>
namespace engine {
class WinApp;

/**
 * @brief DirectX12の共通基盤クラス
 * @note アプリケーション全体で共有されるDirectX12の基本オブジェクト（Device, CommandList等）を保持します
 * 描画の開始(PreDraw)と終了(PostDraw)の管理、およびFPSの固定化機能も備えています
 */
class DirectXCommon {
public: // メンバ関数
    /**
     * @brief DirectX12基盤の初期化
     * @param winApp 管理対象となるWinApp（ウィンドウ管理）のポインタ
     */
    void Initialize(WinApp* winApp);

    /**
     * @brief DirectX12基盤の終了処理
     * @note GPU完了を待ち、スワップチェーンのフルスクリーン解除など安全な順序で解放する
     */
    void Finalize();

    /**
     * @brief 描画前処理
     * @note コマンドアロケータとリストのリセット、バックバッファの状態遷移、画面クリアなどを行います
     */
    void PreDraw();
    /** @brief 描画ごとの定数データを書き込む、フレーム別のアップロード領域を取得する */
    FrameUploadAllocator& GetFrameUploadAllocator() {
        return frameUploadAllocator_;
    }
    /** @brief 描画パスの宣言からバリアを自動で入れるための、リソースの状態の記録を取得する */
    ResourceStateTracker& GetResourceStateTracker() {
        return resourceStateTracker_;
    }
    /** @brief 描画の命令を複数のスレッドでバンドルへ記録するための部品を取得する */
    ParallelBundleRecorder& GetBundleRecorder() {
        return bundleRecorder_;
    }
    /** @brief 捨てられた GPU 資源を、GPU が使い終わるまで預かる列を取得する */
    DeferredReleaseQueue& GetDeferredReleaseQueue() {
        return deferredReleaseQueue_;
    }
    /** @brief メインのスレッドで記録しているコマンドリストを返す（バンドルの記録中でもこちらを返す） */
    ID3D12GraphicsCommandList* GetMainCommandList() {
        return commandList_.Get();
    }

    /**
     * @brief 描画後処理
     * @note バックバッファをPresentation状態に戻し、コマンドの実行、画面の入れ替え（Flip）、FPS固定の待ちを行います
     */
    void PostDraw();

    /**
     * @brief ウィンドウサイズ変更に合わせてスワップチェーンのバックバッファを作り直す
     * @param width  新しいクライアント領域の幅
     * @param height 新しいクライアント領域の高さ
     * @note ゲーム内部の描画解像度（ビューポート・UI座標等）は WinApp::kClientWidth/Height の
     * 固定値のまま変えない。ウィンドウが大きければ余白は黒のまま、小さければ描画がクリップされる。
     * あくまでリサイズしてもクラッシュ・表示崩壊しないことを保証するための対応
     */
    void OnResize(uint32_t width, uint32_t height);

    /**
     * @brief 内部解像度(WinApp::kClientWidth/Height)の絵を実際のバックバッファへ
     * アスペクト比を保ったまま最大表示するためのビューポートを取得する（レターボックス）
     * @note フルスクリーンやウィンドウリサイズでバックバッファが内部解像度と異なるサイズに
     * なった場合でも、ゲーム画面が中央に拡大表示されるようにするためのもの
     * 直接描画で深度を使う場合はGetBackBufferDsvHandle()と組み合わせる。
     * 固定解像度のGetDsvHandle()はオフスクリーン描画用。
     */
    D3D12_VIEWPORT GetBackBufferViewport() const;

    /** @brief GetBackBufferViewport() に対応するシザー矩形を取得する */
    D3D12_RECT GetBackBufferScissorRect() const;

    /**
     * @brief 縦横比を保って画面内の最大サイズへ拡大し、中央に配置するビューポートを取得する。
     * @note GetBackBufferViewport()と同じ領域。直接描画にはGetBackBufferDsvHandle()を使う。
     */
    D3D12_VIEWPORT GetCenteredClientViewport() const;

    /** @brief GetCenteredClientViewport() に対応するシザー矩形を取得する */
    D3D12_RECT GetCenteredClientScissorRect() const;

    /** @brief バックバッファの RTV と深度を描画先にし、中央寄せのビューポートとシザーを設定する（影のパスなどの後に戻す時に使う） */
    void SetBackBufferRenderTarget();

    /** @brief VSyncの有効/無効を設定する */
    void SetVSyncEnabled(bool enabled) {
        vsyncEnabled_ = enabled;
    }
    /** @brief VSyncが有効かどうかを返す */
    bool IsVSyncEnabled() const {
        return vsyncEnabled_;
    }
    /** @brief VSyncの有効/無効を切り替える */
    void ToggleVSync() {
        vsyncEnabled_ = !vsyncEnabled_;
    }
    /**
     * @brief 1秒に目標のフレーム数より速く進まないよう待つかを設定する
     * @note 自動テストを速く回す時だけ false にする（VSync を切っても、これが有効なら目標の速さで待つ）
     */
    void SetFrameLimitEnabled(bool enabled) {
        frameLimitEnabled_ = enabled;
    }

    /**
     * @brief GPU診断ログへ添える現在の処理名を設定する
     * @param context シーン名や描画パス名などの短い識別名
     */
    void SetDiagnosticContext(const std::string& context) {
        diagnosticContext_ = context;
    }

    /** @brief デバッグレイヤーの知らせ1件を受け取る関数（isError は ERROR か CORRUPTION の時 true） */
    using DebugMessageHandler = std::function<void(bool isError, int id, const std::string& description)>;
    /**
     * @brief デバッグレイヤーの警告とエラーを受け取る関数を登録する（Debug ビルドの時だけ届く。空の関数で外す）
     * @param handler 受け取る関数
     */
    void SetDebugMessageHandler(DebugMessageHandler handler);
    /**
     * @brief デバッグレイヤーのエラーで止めるか（ふだんは止める。自動テストでは止めずに記録して先へ進む）
     * @param enabled 止めるなら true
     */
    void SetBreakOnDebugErrors(bool enabled);
    /**
     * @brief 次に画面へ出す絵を PNG で保存するよう頼む（PostDraw の中で、描き終えて画面へ出す前に写し取る）
     * @param path 保存する場所
     * @note 写し取る時に GPU を待つので、自動テストで不具合を見つけた時など、たまにだけ使う
     */
    void RequestScreenshot(const std::wstring& path) {
        screenshotPath_ = path;
    }
    /** @brief このアプリが今使っている GPU のメモリの量（バイト。取れなければ 0） */
    uint64_t GetGpuMemoryUsage() const;

    /**
     * @brief シェーダーファイルをコンパイルする
     * @param filePath シェーダーファイルのパス
     * @param profile コンパイルプロファイル（例: L"vs_6_0", L"ps_6_0"）
     * @return コンパイルされたシェーダーデータ
     */
    Microsoft::WRL::ComPtr<IDxcBlob> CompileShader(const std::wstring& filePath, const wchar_t* profile);

    /**
     * @brief pak とキャッシュを通さずにファイルからコンパイルし直す（シェーダーのホットリロード用）
     * @return 成功したらコンパイル結果（キャッシュも置き換える）。エラーや警告が出たらログに出して nullptr
     */
    Microsoft::WRL::ComPtr<IDxcBlob> CompileShaderFromSource(const std::wstring& filePath, const wchar_t* profile);

    /**
     * @brief バイトコードの先頭のアドレスから、それをコンパイルしたファイルとプロファイルを引く
     * @return CompileShader か CompileShaderFromSource で作ったバイトコードなら true
     */
    bool FindShaderSource(const void* bytecode, std::wstring& outPath, std::wstring& outProfile) const;

    /** @brief デバイスの取得 */
    ID3D12Device* GetDevice() {
        return device_.Get();
    }

    /** @brief グラフィックスコマンドリストの取得 */
    ID3D12GraphicsCommandList* GetCommandList() {
        // 描画を複数のスレッドでバンドルへ記録している間は、そのスレッドのバンドルへ積ませる
        if (ID3D12GraphicsCommandList* bundle = ParallelBundleRecorder::ThreadCommandList()) {
            return bundle;
        }
        return commandList_.Get();
    }

    /** @brief コマンドキューの取得 */
    ID3D12CommandQueue* GetCommandQueue() {
        return commandQueue_.Get();
    }

    /** @brief スワップチェーンの取得 */
    IDXGISwapChain4* GetSwapChain() {
        return swapChain_.Get();
    }

    /** @brief 現在のバックバッファ用コマンドアロケータの取得 */
    ID3D12CommandAllocator* GetCommandAllocator() {
        return commandAllocators_[swapChain_->GetCurrentBackBufferIndex()].Get();
    }

    /** @brief SRV用デスクリプタヒープの取得 */
    ID3D12DescriptorHeap* GetSrvDescriptorHeap() {
        return srvDescriptorHeap_.Get();
    }

    /**
     * @brief バッファリソース（定数バッファ等）を生成する
     * @param sizeInBytes 生成するバッファのサイズ
     * @return Microsoft::WRL::ComPtr<ID3D12Resource> 生成されたリソース
     */
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(size_t sizeInBytes);

    /**
     * @brief 単機能クラス専用のデスクリプタヒープ（RTV/DSV等）を生成する
     * @param device         生成に使用するデバイス
     * @param type           ヒープの種類（D3D12_DESCRIPTOR_HEAP_TYPE_RTV等）
     * @param numDescriptors 確保するディスクリプタ数
     * @param shaderVisible  シェーダーから参照する必要があるか（RTV/DSVは通常false）
     * @return Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> 生成されたヒープ
     * @note staticなので、DirectXCommonのインスタンスを持たずID3D12Device*しか無い箇所からも呼べる
     */
    static Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(
        ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT numDescriptors, bool shaderVisible = false);

    /**
     * @brief コマンドキューにフェンス値を積み、GPUがそこまでの処理を完了するまでCPU側で待機する
     * @note このDirectXCommonが持つグラフィックスキュー・フェンスに対して待機する
     */
    void WaitForGpu();

    /**
     * @brief 任意のコマンドキュー・フェンスに対してフェンス値を積み、GPU完了まで待機する
     * @param queue      待機対象のコマンドキュー
     * @param fence      待機対象のフェンス
     * @param fenceValue 直近で使ったフェンス値（呼び出しごとにインクリメントされる）
     * @param fenceEvent 完了通知に使うイベントハンドル
     * @note グラフィックスキュー以外（TextureManagerのコピーキュー等）を待つ場合に使う
     */
    static void WaitForFence(
        ID3D12CommandQueue* queue, ID3D12Fence* fence, UINT64& fenceValue, HANDLE fenceEvent);

    /**
     * @brief リソースの状態遷移バリア（TYPE_TRANSITION）を構築するだけで発行はしない
     * @note 複数バリアをまとめて1回のResourceBarrier()で発行したい場合はこちらを使う
     */
    static D3D12_RESOURCE_BARRIER MakeTransitionBarrier(
        ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after,
        UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

    /**
     * @brief リソースの状態遷移バリアを構築し、その場で1つだけ発行する
     * @param commandList 発行先のコマンドリスト
     * @param resource    遷移させるリソース
     * @param before      遷移前の状態
     * @param after       遷移後の状態
     * @param subresource 対象サブリソース（省略時は全サブリソース）
     */
    static void TransitionBarrier(
        ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource,
        D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after,
        UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

    // RTV関連

    /** @brief シーンを描く色バッファの形式（明るさ 1 を超える値も残せる浮動小数点。描画先にする PSO の形式もこれにそろえる） */
    static constexpr DXGI_FORMAT kSceneColorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    /** @brief スワップチェーンへ描き込む時の形式（フレームの最後に色バッファを写す時だけ使う） */
    static constexpr DXGI_FORMAT kSwapChainRtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

    /**
     * @brief 今の描画先（シーンの色バッファ）の RTV ハンドルを取得
     * @note 画面へ出すスワップチェーンではなく、フレームの最後にスワップチェーンへ写す浮動小数点の色バッファを返す
     */
    D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentBackBufferHandle();

    /** @brief 今の描画先（シーンの色バッファ）のリソースを取得（画面を写し取る演出はここから写す） */
    ID3D12Resource* GetCurrentBackBufferResource();

    /** @brief 描画先（シーンの色バッファ）の形式を取得 */
    DXGI_FORMAT GetBackBufferFormat() const {
        return kSceneColorFormat;
    }

    /** @brief スワップチェーンのバッファ数を取得（ダブルバッファリングなら2） */
    UINT GetBufferCount() const {
        return kFrameCount;
    }

    /** @brief DSV（深度バッファ）のハンドルを取得 */
    D3D12_CPU_DESCRIPTOR_HANDLE GetDsvHandle() {
        return dsvDescriptorHeap_->GetCPUDescriptorHandleForHeapStart();
    }
    /** @brief バックバッファへの直接描画用深度ビューを取得する */
    D3D12_CPU_DESCRIPTOR_HANDLE GetBackBufferDsvHandle() {
        return backBufferDsvHeap_->GetCPUDescriptorHandleForHeapStart();
    }

    /** @brief 深度ステンシルリソースを取得（SRV生成に使用） */
    ID3D12Resource* GetDepthStencilResource() const {
        return depthStencilResource_.Get();
    }

    /** @brief 描画先にした深度バッファの種類 */
    enum class DepthTarget {
        kBackBuffer, ///< バックバッファへ直接描く時の深度（GetBackBufferDsvHandle）
        kScene, ///< 固定解像度のオフスクリーン描画の深度（GetDsvHandle）
    };
    /** @brief どちらの深度バッファを描画先にしたかを覚える（深度を描画先に設定する所で呼ぶ） */
    void SetActiveDepthTarget(DepthTarget target) {
        activeDepthTarget_ = target;
    }
    /** @brief 今描画先にしている深度バッファの種類 */
    DepthTarget GetActiveDepthTarget() const {
        return activeDepthTarget_;
    }
    /** @brief 今描画先にしている深度バッファ（ソフトパーティクルが描き終えた物の深度を写し取るのに使う） */
    ID3D12Resource* GetActiveDepthResource() const {
        return activeDepthTarget_ == DepthTarget::kBackBuffer ? backBufferDepthResource_.Get() : depthStencilResource_.Get();
    }

    // フェンス関連

    /** @brief フェンスの取得 */
    ID3D12Fence* GetFence() {
        return fence_.Get();
    }

    /** @brief 現在のフェンス値を取得 */
    uint64_t GetFenceValue() const {
        return fenceValue_;
    }

    /** @brief フェンスイベントのハンドルを取得 */
    HANDLE GetFenceEvent() {
        return fenceEvent_;
    }

    /** @brief フェンス値をインクリメントする */
    void IncrementFenceValue() {
        fenceValue_++;
    }

private:
    // 内部初期化関数群

    // デバイスとファクトリの生成
    void InitializeDevice();

    // コマンドキュー・アロケータ・リストの生成
    void CreateCommand();

    // スワップチェーンの生成
    void CreateSwapChain();

    // 各種デスクリプタヒープの生成
    void CreateDescriptorHeaps();

    // レンダーターゲットビューの生成
    void CreateRTV();
    /** @brief スワップチェーンと同じ大きさのシーンの色バッファと、その RTV・SRV を作る（大きさが変わった時も呼ぶ） */
    void CreateSceneColorBuffer();
    /** @brief シーンの色バッファをスワップチェーンへ写すパイプラインを作る */
    void CreatePresentPipeline();
    /**
     * @brief シーンの色バッファをスワップチェーンへ写し、スワップチェーンを画面へ出す状態にする
     * @param backBufferIndex 写し先のスワップチェーンの番号
     */
    void CopySceneColorToSwapChain(UINT backBufferIndex);

    // 深度バッファリソースとDSVの生成
    void CreateDepthBuffer();
    /** @brief バックバッファへ直接描く時の深度バッファを作る */
    void CreateBackBufferDepthBuffer();

    // GPU同期用フェンスの生成
    void CreateFence();

    // DXCシェーダーコンパイラの初期化
    void InitializeDXC();
    /** @brief ファイルを読んでコンパイルする（読めない・エラーや警告が出た時はログに出して nullptr を返す） */
    Microsoft::WRL::ComPtr<IDxcBlob> CompileShaderSource(const std::wstring& filePath, const wchar_t* profile);
    /** @brief コンパイルの結果をキャッシュへ入れ、バイトコードから元のファイルを引けるようにする */
    void RememberShader(const std::wstring& cacheKey, const std::wstring& filePath, const wchar_t* profile,
        const Microsoft::WRL::ComPtr<IDxcBlob>& shaderBlob);

    // FPS固定関連
    void InitializeFixFPS(); ///< FPS固定用の参照時間初期化
    void UpdateFixFPS(); ///< 1/60秒に満たない場合に待機する
    /** @brief RequestScreenshot で頼まれていれば、今のバックバッファを PNG で保存する（画面へ出す前に呼ぶ） */
    void CaptureRequestedScreenshot(UINT backBufferIndex);

#ifdef _DEBUG
    /** @brief D3D12デバッグレイヤーのメッセージをエンジンログへ転送する */
    static void CALLBACK DiagnosticsMessageCallback(
        D3D12_MESSAGE_CATEGORY category, D3D12_MESSAGE_SEVERITY severity,
        D3D12_MESSAGE_ID id, LPCSTR description, void* context);
#endif

private:
    /** @brief フレームインフライト数（スワップチェーンのバッファ数と一致させる） */
    static constexpr UINT kFrameCount = 2;

    /** @brief 描画ごとの定数データを書き込む、フレーム別のアップロード領域 */
    FrameUploadAllocator frameUploadAllocator_;
    /** @brief 描画パスの宣言からバリアを自動で入れるための、リソースの状態の記録 */
    ResourceStateTracker resourceStateTracker_;
    /** @brief 描画の命令を複数のスレッドでバンドルへ記録するための部品 */
    ParallelBundleRecorder bundleRecorder_;
    /** @brief 捨てられた GPU 資源を、GPU が使い終わるまで預かる列 */
    DeferredReleaseQueue deferredReleaseQueue_;

    // DirectX12基盤オブジェクト
    Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory_;
    Microsoft::WRL::ComPtr<IDXGIAdapter4> adapter_; ///< 使っている GPU（メモリの量を聞くのに使う）
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;
    /** @brief バックバッファごとに1つずつ持つコマンドアロケータ
     * @note 1つを使い回すと毎フレームGPU完了を待つ必要が生じるため、
     * バッファ数ぶん用意してCPUとGPUを並行して動かせるようにしている */
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocators_[kFrameCount];
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain_;

    // デスクリプタヒープ
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap_;

    // リソース
    Microsoft::WRL::ComPtr<ID3D12Resource> swapChainResources_[2];
    Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> backBufferDepthResource_;
    DepthTarget activeDepthTarget_ = DepthTarget::kBackBuffer; ///< 今描画先にしている深度バッファ
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> backBufferDsvHeap_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;

    // シーンの色バッファ（全部ここへ描き、フレームの最後にスワップチェーンへ写す）
    Microsoft::WRL::ComPtr<ID3D12Resource> sceneColorResource_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> sceneColorRtvHeap_; ///< 色バッファの RTV 1つぶん
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> presentSrvHeap_; ///< 写す時に色バッファを読む SRV 1つぶん（シェーダーから見える）
    Microsoft::WRL::ComPtr<ID3D12RootSignature> presentRootSignature_; ///< 写すパスのルートシグネチャ
    Microsoft::WRL::ComPtr<ID3D12PipelineState> presentPipeline_; ///< 写すパスのパイプライン

    // DXC (Shader Compiler)
    Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils_;
    Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler_;
    Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler_;
    /** @brief 同一プロセス内で再利用するコンパイル済みシェーダー */
    std::unordered_map<std::wstring, Microsoft::WRL::ComPtr<IDxcBlob>> shaderCache_;
    /** @brief コンパイル結果のファイルとプロファイル（バイトコードのアドレスから引く。シェーダーのホットリロードに使う） */
    struct ShaderSource {
        std::wstring path;
        std::wstring profile;
    };
    std::unordered_map<const void*, ShaderSource> shaderSources_;
    /** @brief シーン初期化が並行した場合のシェーダーキャッシュを保護する */
    mutable std::mutex shaderCacheMutex_;

    // 同期・管理用メンバ
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles_[2];
    uint64_t fenceValue_;
    HANDLE fenceEvent_;
    /** @brief バックバッファごとにそのアロケータをGPUが使い終わったフェンス値を記録する
     * @note PreDraw() で該当インデックスのアロケータを使い回す直前にだけこの値を待つことで、
     * 毎フレーム無条件にWaitForGpu()するのを避けている */
    uint64_t frameFenceValues_[kFrameCount] = { };

    // FPS固定用
    std::chrono::steady_clock::time_point reference_;

    WinApp* winApp_ = nullptr;

    // VSync（ティアリング許可）
    bool vsyncEnabled_ = true;
    bool tearingSupported_ = false;
    bool frameLimitEnabled_ = true; ///< 目標のフレーム数より速く進まないよう待つか

    /** @brief GPUエラー発生時に直前の処理を特定するための識別名 */
    std::string diagnosticContext_ = "Engine initialization";
    DebugMessageHandler debugMessageHandler_; ///< デバッグレイヤーの知らせを受け取る関数（空なら呼ばない）
    std::wstring screenshotPath_; ///< 次に保存する画面の場所（空なら保存しない）

#ifdef _DEBUG
    Microsoft::WRL::ComPtr<ID3D12InfoQueue1> infoQueue_;
    DWORD diagnosticsCallbackCookie_ = 0;
#endif
};

} // namespace engine

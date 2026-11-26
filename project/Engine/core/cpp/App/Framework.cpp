/**
 * @file Framework.cpp
 * @brief アプリケーションのメインループ・基盤システム初期化と終了処理（Framework）の実装
 */
#include "Framework.h"
#include "GamepadRumble.h"
#include "AssetPack.h"
#include "AssetReferenceScanner.h"
#include "BloomEffect.h"
#include "CrashBreadcrumbs.h"
#include "EngineContext.h"
#include "FrameProfiler.h"
#include "GrayscaleEffect.h"
#include "HsvFilter.h"
#include "ImageFilter.h"
#include "LightManager.h"
#include "LightingMode.h"
#include "Logger.h"
#include "MaterialManager.h"
#include "MeshManager.h"
#include "ModelCommon.h"
#include "ModelManager.h"
#include "OutlineEffect.h"
#include "ParticleManager.h"
#include "PipelineCache.h"
#include "SrvManager.h"
#include "TextureManager.h"
#include "TimeManager.h"
#include "ToneMapEffect.h"
#include "VignetteEffect.h"
using namespace engine::graphics;

namespace engine {
/** @brief アプリケーションの実行 */
void Framework::Run() {
    const std::wstring commandLine = GetCommandLineW();
    const bool forceCook = commandLine.find(L"--cook-assets") != std::wstring::npos;
    const bool prepareAssets = commandLine.find(L"--prepare-assets") != std::wstring::npos;
    auto* assetPack = AssetPack::GetInstance();
    assetPack->Initialize(forceCook, prepareAssets);
    AssetReferenceScanner::CheckDisk();
    // 変更がなければ、ビルド後の素材準備はウィンドウやGPUを作らず終了する。
    if (prepareAssets && !assetPack->IsCooking()) {
        assetPack->Finalize();
        return;
    }
    isAssetBuildOnly_ = forceCook || prepareAssets;
    Initialize();
    // 素材準備専用の起動では、全シーンを収集してpakを書き出したら終了する。
    if (isAssetBuildOnly_) {
        Finalize();
        return;
    }
    while (true) {
        FrameProfiler::GetInstance()->BeginFrame();
        {
            const ScopedFramePhase phase(FramePhase::kUpdate);
            Update();
        }

        if (IsEndRequest()) {
            break;
        }

        {
            const ScopedFramePhase phase(FramePhase::kDraw);
            Draw();
        }
        FrameProfiler::GetInstance()->EndFrame();
    }

    Finalize();
}

/** @brief システムの初期化 */
void Framework::Initialize() {
    winApp_ = std::make_unique<WinApp>();
    winApp_->Initialize(!isAssetBuildOnly_);

    dxCommon_ = std::make_unique<DirectXCommon>();
    dxCommon_->Initialize(winApp_.get());

    // ウィンドウがリサイズされたらスワップチェーンを追従させる
    winApp_->SetResizeCallback([this](int32_t width, int32_t height) {
        dxCommon_->OnResize(width, height);
    });

    SrvManager::GetInstance()->Initialize(dxCommon_.get());
    GrayscaleEffect::GetInstance()->Initialize(dxCommon_.get(), SrvManager::GetInstance());
    ImageFilter::GetInstance()->Initialize(dxCommon_.get(), SrvManager::GetInstance());
    VignetteEffect::GetInstance()->Initialize(dxCommon_.get());
    HsvFilter::GetInstance()->Initialize(dxCommon_.get(), SrvManager::GetInstance());
    BloomEffect::GetInstance()->Initialize(dxCommon_.get(), SrvManager::GetInstance());
    ToneMapEffect::GetInstance()->Initialize(dxCommon_.get(), SrvManager::GetInstance());
    OutlineEffect::GetInstance()->Initialize(dxCommon_.get());
    TextureManager::GetInstance()->Initialize(dxCommon_.get());
    ParticleManager::GetInstance()->Initialize(dxCommon_.get());

    input_ = std::make_unique<Input>();
    input_->Initialize(winApp_.get());

    audio_ = std::make_unique<Audio>();
    audio_->Initialize();

    imguiManager_ = std::make_unique<ImGuiManager>();
    imguiManager_->Initialize(winApp_.get(), dxCommon_.get());

    // ゲームが GetInstance を直接呼ばずに取り出せるよう、主な仕組みを入口へ登録する
    EngineServices services;
    services.dxCommon = dxCommon_.get();
    services.input = input_.get();
    services.audio = audio_.get();
    services.time = TimeManager::GetInstance();
    services.assetPack = AssetPack::GetInstance();
    services.srvManager = SrvManager::GetInstance();
    services.textures = TextureManager::GetInstance();
    services.models = ModelManager::GetInstance();
    services.particles = ParticleManager::GetInstance();
    services.lights = LightManager::GetInstance();
    EngineContext::Set(services);
}

/** @brief システムの更新処理 */
void Framework::Update() {
    GamepadRumble::GetInstance()->Update(GamepadRumble::RealDeltaSeconds());
    // 直前の出来事の記録が何フレーム目のことか分かるよう、最初にフレームを進める
    CrashBreadcrumbs::NextFrame();
    input_->Update();
    imguiManager_->Begin();

    // F11 でフルスクリーン/ウィンドウ切り替え
    if (input_->TriggerKey(DIK_F11)) {
        winApp_->ToggleFullscreen();
    }

    // シェーダーのファイルが書き換わっていたらパイプラインを作り直す（描く前に入れ替えておく）
    PipelineCache::GetInstance()->CheckHotReload();

    // F10 でVSyncのON/OFF切り替え
    if (input_->TriggerKey(DIK_F10)) {
        dxCommon_->ToggleVSync();
        Logger::LogInfo(dxCommon_->IsVSyncEnabled() ? "VSync: ON" : "VSync: OFF");
    }
}

/** @brief システムの終了処理 */
void Framework::Finalize() {
    GamepadRumble::GetInstance()->Stop();
    // ImGuiの終了
    if (imguiManager_) {
        imguiManager_->Finalize();
    }

    if (videoPlayer_) {
        videoPlayer_->Finalize();
    }

    // シーンの収集後、素材を追加できる間に最終確認と書き出しを行う。
    ModelCommon cookModelCommon;
    AssetPack::GetInstance()->Finalize(dxCommon_.get(), &cookModelCommon);

    // 各種マネージャーのGPUリソースを解放する
    BloomEffect::GetInstance()->Finalize();
    ToneMapEffect::GetInstance()->Finalize();
    HsvFilter::GetInstance()->Finalize();
    VignetteEffect::GetInstance()->Finalize();
    ImageFilter::GetInstance()->Finalize();
    GrayscaleEffect::GetInstance()->Finalize();
    ParticleManager::GetInstance()->Finalize();
    MeshManager::GetInstance()->Finalize();
    MaterialManager::GetInstance()->Finalize();
    TextureManager::GetInstance()->Finalize();
    SrvManager::GetInstance()->Finalize();
    ModelManager::GetInstance()->Finalize();

    // Audioの終了
    if (audio_) {
        audio_->Finalize();
    }

    // クラス自体の削除
    imguiManager_.reset();
    audio_.reset();
    input_.reset();

    // 消した物を入口から取り出せないよう、空の組に戻す
    EngineContext::Set({});

    // 最後にデバイスとウィンドウを削除（Finalizeでまずフルスクリーン解除とGPU完了待ち）
    dxCommon_->Finalize();
    dxCommon_.reset();
    winApp_.reset();
}

} // namespace engine

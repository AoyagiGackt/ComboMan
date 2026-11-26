/**
 * @file MyGame.cpp
 * @brief Frameworkを継承したゲーム固有の初期化・更新・描画・シーン遷移制御（MyGame）の実装
 */
#include "MyGame.h"
#include "EngineContext.h"
#include "GameActions.h"
#include "GameAudio.h"
#include "AssetPack.h"
#include "Audio.h"
#include "BossBattleDirector.h"
#include "BossMotion.h"
#include "ClearScene.h"
#include "CombatTuning.h"
#include "EnemyTuning.h"
#include "EnemyTypeLibrary.h"
#include "FinalBossTuning.h"
#include "GameRules.h"
#include "GameSounds.h"
#include "HitEffectLibrary.h"
#include "JsonConfigEditor.h"
#include "KickedBombPool.h"
#include "MonsterTraitTuning.h"
#include "WeaponFinisherTuning.h"
#include "PlayerDefeat.h"
#include "BossExecution.h"
#include "PlayerTuning.h"
#include "ResultBackdrop.h"
#include "StageLiberation.h"
#include "StyleMeter.h"
#include "Logger.h"
#include "FrameProfiler.h"
#include "GameConstants.h"
#include "GamePlayScene.h"
#include "GameSettings.h"
#include "GamepadRumble.h"
#include "GrayscaleEffect.h"
#include "HsvFilter.h"
#include "ImGuiControlPanel.h"
#include "ImageFilter.h"
#include "Object3d.h"
#include "Object3dRenderQueue.h"
#include "ParticleManager.h"
#include "PipelineCache.h"
#include "InputBuffer.h"
#include "RenderPassGraph.h"
#include "PostEffectRenderTarget.h"
#include "SaveData.h"
#include "SceneFactory.h"
#include "SceneManager.h"
#include "ScoreManager.h"
#include "ScreenFlash.h"
#include "TextureManager.h"
#include "TimeManager.h"
#include "TitleScene.h"
#include "VignetteEffect.h"
#include <SrvManager.h>
#ifdef USE_IMGUI
#include "EditorFlowRow.h"
#include "EditorHub.h"
#include <imgui.h>
#endif
using namespace engine::graphics;

namespace engine {
namespace {
constexpr double kFileTimeTicksPerMillisecond = 10000.0; // FILETIMEは100ナノ秒単位

/** @brief exeのプロセスが作られてからの経過ミリ秒（main()より前の起動処理も含めて測るため） */
double MillisecondsSinceProcessStart() {
    FILETIME creation {}, exitTime {}, kernel {}, user {};
    GetProcessTimes(GetCurrentProcess(), &creation, &exitTime, &kernel, &user);
    FILETIME now {};
    GetSystemTimePreciseAsFileTime(&now);
    const ULARGE_INTEGER start { { creation.dwLowDateTime, creation.dwHighDateTime } };
    const ULARGE_INTEGER current { { now.dwLowDateTime, now.dwHighDateTime } };
    return static_cast<double>(current.QuadPart - start.QuadPart) / kFileTimeTicksPerMillisecond;
}

/** @brief ロード時間の比較用に、目印付きでログへ出す（素材の読み方のモードも添える） */
void LogLoadProfile(const std::string& label) {
    const char* mode = AssetPack::GetInstance()->GetMode() == AssetPack::Mode::kUse ? "use"
        : AssetPack::GetInstance()->IsCooking() ? "cook" : "off";
    Logger::Log("[LoadProfile] " + label + ": " + std::to_string(static_cast<int>(MillisecondsSinceProcessStart()))
        + " ms（exe起動から, pak=" + mode + "）");
}

#ifdef USE_IMGUI
/**
 * @brief 今のシーンの名前と、戻る・前へ・次へ・一覧から選んで移動する操作を描く
 * @note 前へ・次へは開始シーンの一覧の順に進み、端まで行くと反対の端へ回る
 */
void DrawSceneNavigation() {
    using game::SceneManager;
    SceneManager* scenes = SceneManager::GetInstance();
    const std::string& current = scenes->GetCurrentSceneName();
    const int count = SceneManager::GetDebugStartSceneCount();
    const int currentIndex = SceneManager::FindDebugStartSceneIndex(current);
    ImGui::Text("今のシーン: %s", SceneManager::SceneLabelFor(current).c_str());

    // さっきまで開いていたシーンへ戻る
    const bool canGoBack = scenes->CanGoBack();
    const std::string backLabel = canGoBack
        ? "戻る（" + SceneManager::SceneLabelFor(scenes->PreviousSceneName()) + "）"
        : std::string("戻る");
    ImGui::BeginDisabled(!canGoBack);
    if (ImGui::Button(backLabel.c_str())) {
        scenes->GoBack();
    }
    ImGui::EndDisabled();

    // 一覧の順で1つ前と1つ後のシーン（一覧に無いシーンにいる時は先頭と末尾から）
    const int previousIndex = currentIndex < 0 ? count - 1 : (currentIndex + count - 1) % count;
    const int nextIndex = currentIndex < 0 ? 0 : (currentIndex + 1) % count;
    const std::string previousLabel = std::string("< 前: ") + SceneManager::GetDebugStartSceneLabel(previousIndex);
    const std::string nextLabel = std::string("次: ") + SceneManager::GetDebugStartSceneLabel(nextIndex) + " >";
    // 入りきらなければ次の行へ回す（シーンの名前が長くてもはみ出さない）
    editor::FlowRow row;
    if (row.Button(previousLabel.c_str())) {
        scenes->OpenSceneFromEditor(SceneManager::GetDebugStartSceneName(previousIndex));
    }
    if (row.Button(nextLabel.c_str())) {
        scenes->OpenSceneFromEditor(SceneManager::GetDebugStartSceneName(nextIndex));
    }

    // 一覧から選んで直接移動する（今のシーンを選び直すと最初からやり直せる）
    ImGui::TextDisabled("シーンを選んで移動");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##sceneJump", SceneManager::SceneLabelFor(current).c_str())) {
        for (int index = 0; index < count; ++index) {
            if (ImGui::Selectable(SceneManager::GetDebugStartSceneLabel(index), index == currentIndex)) {
                scenes->OpenSceneFromEditor(SceneManager::GetDebugStartSceneName(index));
            }
        }
        ImGui::EndCombo();
    }
}

/** @brief 調整値エディタの対象フォルダと、保存したらすぐ読み直す設定ファイルを登録する */
void SetupConfigEditor() {
    auto* editor = JsonConfigEditor::GetInstance();
    editor->Initialize("Resources/Config", "Resources/Config/UI/tuning_labels.json", "Resources");
    // ここに無いファイルは保存しても次回起動時の反映になる（起動時に部品の数を決める演出などは、途中で読み直すと数が合わなくなるため登録しない）
    editor->SetReloadCallback("player_params.json", [] { game::PlayerTuning::Reload(); });
    editor->SetReloadCallback("combat_tuning.json", [] { game::CombatTuning::GetInstance()->Reload(); });
    editor->SetReloadCallback("enemy_params.json", [] { game::EnemyTuning::GetInstance()->Reload(); });
    editor->SetReloadCallback("enemy_types.json", [] { game::EnemyTypeLibrary::GetInstance()->Reload(); });
    editor->SetReloadCallback("game_rules.json", [] { game::GameRules::GetInstance()->Reload(); });
    editor->SetReloadCallback("hit_effects.json", [] { game::HitEffectLibrary::GetInstance()->Reload(); });
    editor->SetReloadCallback("style_meter.json", [] { game::StyleMeter::ReloadTuning(); });
    editor->SetReloadCallback("player_defeat.json", [] { game::PlayerDefeat::ReloadTuning(); });
    editor->SetReloadCallback("boss_execution.json", [] { game::BossExecution::ReloadTuning(); });
    editor->SetReloadCallback("stage_liberation.json", [] { game::StageLiberation::ReloadTuning(); });
    editor->SetReloadCallback("result_backdrop.json", [] { game::ResultBackdrop::ReloadTuning(); });
    editor->SetReloadCallback("clear_scene.json", [] { game::ClearScene::ReloadTuning(); });
    // 撃ち出す剣の数は闘技場を開いた時に決まるが、呼ぶ本数は剣の数で止めるので読み直してもよい
    editor->SetReloadCallback("final_boss.json", [] { game::FinalBossTuningStore::Reload(); });
    // ボスの名前と技の並びは、次に戦うボスを結び付けた時から使われる
    editor->SetReloadCallback("boss_battle.json", [] { game::BossBattleTuningStore::Reload(); });
    // ボスの技ごとの動きは、読み直した次のフレームから使われる
    editor->SetReloadCallback("boss_motion.json", [] { game::BossMotionLibrary::Reload(); });
    // 飛んでいる爆弾の見た目は最初に読み込んだモデルのまま（速さや当たりの大きさは読み直せる）
    editor->SetReloadCallback("bomb_kick.json", [] { game::BombKickTuningStore::Reload(); });
    // 締めの動作の数値と演出は読み直せる（打ち上げ技の有無はコンボの表を作る起動時だけ見る）
    editor->SetReloadCallback("weapon_finishers.json", [] { game::WeaponFinisherTuning::Reload(); });
    editor->SetReloadCallback("monster_traits.json", [] { game::MonsterTraitTuning::Reload(); });
    editor->SetReloadCallback("sounds.json", [] { game::gameSounds::Reload(); });
    editor->SetReloadCallback("bgm.json", [] { Audio::ReloadBGMConfig(); });
}
#endif
} // namespace
using namespace engine::game;

/** @brief ゲーム固有の初期化処理 */
void MyGame::Initialize() {
    // 自動テストで起動していれば、乱数を作る物より先に乱数の元の種を決める（同じ種なら同じ動きになる）
    SmokeTestRunner::ApplySeedFromCommandLine();

    // 基盤の初期化
    Framework::Initialize();
    input_->SetActionTable(GameActionTable());
    input_->SetPromptResolver(ResolveMovementPrompt);
    // 曲の表とメニュー音はゲームが決めて、エンジンの音へ渡す
    ConfigureGameAudio(*audio_);

    // 工場を作る
    sceneFactory_ = std::make_unique<SceneFactory>();

    // SceneManagerに工場を教える
    SceneManager::GetInstance()->SetSceneFactory(sceneFactory_.get());

    // 最初のBGMが鳴る前に、保存済みの音量設定を反映する。
    GameSettingsManager::GetInstance()->Load();
    GamepadRumble::GetInstance()->SetEnabled(GameSettingsManager::GetInstance()->Get().vibration);
    const GameSettings& s = GameSettingsManager::GetInstance()->Get();
    audio_->SetBGMVolume(s.bgmVolume);
    audio_->SetSEVolume(s.seVolume);

    // タイトルのメニュー判定より前に、セーブを読み込む。
    SaveDataManager::GetInstance()->Load();

    // 最初のシーンを工場経由でセットする
    LogLoadProfile("エンジン初期化完了");
    SceneManager::GetInstance()->Initialize(
        dxCommon_.get(),
        input_.get(),
        audio_.get(),
        imguiManager_.get());
    LogLoadProfile("タイトル準備完了");

    // スコアランキングを読み込む（コンティニューデータと通算記録はシーン生成の前に読み込み済み）
    ScoreManager::GetInstance()->LoadScores();

    // スクリーンフラッシュ初期化
    ScreenFlash::GetInstance()->Initialize(dxCommon_.get());

    // --smoke-test で起動したら自動テストプレイを始める（画面の更新を待たずに速く回すため VSync は切る）
    // --realtime も付けた時は、人が画面を見ながら確かめられるよう、ふだんの速さのまま進める
    constexpr const wchar_t* kRealtimeFlag = L"--realtime";
    const bool realtime = std::wstring(GetCommandLineW()).find(kRealtimeFlag) != std::wstring::npos;
    if (smokeTest_.StartIfRequested(input_.get(), dxCommon_.get()) && !realtime) {
        dxCommon_->SetVSyncEnabled(false);
        // 1秒60フレームで待つのもやめ、手順の表の speed の回数だけ1回の描画の間にゲームを進める
        dxCommon_->SetFrameLimitEnabled(false);
        fastSmokeTest_ = true;
    }

#ifdef USE_IMGUI
    SetupConfigEditor();
#endif
}

/** @brief ゲーム固有の更新処理（自動テストの速回しの時は、1回描くごとに何回か進める） */
void MyGame::Update() {
    // 再生で見つけた所が近づいたら、速回しをやめて人が見られるふつうの速さに戻す
    if (fastSmokeTest_ && smokeTest_.WantsRealtime()) {
        dxCommon_->SetVSyncEnabled(true);
        dxCommon_->SetFrameLimitEnabled(true);
        fastSmokeTest_ = false;
    }
    // 1回の更新で進める時間は固定のまま、重くて描画が遅れた時だけ遅れた分の回数を回す（速回しの自動テストは決まった回数）
    const int updates = fastSmokeTest_ ? smokeTest_.GetUpdatesPerDraw() : frameClock_.UpdatesThisFrame();
    for (int i = 0; i < updates; ++i) {
        // 描かなかった回の ImGui のフレームは、次を始める前に閉じておく
        if (i > 0) {
            imguiManager_->EndFrameWithoutRender();
        }
        UpdateOnce();
        if (endRequest_) {
            return;
        }
    }
}

/** @brief ゲームを1回分進める */
void MyGame::UpdateOnce() {
    // 自動テストの間は、1回の更新の重さを測り始める
    smokeTest_.BeginUpdate();

    // 基盤の更新
    Framework::Update();

    // 入力バッファ更新（Input::Update() の直後）
    InputBuffer::GetInstance()->Update(input_.get());

    // 時間管理・オーディオフェードを毎フレーム更新
    TimeManager::GetInstance()->Update();
    audio_->Update(gameConstants::kFrameDeltaTime);

    // 遅延コールバック・スクリーンフラッシュを毎フレーム更新
    ScreenFlash::GetInstance()->Update(gameConstants::kFrameDeltaTime);

#ifdef _DEBUG
    // テクスチャのホットリロード（保存したら即座に反映、開発時のみ）
    engine::Services().textures->CheckHotReload();
#endif

    // シーンマネージャー更新
    SceneManager::GetInstance()->Update();

    // 自動テストプレイの手順を進める（押したキーは次のフレームの入力から効く）。終えたらゲームを閉じる
    if (smokeTest_.Update()) {
        endRequest_ = true;
    }

#ifdef USE_IMGUI
    // F8でエディタの一覧とエンジン共通の実行時診断を開閉する
    if (input_->TriggerKey(DIK_F8)) {
        showEngineDiagnostics_ = !showEngineDiagnostics_;
    }
    // どのシーンでも使えるエディタも一覧に載せ、一覧で押されたら開閉する
    auto* editorHub = editor::EditorHub::GetInstance();
    if (editorHub->Offer("tuning", "調整値エディタ", "", "プレイヤーや敵の強さ、演出の長さなどの数値を、日本語の名前と説明を見ながら直します",
            JsonConfigEditor::GetInstance()->IsOpen())) {
        JsonConfigEditor::GetInstance()->Toggle();
    }
    if (editorHub->Offer("sound", "音の割り当て", "", "効果音や曲を、どの場面で鳴らすかを選びます", showSoundAssignEditor_)) {
        showSoundAssignEditor_ = !showSoundAssignEditor_;
    }
    if (editorHub->Offer("postProcess", "画面の効果の順番", "", "ブルーム・トーンマップ・空間のゆがみなどを掛ける順番と、掛けるかどうかを場面ごとに決めます", showPostProcessEditor_)) {
        showPostProcessEditor_ = !showPostProcessEditor_;
    }
    if (showEngineDiagnostics_) {
        const bool panelOpen = ImGui::Begin("エディタとツール (F8)", &showEngineDiagnostics_);
        if (panelOpen) {
            // 今のシーンで使えるエディタを、キーを覚えなくても開けるように並べる
            ImGui::SeparatorText("シーンの移動");
            DrawSceneNavigation();
            ImGui::SeparatorText("エディタ");
            editorHub->DrawList();
            ImGui::SeparatorText("エンジンの状態");
            auto* profiler = FrameProfiler::GetInstance();
            ImGui::Text("CPU %.2f ms  %.1f FPS", profiler->GetMs(), profiler->GetFPS());
            ImGui::Text("Gamepad %s", input_->IsGamepadConnected() ? "Connected" : "Disconnected");
            // 配置物の描画を複数のスレッドでバンドルへ記録するか（切って重さを比べられる）
            bool parallelRecording = ParallelBundleRecorder::IsEnabled();
            if (ImGui::Checkbox("配置物の描画を複数のスレッドで記録", &parallelRecording)) {
                ParallelBundleRecorder::SetEnabled(parallelRecording);
            }
            bool hotReloadEnabled = engine::Services().textures->IsHotReloadEnabled();
            if (ImGui::Checkbox("ゲーム中のテクスチャホットリロード", &hotReloadEnabled)) {
                engine::Services().textures->SetHotReloadEnabled(hotReloadEnabled);
            }

            // 画面外の 3D の物を描かない判定（切ると全部描くので、数と重さを比べられる）
            bool frustumCulling = Object3d::IsFrustumCullingEnabled();
            if (ImGui::Checkbox("画面外の物を描かない", &frustumCulling)) {
                Object3d::SetFrustumCullingEnabled(frustumCulling);
            }
            ImGui::Text("3D の物  描いた %u  画面外で省いた %u", lastDrawnObjects_, lastCulledObjects_);
            // 配置物をモデルごと・手前からの順に並べて描くか（切ると置いた順のまま描くので、重さを比べられる）
            bool sortObjects = Object3dRenderQueue::IsSortingEnabled();
            if (ImGui::Checkbox("配置物を並べ替えて描く", &sortObjects)) {
                Object3dRenderQueue::SetSortingEnabled(sortObjects);
            }
            // 同じモデル・同じ見た目の配置物を1回の命令でまとめて描くか（並べ替えを切ると使わない）
            bool instancing = Object3dRenderQueue::IsInstancingEnabled();
            if (ImGui::Checkbox("同じ配置物をまとめて1回で描く", &instancing)) {
                Object3dRenderQueue::SetInstancingEnabled(instancing);
            }
            ImGui::Text("配置物  描いた %u  命令 %u  まとめ描き %u", lastQueueStats_.drawn, lastQueueStats_.drawCalls, lastQueueStats_.instancedObjects);
            // 捨てられた GPU の資源のうち、GPU が使い終わるのを待っている数
            ImGui::Text("解放待ちの GPU 資源 %zu", dxCommon_->GetDeferredReleaseQueue().GetPendingCount());
            // GPU が数えて描いた粒の数（数フレーム前の値を読み戻したもの）
            ImGui::Text("生きている粒 %u", engine::Services().particles->GetGpuAliveParticleCount());
            // パイプラインの保存と再利用（2回目の起動から取り出せる数が増える）と、シェーダーのホットリロード
            const PipelineCache::Stats pipelineStats = PipelineCache::GetInstance()->GetStats();
            ImGui::Text("パイプライン  保存から %u  新しく %u  %.1f ms  読み直し %u", pipelineStats.hits, pipelineStats.misses,
                pipelineStats.createMilliseconds, pipelineStats.reloaded);
            bool shaderHotReload = PipelineCache::GetInstance()->IsHotReloadEnabled();
            if (ImGui::Checkbox("ゲーム中のシェーダーホットリロード", &shaderHotReload)) {
                PipelineCache::GetInstance()->SetHotReloadEnabled(shaderHotReload);
            }

            bool vsync = dxCommon_->IsVSyncEnabled();
            if (ImGui::Checkbox("VSync", &vsync)) {
                dxCommon_->SetVSyncEnabled(vsync);
            }

            GameSettings settings = GameSettingsManager::GetInstance()->Get();
            bool settingsChanged = false;
            settingsChanged |= ImGui::SliderFloat("BGM", &settings.bgmVolume, 0.0f, 1.0f);
            settingsChanged |= ImGui::SliderFloat("SE", &settings.seVolume, 0.0f, 1.0f);
            if (settingsChanged) {
                audio_->SetBGMVolume(settings.bgmVolume);
                audio_->SetSEVolume(settings.seVolume);
                GameSettingsManager::GetInstance()->Apply(settings);
                GameSettingsManager::GetInstance()->Save();
            }

        }
        ImGui::End();
    }
    JsonConfigEditor::GetInstance()->Draw();
    soundAssignEditor_.Draw(&showSoundAssignEditor_);
    postProcessEditor_.Draw(&showPostProcessEditor_);
    // このフレームの一覧を片付け、押された開閉の頼みを次のフレームへ渡す
    editorHub->EndFrame();
#endif

    ImGuiControlPanel::ShowControls();

    // ImGui終了処理
    imguiManager_->End();
}

/** @brief ゲーム固有の描画処理 */
void MyGame::Draw() {
    // バックバッファとSRVヒープを描画できる状態にする
    dxCommon_->PreDraw();
    // 前のフレームに数えた、描いた数と画面外で省いた数を診断パネル用に取っておく
    const Object3d::CullingStats cullingStats = Object3d::TakeCullingStats();
    lastDrawnObjects_ = cullingStats.drawn;
    lastCulledObjects_ = cullingStats.culled;
    lastQueueStats_ = Object3dRenderQueue::TakeFrameStats();
    // 解放待ちの数え上げはここで1フレームに1回だけ進める（PreDraw は描画中に何度も呼ばれる）
    engine::Services().srvManager->BeginFrame();
    engine::Services().srvManager->PreDraw();

    // ポストエフェクトの候補（FindActivePostEffect に渡す優先順）
    auto* gs = GrayscaleEffect::GetInstance();
    auto* imgFilter = ImageFilter::GetInstance();
    auto* hsv = HsvFilter::GetInstance();

    // オフスクリーンRTVへの描画に対応していないシーンでは
    // BeginScene/EndScene/Apply を呼ばない。呼んでしまうと、シーン側が何も描き込まない
    // オフスクリーンテクスチャでバックバッファが上書きされ、画面から絵が消えてしまう
    const bool postEffectsSupported = SceneManager::GetInstance()->CurrentScenePostEffectsSupported();

    RenderPassGraph graph(dxCommon_.get());

    // 同じ優先順で選んだエフェクトに開始と合成を委譲する
    IPostEffectSource* activeEffect = postEffectsSupported ? FindActivePostEffect({ imgFilter, gs, hsv }) : nullptr;
    // シーンの描き込み先は、描き始めのパスで描画先、合成のパスで読む状態にする（バリアはグラフが宣言から入れる）
    ID3D12Resource* sceneTexture = activeEffect != nullptr ? activeEffect->GetSceneTexture() : nullptr;
    graph.AddPass("Scene Capture Begin", { .writes = { WriteRenderTarget(sceneTexture) } }, [activeEffect] {
        activeEffect->BeginScene();
    }, activeEffect != nullptr);

    graph.AddPass("Scene", [] {
        SceneManager::GetInstance()->Draw();
    });

    graph.AddPass("Post Effects", { .reads = { ReadTexture(sceneTexture) } }, [activeEffect] {
        activeEffect->EndScene();
        activeEffect->Apply(engine::Services().srvManager);
    }, activeEffect != nullptr);

    // ポストエフェクトの後に、画面全体へかける演出・遷移・エディタ用の取り込み・デバッグUIの順で重ねる
    graph.AddPass("Vignette", [] {
        VignetteEffect::GetInstance()->Apply();
    });
    graph.AddPass("Screen Flash", [] {
        ScreenFlash::GetInstance()->Draw();
    });
    graph.AddPass("Scene Transition", [] {
        SceneManager::GetInstance()->DrawTransition();
    });
    graph.AddPass("Editor Preview", [] {
        SceneManager::GetInstance()->CaptureEditorPreview();
    });
    graph.AddPass("Diagnostics UI", [this] {
        imguiManager_->Draw(dxCommon_.get());
    });
    graph.Execute();

    dxCommon_->PostDraw();
    // このフレームに解放したデスクリプタは、今積んだフェンスの値まで GPU が進んでから再利用する
    engine::Services().srvManager->EndFrame();
}

/** @brief ゲーム固有の終了処理 */
void MyGame::Finalize() {
    SceneManager::GetInstance()->Finalize();

    Framework::Finalize();
}

} // namespace engine

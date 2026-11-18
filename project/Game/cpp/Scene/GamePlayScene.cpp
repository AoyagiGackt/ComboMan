/**
 * @file GamePlayScene.cpp
 * @brief メインステージ（GamePlayScene）の初期化と基本更新フローの実装
 */
#include "GamePlayScene.h"
#include "GameSounds.h"
#include "FinisherEffects.h"
#include "SceneVisualTuning.h"
#include "HitEffectLibrary.h"
#include "CombatTuning.h"
#include "AudioBridge.h"
#include "GameConstants.h"
#include "FinalBossTuning.h"
#include "GamePlaySceneInitializer.h"
#include "GameRules.h"
#include "SceneEffectBridge.h"
#include "SmokeTestRunner.h"
#include "SceneFlow.h"
#include "GrayscaleEffect.h"
#include "HsvFilter.h"
#include "ImGuiControlPanel.h"
#include "ImageFilter.h"
#include "ModelManager.h"
#include "ParticleManager.h"
#include "PipelineStateGuard.h"
#include "PlayerBridge.h"
#include "PlayerTuning.h"
#include "PlaytestLog.h"
#include "PostEffectRenderTarget.h"
#include "RunData.h"
#include "SceneManager.h"
#include "ScreenFlash.h"
#include "SlashMark.h"
#include "StageEditor.h"
#include "StringUtility.h"
#include "TextureManager.h"
#include "WeaponManager.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
using namespace engine::graphics;

namespace engine::game {
namespace {
constexpr const char* kDefaultLevelPath = "Resources/Levels/level01.json";


}

/** @brief ステージエディタが読み書きするレベルファイルを返す */
std::string GamePlayScene::GetEditorLevelPath() const {
    // 最後のステージをクリアした後は、ラスボス戦の闘技場を開く
    if (IsFinalBossMode()) {
        return FinalBossTuningStore::Get().level;
    }
    // マップのチュートリアルの入口から入った時は、練習用のステージを開く
    if (RunData::GetInstance()->IsTutorialActive() && !GameRules::GetInstance()->Get().tutorialLevelPath.empty()) {
        return GameRules::GetInstance()->Get().tutorialLevelPath;
    }
    return GameRules::GetInstance()->LevelPathForFloor(RunData::GetInstance()->GetPlayFloor(), kDefaultLevelPath);
}

// 初期化

// ══════════════════════════════════════════════════════
// シーン初期化
// ══════════════════════════════════════════════════════

/** @brief 共通の依存を受け取り、シーンが使うゲーム実体と描画資源を初期化する */
void GamePlayScene::Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio) {
    // 外部から受け取る共通依存だけを入口で保持し、所有資源の構築は下請けへ委譲する
    spriteCommon_ = InitializeCommonResources(dxCommon, input, audio, dxCommon_, input_, audio_);
    InitializeCoreSystems();
    InitializeFinalBoss();
    audio_->PlaySceneBGM(bgmKey::kStage);
}

/** @brief シーンが所有するゲーム実体、描画資源、演出を責務順に初期化する */
void GamePlayScene::InitializeCoreSystems() {
    InitializeRenderFoundation();
    GamePlaySceneInitializer::InitializeStageActors(*this);
    InitializeRenderTargetsAndOverlays();
    InitializeParticlesAndHud();
    InitializeGhostEditorAndEffects();
}

/** @brief InitializeCoreSystems()の下請け 描画基盤(Common類・シャドウ・カメラ)とスカイドームを初期化する */
void GamePlayScene::InitializeRenderFoundation() {
    srvManager_ = SrvManager::GetInstance();
    grayscaleEffect_ = GrayscaleEffect::GetInstance();
    imageFilter_ = ImageFilter::GetInstance();
    hsvFilter_ = HsvFilter::GetInstance();

    // 3D描画基盤を先に構築し、後続のゲーム実体が安全にモデルを生成できる状態にする
    BuildRenderFoundation(dxCommon_, srvManager_, "gameplay", { modelCommon_, objectCommon_, shadowManager_, camera_ });

    skydome_ = std::make_unique<Skydome>();
    skydome_->InitializeDefault(modelCommon_.get());
}

/** @brief InitializeCoreSystems()の下請け レンダーテクスチャとクリア演出用オーバーレイスプライトを初期化する */
void GamePlayScene::InitializeRenderTargetsAndOverlays() {
    renderTexture_ = std::make_unique<RenderTexture>();
    renderTexture_->Initialize(dxCommon_, srvManager_,
        WinApp::kClientWidth, WinApp::kClientHeight);

    renderTextureSprite_ = std::make_unique<Sprite>();
    renderTextureSprite_->Initialize(spriteCommon_.get(), "Resources/white.png");
    renderTextureSprite_->SetExternalTexture(renderTexture_->GetSrvIndex());
    renderTextureSprite_->SetPosition({ 0.0f, 0.0f });
    renderTextureSprite_->SetSize({ static_cast<float>(WinApp::kClientWidth),
        static_cast<float>(WinApp::kClientHeight) });

    resultBackdrop_.Initialize(spriteCommon_.get());

    finisherOverlay_ = sceneShared::CreateFinisherOverlay(spriteCommon_.get());
}

/** @brief InitializeCoreSystems()の下請け パーティクル・武器スロットHUDを初期化する */
void GamePlayScene::InitializeParticlesAndHud() {
    // パーティクルの設定（水場はこのゲームでは使わない。水面の描画はエンジン層の WaterSurfaceEffect に残してある）
    hudReady_ = false;
    pm_ = ParticleManager::GetInstance();
    sceneShared::CreateParticleGroupsFromJson(pm_, "Resources/particles/gameplay.json");

    // 文字・開始演出・斬撃線・スタイルランクの表示
    fontRenderer_.Initialize(spriteCommon_.get());
    stageIntro_.Initialize(spriteCommon_.get());
    playerDefeat_.Initialize(spriteCommon_.get(), &fontRenderer_, audio_);
    clearFinish_.Initialize(spriteCommon_.get());
    SlashMark::GetInstance()->Initialize(spriteCommon_.get());

    styleRankHud_.Initialize(spriteCommon_.get());

    // ボス頭上のHPバー（道中の武器敵ぶんはOnEditorLevelLoaded()でレベル読込のたびに生成する）
    const Vector4& hpBarBackground = SceneVisualTuning::Gameplay().hpBarBackgroundColor;
    sceneShared::CreateHpBarSprites(spriteCommon_.get(), bossHpBarBg_, bossHpBarFg_, hpBarBackground);
    InitializeAlly();
    // プレイヤーHPゲージ（左下、既存の数値表示のすぐ上に置く）
    sceneShared::CreateHpBarSprites(spriteCommon_.get(), playerHpBarBg_, playerHpBarFg_, hpBarBackground);

    // ボスAoEスラムの着弾予告円（円形グロー画像を赤く点滅させて範囲を示す）
    bossSlamWarningSprite_ = std::make_unique<Sprite>();
    bossSlamWarningSprite_->Initialize(spriteCommon_.get(), "Resources/Effects/circle2.png");
    stageBossHud_.Initialize(spriteCommon_.get(), &fontRenderer_);

    InitializeWeaponSlotHud();
    SetupPauseMenu();
}

/** @brief InitializeCoreSystems()の下請け 残像用オブジェクト・シーンエディタ・画面演出エフェクトを初期化する */
void GamePlayScene::InitializeGhostEditorAndEffects() {
    ghostObject_ = std::make_unique<Object3d>();
    ghostObject_->Initialize(modelCommon_.get());
    ghostObject_->SetModel(player_->GetModel());
    ghostObject_->SetEnableLighting(false);
    kickedBombs_.Initialize(modelCommon_.get());
    InitializeThrownWeapons();

    sceneEditor_.LoadAll(BuildEditContext());

    // カメラスムージング用の初期目標値を現在のカメラ位置から取る
    cameraTargetPos_ = camera_->GetTranslate();
    cameraTargetRot_ = camera_->GetRotate();

    glassShatter_.Initialize(dxCommon_, srvManager_);
    enemySlice_.Initialize(dxCommon_);
    bladeFlash_.Initialize(dxCommon_);
    comboFeel_.Initialize(dxCommon_, srvManager_);
    spaceWarp_.Initialize(dxCommon_, srvManager_);
    finisherShatter_.Initialize(dxCommon_, srvManager_);
    finisherShatter_.SetDuration(SceneVisualTuning::Gameplay().finisherShatterSeconds);

    ImGuiControlPanel::RegisterGlassShatterTrigger([this]() {
        TriggerGlassShatterTest();
    });
    // ノードグラフのShakeCameraノードからこのシーンのカメラシェイクを呼べるようにする
    SceneEffectBridge::GetInstance()->SetCameraShakeHandler(
        [this](float amount, float seconds) {
            cameraShaker_.Request(amount, seconds);
        });
    RegisterScriptEffectHandlers();
}

/** @brief 編集中にプレイヤーの表示座標を現在位置へ同期する */
void GamePlayScene::RefreshVisualTransformsForEditor() {
    if (player_) {
        player_->RefreshVisualTransforms();
    }
    RefreshEnemyVisualTransforms();

    // ゲーム更新停止中も、編集カメラで描画する全3D実体のWVPだけは更新する。
    // （収集物・壊せる物はStageEditor所有の配置物なのでStageEditor::UpdateObjects()側が追従させる）
    if (skydome_) {
        skydome_->Update(camera_.get());
    }
    if (ghostObject_ && !ghostTrail_.empty()) {
        ghostObject_->Update();
    }

    // HUDの文字はUpdate()内で組み立てているため、エディタでUIを動かした結果を止まった画面にも反映する
    UpdateWeaponSlotHud();
    DrawStyleUI();
}

/** @brief 敵（ボスと道中の武器敵）の見た目の行列だけを今のカメラで計算し直す */
void GamePlayScene::RefreshEnemyVisualTransforms() {
    if (enemy_) {
        enemy_->RefreshVisualTransforms();
    }
    for (auto& entry : weaponEnemies_) {
        if (entry.enemy) {
            entry.enemy->RefreshVisualTransforms();
        }
    }
}

/** @brief 立っている敵（倒れた敵と消えた敵は除く）の位置を集める */
std::vector<Vector3> GamePlayScene::CollectStandingEnemyPositions() const {
    std::vector<Vector3> positions;
    auto addStanding = [&positions](const EnemyEntity* enemy) {
        if (enemy != nullptr && !enemy->IsDefeated() && enemy->IsVisible()) {
            positions.push_back(enemy->GetPosition());
        }
    };
    addStanding(enemy_);
    for (const auto& entry : weaponEnemies_) {
        addStanding(entry.enemy);
    }
    return positions;
}

/** @brief 今の戦闘の相手（ボスか道中の敵）の実体か（作り直しで消えた敵を指したままにしないための確認） */
bool GamePlayScene::IsCombatEnemy(const EnemyEntity* enemy) const {
    if (enemy == nullptr) {
        return false;
    }
    return enemy == enemy_ || std::any_of(weaponEnemies_.begin(), weaponEnemies_.end(), [enemy](const WeaponEnemyEntry& entry) {
        return entry.enemy == enemy;
    });
}

/** @brief 乱舞の相手に向いた敵を選ぶ（ロック中の敵、いなければ近くで一番近い敵。届く所に誰もいなければ nullptr） */
EnemyEntity* GamePlayScene::PickRampageEnemy() const {
    const Vector3& playerPos = player_->GetPosition();
    const PlayerTuning& tuning = PlayerTuning::Get();
    EnemyEntity* nearest = nullptr;
    float nearestDistance = tuning.rampageSearchRange;
    bool lockedChosen = false;
    auto consider = [&](EnemyEntity* enemy) {
        if (lockedChosen || enemy == nullptr || enemy->IsDefeated() || !enemy->IsVisible()) {
            return;
        }
        // 閉じた扉や壁の向こうの敵は選ばない（連撃で敵の周りへ移る時に扉を抜けてしまう）
        if (IsWallBetweenEnemyAndPlayer(enemy->GetPosition())) {
            return;
        }
        // ロック中の敵は距離に関係なく選び、ほかの敵では置き換えない
        if (enemy == lockedEnemy_) {
            nearest = enemy;
            lockedChosen = true;
            return;
        }
        const Vector3& position = enemy->GetPosition();
        const float distance = std::abs(position.x - playerPos.x);
        if (distance <= nearestDistance && std::abs(position.y - playerPos.y) <= tuning.rampageSearchHeight) {
            nearest = enemy;
            nearestDistance = distance;
        }
    };
    consider(enemy_);
    for (const auto& entry : weaponEnemies_) {
        consider(entry.enemy);
    }
    return nearest;
}

/** @brief 乱舞の相手を決め直し、向かう位置を返す（乱舞の途中は同じ相手を追い、乱舞していない時は近くの敵を選び直す） */
Vector3 GamePlayScene::UpdateRampageEnemy() {
    if (!player_->IsRampaging() || !IsCombatEnemy(rampageEnemy_)) {
        rampageEnemy_ = PickRampageEnemy();
    }
    // 届く所に誰もいなければ、その場で打ち上げる
    return rampageEnemy_ != nullptr ? rampageEnemy_->GetPosition() : player_->GetPosition();
}

/** @brief 乱舞の打ち上げ・連撃・締めを向ける敵（相手を選べずその場で打ち上げた時は nullptr） */
EnemyEntity* GamePlayScene::RampageTargetEnemy() const {
    return IsCombatEnemy(rampageEnemy_) ? rampageEnemy_ : nullptr;
}

/** @brief シーンエディタへ渡す編集対象をまとめる */
SceneEditor::EditContext GamePlayScene::BuildEditContext() {
    SceneEditor::EditContext ctx;

    ctx.camera = camera_.get();
    ctx.skydome = skydome_.get();
    ctx.spriteCommon = spriteCommon_.get();

    ctx.cameraTargetPos = &cameraTargetPos_;
    ctx.cameraTargetRot = &cameraTargetRot_;
    ctx.cameraSmoothFrames = &cameraSmoothFrames_;
    ctx.cameraPosHistory = &cameraPosHistory_;
    ctx.cameraRotHistory = &cameraRotHistory_;

    ctx.skyColor = &skyColor_;
    ctx.skyRotOffsetY = &skyRotOffsetY_;

    ctx.requestClear = &requestClear_;
    return ctx;
}

/** @brief カメラを追従先へ滑らかに寄せる */
void GamePlayScene::UpdateCameraSmoothing() {
    cameraPosHistory_.push_back(cameraTargetPos_);
    cameraRotHistory_.push_back(cameraTargetRot_);

    while ((int)cameraPosHistory_.size() > cameraSmoothFrames_) {
        cameraPosHistory_.pop_front();
        cameraRotHistory_.pop_front();
    }

    Vector3 avgPos = { };
    Vector3 avgRot = { };
    for (const auto& p : cameraPosHistory_) {
        avgPos.x += p.x;
        avgPos.y += p.y;
        avgPos.z += p.z;
    }
    for (const auto& r : cameraRotHistory_) {
        avgRot.x += r.x;
        avgRot.y += r.y;
        avgRot.z += r.z;
    }
    float n = static_cast<float>(cameraPosHistory_.size());

    camera_->SetTranslate({ avgPos.x / n, cameraTargetPos_.y, avgPos.z / n });
    camera_->SetRotate({ avgRot.x / n, avgRot.y / n, avgRot.z / n });
}

// ══════════════════════════════════════════════════════
// シーン更新
// ══════════════════════════════════════════════════════

/** @brief ゲーム進行・戦闘・カメラ・演出を更新する（ポーズ中は止める） */
void GamePlayScene::Update() {
    // ラスボス戦の入力の差し替えは、プレイヤーを動かす間だけ使う
    if (clearTriggered_ || playerDefeat_.IsActive()) {
        finalBoss_.ReleaseInput();
    }
    if (UpdateClearState()) {
        return;
    }
    // やられ演出の間はポーズも開かせず、演出だけを進める
    if (playerDefeat_.IsActive()) {
        UpdatePlayerDefeat();
        return;
    }

    // 一時停止中はHUDを出さないが、文字コマンドの破棄だけは毎フレーム行う
    // （破棄はふだんDrawStyleUI()が行うため、飛ばすとポーズ画面の文字が溜まり続けて上限を超えた分から消える）
    pauseMenu_.SetPreviewActor(player_.get()); // コマンド表の技の見本はこのプレイヤーが演じる
    const bool choosingWeapon = WeaponManager::GetInstance()->HasPendingWeapon() || WeaponManager::GetInstance()->HasPendingBossChoice();
    const bool paused = pauseMenu_.Update(input_, !choosingWeapon);
    // ポーズ中と開始演出の間は、配置物・敵の進行も止める（BaseScene が Update の後に配置物を更新するため）
    GetStageEditor().SetGameplayHeld(paused || stageIntro_.IsPlaying());
    if (paused) {
        fontRenderer_.Reset();
        return;
    }

    if (stageIntro_.IsPlaying()) {
        UpdateStageIntro();
        return;
    }
    // ステージの最初の操作説明を出している間は、閉じるまでゲームと敵を止める
    if (stageGuide_.Update(*input_, true)) {
        GetStageEditor().SetGameplayHeld(true);
        fontRenderer_.Reset();
        UpdateCamera();
        player_->RefreshVisualTransforms();
        return;
    }
    // モーションエディタでプレビューしている間はゲームを止め、見た目だけ追従させる
    if (motionEditor_.Update(input_, *player_, WeaponManager::GetInstance()->GetCurrent().type)) {
        GetStageEditor().SetGameplayHeld(true);
        fontRenderer_.Reset();
        RefreshVisualTransformsForEditor();
        return;
    }
    // GO! を出した瞬間から操作できるので、残っている文字の表示時間だけここで進める
    stageIntro_.Update(input_);

    // 自動テストプレイの間だけ、遊んでいる様子からおかしな所を探して記録する（ふだんは何もしない）
    RunBugChecks();
    // 自動テストプレイの間だけ、F9 でやられ演出、F5 ですぐクリアできる（手順の表がステージを先へ進めるのに使う。ふだんは効かない）
    if (SmokeTestRunner::IsActive() && input_->TriggerKey(DIK_F9)) {
        StartPlayerDefeat();
        return;
    }
    if (SmokeTestRunner::IsActive() && input_->TriggerKey(DIK_F5)) {
        TriggerDebugClear();
        return;
    }
    // ラスボス戦の演出の間は、操作を止めて演出だけを進める
    if (UpdateFinalBossCinematic()) {
        return;
    }
    // ボスへのとどめの間は、操作と敵を止めて演出だけを進める
    if (bossExecution_.IsActive()) {
        UpdateBossExecution();
        return;
    }

    UpdateWeaponExchange();
    UpdateBossMainChoice();
    if (WeaponManager::GetInstance()->HasPendingWeapon() || WeaponManager::GetInstance()->HasPendingBossChoice()) {
        // 武器の入れ替えを選んでいる間もゲームは止まっているので、敵と仕掛けも待たせる
        GetStageEditor().SetGameplayHeld(true);
        UpdateCamera();
        UpdateStyleAndUI(0.0f);
        return;
    }

    auto* tm = TimeManager::GetInstance();
    const float dt = tm->GetDeltaTime(); // ヒットストップ中 = 0、スロー時は比例値
    floorElapsedSeconds_ += gameConstants::kFrameDeltaTime; // プレイログ用。ヒットストップの影響を受けない実時間換算

    UpdateCombat();
    // ラスボス戦はプレイヤーの更新と攻撃の判定の後に、ボスの技と闘技場を進める
    if (finalBoss_.IsActive()) {
        finalBoss_.UpdateFight(dt);
    }
    UpdateCamera();
    player_->RefreshVisualTransforms();
    sceneEditor_.Update(BuildEditContext(), input_);
    UpdateStyleAndUI(dt);
    // 締めの動作は、ボスへの通常の当たり（UpdateStyleAndUI）で吹き飛ばしが上書きされないようその後に出す
    ApplyPendingWeaponFinisher();
    UpdateParticles(dt);
    UpdateFinisherSlash(dt);
    enemySlice_.Update(dt, camera_.get());
    bladeFlash_.Update(dt, camera_.get());

    // 敵位置を画面UVへ投影して空間歪みの中心に設定する
    if (enemy_ && (spaceWarp_.IsActive() || finisherActive_)) {
        finisherEffects::UpdateWarpCenter(spaceWarp_, *camera_, enemy_->GetPosition());
    }
    spaceWarp_.Update(dt);

    // 解放時の世界割れ（ヒットストップ中は凍結し、時が動き出すと砕け散る）
    finisherShatter_.Update(dt);
    if (finisherShatter_.IsFinished()) {
        finisherShatter_.Reset();
    }

    // 切断演出が飛散に移ったら、敵本体を再表示する
    // 撃破済みでもJの武器吸収で本体を消費するまでは戻す（フィニッシャーで倒した場合に
    // 透明な敵へJを当てる状態にならないように）。吸収完了後はweaponStealTriggered_が立つので戻さない
    if (enemy_ && !enemy_->IsVisible() && !weaponStealTriggered_
        && (enemySlice_.IsBursting() || !enemySlice_.IsActive())) {
        enemy_->SetVisible(true);
    }

    SlashMark::GetInstance()->Update(dt);

    // ラン中にHPが尽きたら、やられ演出を見せてから再挑戦かマップへ戻るかを選ばせる（クリア演出が始まっていればそちらを優先する）
    auto* runData = RunData::GetInstance();
    if (GameRules::GetInstance()->Get().gameOverOnHpZero
        && runData->IsRunActive() && runData->GetHp() <= 0 && !clearTriggered_) {
        PlaytestLog::GetInstance()->RecordRunResult(false, runData->GetPlayFloor(), floorElapsedSeconds_,
            peakStyle_, styleRankHud_.GetBestChain(), player_->GetPosition());
        StartPlayerDefeat();
        return;
    }

    CheckClearCondition();
}

/** @brief ステージ開始演出を始める（最初にレベルを読み込んだ時に1回） */
void GamePlayScene::StartStageIntro() {
    StageIntro::Setup setup;
    setup.stageKey = GetEditorLevelPath();
    // 開始位置は床より少し上に置かれていることがあるので、真下の床に立った位置へ降ろす
    setup.landing = player_->FindStandingPosition(player_->GetPosition(), GetStageEditor().GetSolidColliders());
    setup.hasBoss = enemy_ != nullptr;
    // ステージ全体を見渡す時に収める範囲。高さはステージの上下の中央を映す（カメラ側で追従の高さ分を足すので引いておく）
    const StageBounds bounds = GetStageBounds();
    setup.stageLeft = bounds.left;
    setup.stageRight = bounds.right;
    setup.stageCenterY = (bounds.fromLevel ? (bounds.bottom + bounds.top) * 0.5f : setup.landing.y) - gameConstants::kCameraFollowOffsetY;
    setup.stageHeight = bounds.fromLevel ? bounds.top - bounds.bottom : 0.0f;
    if (enemy_ != nullptr) {
        setup.bossFocus = enemy_->GetPosition();
        // ボスを倒すと奪える武器（配置物に設定された武器種別）の名前と色を見せる
        const auto& weapons = WeaponManager::GetInstance()->GetList();
        const auto weapon = std::find_if(weapons.begin(), weapons.end(),
            [this](const WeaponData& data) {
                return data.type == bossWeaponType_;
            });
        if (weapon != weapons.end()) {
            // 日本語のスタイル名だけを出す（未設定の武器だけ内部名で代用する）
            setup.weaponName = WeaponManager::GetDisplayName(*weapon);
            setup.weaponColor = { weapon->styleColor[0], weapon->styleColor[1], weapon->styleColor[2], 1.0f };
        }
    }
    stageIntro_.Start(setup, { player_.get(), pm_, &cameraShaker_, &rng_ });
    // 決めたステージなら、開始演出が終わった後に操作説明を出す
    stageGuide_.Arm(GetEditorLevelPath());
    GetStageEditor().SetGameplayHeld(stageIntro_.IsPlaying());
}

/** @brief 開始演出の間の1フレーム（操作・HUD・敵を止め、カメラだけ演出に合わせて動かす） */
void GamePlayScene::UpdateStageIntro() {
    // HUDは出さず、演出の文字だけを積む（文字コマンドの破棄はふだんDrawStyleUI()が行うのでここで代わりに行う）
    fontRenderer_.Reset();
    stageIntro_.Update(input_);
    UpdateCamera();
    // 戦闘の更新を通らないので、カメラに付いていく空とプレイヤーの見た目はここで追従させる
    skydome_->Update(camera_.get());
    player_->RefreshVisualTransforms();
    stageIntro_.QueueText(fontRenderer_);
    if (!stageIntro_.IsPlaying()) {
        GetStageEditor().SetGameplayHeld(false);
    }
}

/** @brief プレイヤーのやられ演出を始める（ラン中にHPが尽きた時に1回） */
void GamePlayScene::StartPlayerDefeat() {
    // ロック中の対象へカメラを寄せないよう外し、敵と仕掛けも止める（とどめの途中なら閉じて技の姿勢から戻す）
    bossExecution_.End();
    SetWeaponEnemyWeaponsHidden(false);
    lockedEnemy_ = nullptr;
    const StageBounds bounds = GetStageBounds();
    PlayerDefeat::Setup setup;
    setup.stageLeft = bounds.left;
    setup.stageRight = bounds.right;
    setup.solids = GetStageEditor().GetSolidColliders();
    // 倒れた体が埋まらないよう、立っている敵を避けさせる
    setup.enemyPositions = CollectStandingEnemyPositions();
    playerDefeat_.Start(setup, { player_.get(), pm_, &cameraShaker_, &rng_ });
    // やられた瞬間に曲を止め、GAME OVER が出た所でゲームオーバーの曲を流す
    audio_->StopBGM();
    GetStageEditor().SetGameplayHeld(true);
}

/** @brief やられ演出の間の1フレーム（操作・HUD・敵を止め、カメラだけ演出に合わせて動かし、見せ終えたら選ばれた行き先（同じステージ／マップ）へ） */
void GamePlayScene::UpdatePlayerDefeat() {
    // HUDは出さず、演出の文字だけを積む（文字コマンドの破棄はふだんDrawStyleUI()が行うのでここで代わりに行う）
    fontRenderer_.Reset();
    GetStageEditor().SetGameplayHeld(true);
    playerDefeat_.Update(input_);
    UpdateCamera();
    // 戦闘の更新を通らないので、カメラに付いていく空・プレイヤー・敵の見た目はここで追従させる
    skydome_->Update(camera_.get());
    playerDefeat_.UpdatePlayerPose();
    RefreshEnemyVisualTransforms();
    playerDefeat_.QueueText(fontRenderer_);
    if (playerDefeat_.ConsumeGameOverShown()) {
        audio_->PlaySceneBGM(bgmKey::kGameOver, false);
    }
    const PlayerDefeat::Choice choice = playerDefeat_.ConsumeChoice();
    if (choice == PlayerDefeat::Choice::kNone) {
        return;
    }
    // HPが尽きたままだと戻った先ですぐやられるので、どちらを選んでも満タンにしてから移る
    auto* runData = RunData::GetInstance();
    runData->Heal(runData->GetMaxHp());
    if (choice == PlayerDefeat::Choice::kRetry) {
        SceneFlow::GetInstance()->Transition(kSceneName, "retry", "GAMEPLAY");
    } else {
        SceneFlow::GetInstance()->Transition(kSceneName, "defeat_map", "MAP");
    }
}


// ══════════════════════════════════════════════════════
// 戦闘とカメラの更新
// ══════════════════════════════════════════════════════
void GamePlayScene::UpdateCombatEvents() {
    auto* tm = TimeManager::GetInstance();
    // 乱舞の打ち上げ・連撃・締めは乱舞の相手に向ける（道中の敵にも、ボスにも）。相手がいなければ演出だけその場に出す
    EnemyEntity* const rampageTarget = RampageTargetEnemy();
    const Vector3 rampagePos = rampageTarget != nullptr ? rampageTarget->GetPosition() : player_->GetPosition();
    const HitEffectPlayer rampageFx(pm_, &cameraShaker_, &rng_);
    // 乱舞の始め: 足元から覚醒色の輪を広げ、画面を光らせて揺らす（ここから突進することを見せる）
    if (player_->JustStartedRampage()) {
        rampageFx.Ring("rampage_start", player_->GetPosition());
        rampageFx.Flash("rampage_start");
        rampageFx.Impact("rampage_start");
    }
    // 乱舞 打ち上げヒット
    // 打ち上げ: 敵を浮かせ、止めと揺れと輪と火花を出す
    if (player_->JustLaunched()) {
        // 道中の敵は必ず浮き、ボスはひるんでいる間だけ浮く
        if (rampageTarget != nullptr && (rampageTarget != enemy_ || BossReactsToHits())) {
            rampageTarget->Launch(CombatTune().launchSpeed);
        }
        const Vector3& epos = rampagePos;
        rampageFx.Ring("rampage_launch", epos);
        rampageFx.Sparks("rampage_launch", epos);
        tm->RequestHitStop(HitEffectLibrary::GetInstance()->SceneParticles().hitStopLaunch);
        cameraShaker_.Request(HitEffectLibrary::GetInstance()->SceneParticles().shakeLaunchAmt, HitEffectLibrary::GetInstance()->SceneParticles().shakeLaunchDur);
        pm_->EmitRing("hit_ring", epos, SceneVisualTuning::Gameplay().launchRingSpeed, SceneVisualTuning::Gameplay().launchRingColor, SceneVisualTuning::Gameplay().launchRingCount, SceneVisualTuning::Gameplay().launchRingLifetime, SceneVisualTuning::Gameplay().launchRingSize);
        std::uniform_real_distribution<float> vxL(-SceneVisualTuning::Gameplay().launchSparkSpreadX, SceneVisualTuning::Gameplay().launchSparkSpreadX);
        std::uniform_real_distribution<float> vyL(SceneVisualTuning::Gameplay().launchSparkRiseMin, SceneVisualTuning::Gameplay().launchSparkRiseMax);
        for (int i = 0; i < SceneVisualTuning::Gameplay().launchSparkCount; ++i) {
            pm_->EmitGravity("hit_spark", epos,
                { vxL(rng_), vyL(rng_), 0.0f },
                SceneVisualTuning::Gameplay().launchSparkColor, SceneVisualTuning::Gameplay().launchSparkLifetime, SceneVisualTuning::Gameplay().launchSparkSize);
        }
    }
    // 乱舞 ジャグルスラッシュ
    if (player_->JustRampageHit()) {
        // ジャグル: 当てた回数が増えるほど斬撃を大きく、色を赤寄りに、揺れを強くする
        int cnt = player_->GetJuggleCount();
        float dir = player_->GetLastDirX();
        float slashAng = (dir > 0.0f) ? 0.0f : gameConstants::kPi;
        float rad = SceneVisualTuning::Gameplay().juggleSlashRadiusBase + cnt * SceneVisualTuning::Gameplay().juggleSlashRadiusStep;
        const Vector3& epos = rampagePos;
        pm_->EmitSlash("sword_slash", epos, slashAng,
            { 1.0f, 1.0f - cnt * SceneVisualTuning::Gameplay().juggleSlashGreenFade, 1.0f - cnt * SceneVisualTuning::Gameplay().juggleSlashBlueFade, 1.0f }, rad);
        pm_->EmitRing("hit_ring", epos, SceneVisualTuning::Gameplay().juggleRingSpeedBase + cnt * SceneVisualTuning::Gameplay().juggleRingSpeedStep,
            SceneVisualTuning::Gameplay().juggleRingColor, SceneVisualTuning::Gameplay().juggleRingCount, SceneVisualTuning::Gameplay().juggleRingLifetime, SceneVisualTuning::Gameplay().juggleRingSize);
        tm->RequestHitStop(HitEffectLibrary::GetInstance()->SceneParticles().hitStopJuggle);
        cameraShaker_.Request(SceneVisualTuning::Gameplay().juggleShakeBase + cnt * SceneVisualTuning::Gameplay().juggleShakeStep, SceneVisualTuning::Gameplay().juggleShakeSeconds);
        rampageFx.Star("rampage_slash", epos);
        // 近接の当たりと同じ画面の斬撃線を引く（覚醒中は扇状に重なる）。最後の一撃はブラー・ズーム・スローの締め演出も出す
        const auto* wm = WeaponManager::GetInstance();
        const Vector4 slashColor = wm->HasEquippedWeapon()
            ? ColorFromArray(wm->GetCurrent().effectColor)
            : HitEffectLibrary::GetInstance()->Color("unarmed_trail");
        comboFeel_.OnMeleeHit(nullptr, player_->JustRampageFinish(), epos, slashColor, dir, camera_.get());
    }
    // 乱舞 フィニッシュ
    if (player_->JustRampageFinish()) {
        // 乱舞の締め: 大きな止めと揺れ、内外2重の輪と火花
        const Vector3& epos = rampagePos;
        rampageFx.Flash("rampage_finish");
        rampageFx.Impact("rampage_finish");
        tm->RequestHitStop(HitEffectLibrary::GetInstance()->SceneParticles().hitStopFinish);
        cameraShaker_.Request(HitEffectLibrary::GetInstance()->SceneParticles().shakeFinishAmt, HitEffectLibrary::GetInstance()->SceneParticles().shakeFinishDur);
        pm_->EmitRing("hit_ring", epos, SceneVisualTuning::Gameplay().finishOuterRingSpeed, SceneVisualTuning::Gameplay().finishOuterRingColor,
            SceneVisualTuning::Gameplay().finishOuterRingCount, SceneVisualTuning::Gameplay().finishOuterRingLifetime, SceneVisualTuning::Gameplay().finishOuterRingSize);
        pm_->EmitRing("hit_ring", epos, SceneVisualTuning::Gameplay().finishInnerRingSpeed, SceneVisualTuning::Gameplay().finishInnerRingColor,
            SceneVisualTuning::Gameplay().finishInnerRingCount, SceneVisualTuning::Gameplay().finishInnerRingLifetime, SceneVisualTuning::Gameplay().finishInnerRingSize);
        std::uniform_real_distribution<float> vxF(-SceneVisualTuning::Gameplay().finishSparkSpreadX, SceneVisualTuning::Gameplay().finishSparkSpreadX);
        std::uniform_real_distribution<float> vyF(SceneVisualTuning::Gameplay().finishSparkRiseMin, SceneVisualTuning::Gameplay().finishSparkRiseMax);
        for (int i = 0; i < SceneVisualTuning::Gameplay().finishSparkCount; ++i) {
            pm_->EmitGravity("hit_spark", epos,
                { vxF(rng_), vyF(rng_), 0.0f },
                { 1.0f, SceneVisualTuning::Gameplay().finishSparkGreenBase + i * SceneVisualTuning::Gameplay().finishSparkGreenStep, SceneVisualTuning::Gameplay().finishSparkBlue, 1.0f },
                SceneVisualTuning::Gameplay().finishSparkLifetime, SceneVisualTuning::Gameplay().finishSparkSize);
        }
    }
    // ここから下はボスに対する演出なので、ボスがいる時だけ進める
    if (!enemy_) {
        return;
    }
    // フィニッシャースラッシュ 発動の合図（斬撃線の表示は UpdateFinisherSlash に委譲）
    if (player_->JustFinisherSlash()) {
        const Vector3& epos = enemy_->GetPosition();
        finisherActive_ = true;
        finisherLineIdx_ = 0;
        finisherSlashes_.clear();
        finisherBeatTimer_ = CombatTune().finisherChargeDelay;
        // 画面内の敵をすべて打ち上げて空中に拘束し、暗転とともに溜めを作る
        for (EnemyEntity* target : CollectOnScreenEnemies()) {
            target->Launch(CombatTune().launchSpeed * CombatTune().finisherLaunchRatio);
        }
        tm->RequestHitStop(HitEffectLibrary::GetInstance()->SceneParticles().hitStopJuggle);
        cameraShaker_.Request(SceneVisualTuning::Gameplay().finisherStartShakeAmount, SceneVisualTuning::Gameplay().finisherStartShakeSeconds);
        sceneShared::EmitFinisherCharge(pm_, "hit_ring", "hit_spark", epos);
        spaceWarp_.AddImpulse(SceneVisualTuning::Gameplay().finisherStartWarpImpulse);
    }
}
/** @brief 戦闘（攻撃の判定・敵の攻撃・演出）を1フレーム進める */
void GamePlayScene::UpdateCombat() {
    auto* tm = TimeManager::GetInstance();
    // ヒットストップ中も回避ボタンの押下は記憶しておく（停止明けに回避へつなげ、押した感触が消えないように）
    player_->BufferDodgeInput(input_);
    // 奪う相手はここで1体だけ決め、道中の敵とボスの両方の回収で使う（ヒットストップ中も決め直す）
    SelectStealTarget();
    // ヒットが入るとこのフレーム内でヒットストップが始まるため、進んだかどうかは先に確定させておく
    const bool gameAdvanced = !tm->IsHitStopped();
    if (gameAdvanced) {
        SyncCombatEnemies(); // spawn_pointが出した敵をこのフレームから戦闘対象に含める
        UpdateTargetLock();
        std::vector<AABB> activeColliders = GetStageEditor().GetSolidColliders();
        ApplyScriptControlLock();
        // 闘技場では前後左右の入力を、ボスへ向かう線の上の左右の操作と回り込みへ置き換える
        if (finalBoss_.IsActive()) {
            finalBoss_.PrepareInput();
        }
        player_->SetRampageTarget(UpdateRampageEnemy());
        const Vector3 positionBeforeUpdate = player_->GetPosition();
        player_->Update(input_, enemy_ ? enemy_->GetPosition() : player_->GetPosition(), enemy_ && !enemy_->IsDefeated() && enemy_->IsVisible(), !activeColliders.empty());
        gameSounds::PlayPlayerActions(*player_);
        // ボスだけでなく道中の敵の手前でも止め、プレイヤーが歩いて敵を押しのけないようにする
        player_->ResolveEnemyOverlaps(CollectStandingEnemyPositions());
        // 乱舞の連撃や踏み込みなど一気に動く技と、敵と重なった時に敵の向こうへずらす分で、壁や閉じた扉の真ん中を横切ったら横の位置を戻す
        // （壁際の敵に重なると壁の向こうへ置かれてしまう。ふつうに歩いてぶつかるだけなら真ん中までは入らないので、この後の地形との当たり判定に任せる）
        if (DidPlayerCrossWall(positionBeforeUpdate, player_->GetPosition())) {
            Vector3 kept = player_->GetPosition();
            kept.x = positionBeforeUpdate.x;
            player_->SetPosition(kept);
        }
        // 移動後に足場との接触を解決し、補正が入ったフレームは見た目も同期する
        // エディタの現在状態から毎フレーム判定を作り、移動・追加・削除を即時反映する
        player_->ResolveBlockCollision(activeColliders);
        // 地形との当たり判定で壁の真ん中より向こうへ押し出された時も、このフレームの初めの横の位置へ戻す
        if (DidPlayerCrossWall(positionBeforeUpdate, player_->GetPosition())) {
            Vector3 kept = player_->GetPosition();
            kept.x = positionBeforeUpdate.x;
            player_->SetPosition(kept);
            player_->RefreshVisualTransforms();
        }
        UpdateFallRescue();
        UpdateAlly();
        player_->RefreshVisualTransforms();
        // 敵が自分から食い込んできた分は、敵が動いた後（StageEditor::UpdateObjects）に押し戻させる。乱舞はわざと密着させるので外す
        GetStageEditor().SetEnemiesAvoidPlayer(!player_->IsRampaging());
        UpdateWeaponTrail();
        UpdateComboFeel(true);
        BuildGunShotTarget();
        // ロック中は移動入力に関係なく対象の方を向かせる（コンボ判定より前でないと今フレームに反映されない）
        if (lockedEnemy_ != nullptr) {
            player_->FaceTarget(lockedEnemy_->GetPosition());
        }
        UpdateCombatEvents();
        UpdateWeaponEnemies();
        // 地上コンボの締めが当たった瞬間の武器ごとの動作と、投げた武器（通常の当たりの後に重ねる）
        UpdateWeaponFinisher();
        UpdateThrownWeapons();
        // 雑魚へのヒットは直前のUpdateWeaponEnemies()、ボスへのヒットは前フレームのUpdateStyleAndUI()で記録済み
        UpdateWeaponFatigue();
        UpdateExplosiveBarrels();
        // 配置物の更新で集めた扉の開始通知を一度だけ演出へ渡す
        const HitEffectPlayer gateFx(pm_, &cameraShaker_, &rng_);
        for (const Vector3& position : GetStageEditor().ConsumeOpenedGatePositions()) {
            gateFx.Sparks("gate_dust", position);
            gateFx.Impact("gate_open");
        }
        // enemy_の物理/アニメーション更新自体はStageEditor所有のためGetStageEditor().UpdateObjects()
        // （BaseScene::Tick()がUpdate()の直後に呼ぶ）が担う。ここでは前フレーム分の着地判定だけ読む
        // （1フレーム遅延するが60fps下では実用上無視できる差）
        // 乱舞の相手が着地したらジャグル強制終了
        if (const EnemyEntity* rampageTarget = RampageTargetEnemy(); rampageTarget != nullptr && rampageTarget->JustLanded()) {
            player_->EndRampage();
        }
        skydome_->Update(camera_.get());
    } else {
        // 止まっている間も締め演出の時間は進め、軌跡はその場に留める
        UpdateComboFeel(false);
    }
}
/** @brief シーン固有の演出資源と登録済みコールバックを破棄する */
void GamePlayScene::Finalize() {
    motionEditor_.Close(*player_);
    // 2P に渡していた機器を 1P の操作へ戻す（次のシーンで 1P の機器が外れたままにならないように）
    LeaveTwoPlayer(nullptr);
    // ラスボス戦で刀だけにした武器と、闘技場の入力の差し替えを元へ戻す
    finalBoss_.Finalize();
    // enemy_/weaponEnemies_はStageEditor所有（非所有ポインタ）のため、EnemyRegistryへの
    // 登録解除もStageEditor自身のReleaseLevelResources()が行う。ここでは何もしない
    // デバッグパネルとカメラ揺れの受け口から、このシーンの登録を外す
    ImGuiControlPanel::RegisterGlassShatterTrigger(nullptr);
    SceneEffectBridge::GetInstance()->SetCameraShakeHandler({ });
    SceneEffectBridge::GetInstance()->SetMessageHandler({ });
    SceneEffectBridge::GetInstance()->SetCameraFocusHandler({ });
    SceneEffectBridge::GetInstance()->SetControlLockHandler({ });
    // 操作を止めたままシーンを抜けても、次の画面に上書きを残さない
    if (scriptControlLocked_) {
        input_->ClearActionOverrides();
        scriptControlLocked_ = false;
    }
    // 描画先と演出の資源を片付ける
    renderTexture_->Finalize(srvManager_);
    pm_->ClearAllGroups();
    finisherEffects::FinalizeAll(glassShatter_, finisherShatter_, spaceWarp_, bladeFlash_);
    comboFeel_.Finalize();
    // 音を全部止める（BGM・SE どちらも）
    if (audio_) {
        audio_->StopBGM();
        audio_->StopAllSE();
    }
}

} // namespace engine::game

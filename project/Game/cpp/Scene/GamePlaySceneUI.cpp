/**
 * @file GamePlaySceneUI.cpp
 * @brief GamePlaySceneのHUD・オーバーレイ・武器スロット表示・スタイルコマンド表示を実装するファイル
 * @note GamePlayScene のHUD表示だけを GamePlayScene.cpp から分けて置いている（クラスは GamePlayScene のまま）
 */
#include "GamePlayScene.h"
#include "HitEffectLibrary.h"
#include "WeaponSlotHud.h"
#include "AwakenGaugeHud.h"
#include "AudioBridge.h"
#include "CombatTuning.h"
#include "FinisherEffects.h"
#include "GameConstants.h"
#include "GamePlaySceneInitializer.h"
#include "GameSettings.h"
#include "GrayscaleEffect.h"
#include "HsvFilter.h"
#include "ImGuiControlPanel.h"
#include "ImageFilter.h"
#include "ModelManager.h"
#include "ParticleManager.h"
#include "PipelineStateGuard.h"
#include "PlayerBridge.h"
#include "PostEffectRenderTarget.h"
#include "RunData.h"
#include "SaveData.h"
#include "SceneEffectBridge.h"
#include "SceneFlow.h"
#include "SceneManager.h"
#include "ScreenFlash.h"
#include "SlashMark.h"
#include "StageEditor.h"
#include "StringUtility.h"
#include "TextureManager.h"
#include "UILayout.h"
#include "MeleeCombo.h"
#include "WeaponFinisherTuning.h"
#include "WeaponManager.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
using namespace engine::graphics;

namespace engine::game {
namespace {
constexpr Vector4 kFinisherOverlayTint = { 0.0f, 0.0f, 0.05f, 0.0f }; // アルファはHitEffectLibrary::GetInstance()->SceneParticles().finisherOverlayAlpha

// プレイヤーHPの数値表示（HPバーの中の真ん中に、影を付けて重ねる）
constexpr float kInfoScale = 1.5f;
constexpr Vector4 kInfoColor = { 1.0f, 1.0f, 1.0f, 1.0f };
constexpr Vector4 kInfoShadowColor = { 0.0f, 0.0f, 0.0f, 0.85f };
constexpr float kInfoShadowOffset = 2.0f;

// HPバーの色（残量が減るほど緑から赤へ）
constexpr float kHpBarGreenScale = 0.85f;
constexpr float kHpBarGreenBase = 0.15f;
constexpr float kEnemyHpBarAlpha = 0.9f;
constexpr float kPlayerHpBarAlpha = 0.95f;
constexpr Vector2 kPlayerHpBarPosition = { 12.0f, 40.0f }; // 左上のステージ名の見出しの真下（下に置くとプレイヤーと重なって見えなくなる）
constexpr Vector2 kPlayerHpBarSize = { 320.0f, 32.0f };
constexpr Vector2 kEnemyHpBarSize = { 60.0f, 8.0f };
constexpr float kEnemyHpBarRaise = 70.0f; // 敵の足元からバーまでの高さ（画面px）

// コンボルート案内
constexpr Vector2 kGuidePosition = { 355.0f, 574.0f };
constexpr float kGuideTitleScale = 1.15f;
constexpr Vector4 kGuideTitleColor = { 1.0f, 0.82f, 0.2f, 0.82f };
constexpr float kGuideRouteOffsetY = 25.0f;
constexpr float kGuideRouteScale = 1.0f;
constexpr Vector4 kGuideRouteColor = { 0.78f, 0.84f, 0.92f, 0.8f };
constexpr float kGuideProgressScale = 1.35f;
constexpr Vector4 kGuideProgressColor = { 1.0f, 0.82f, 0.2f, 0.95f };
constexpr float kGuideNextOffsetY = 28.0f;
constexpr float kGuideNextScale = 1.15f;
constexpr Vector4 kGuideNextAirColor = { 0.45f, 0.95f, 1.0f, 1.0f };
constexpr Vector4 kGuideNextOpenerColor = { 1.0f, 1.0f, 1.0f, 0.98f };
constexpr Vector4 kGuideNextLauncherColor = { 1.0f, 0.85f, 0.25f, 1.0f };
constexpr Vector4 kGuideFinishColor = { 1.0f, 0.42f, 0.16f, 1.0f };
constexpr int kGuideOpenerSteps = 2; // この段数に達するまでは追撃を案内する

// ラスボス戦の操作説明パネルの既定位置
constexpr Vector2 kGameplayControlsAnchor = { 1020.0f, 12.0f };

// 武器回収の操作表示（届く距離の一番近い1体だけに出し、ほかは小さな印だけ）
constexpr float kWeaponStealPromptScale = 1.3f;
constexpr Vector4 kWeaponStealPromptColor = { 0.55f, 0.9f, 1.0f, 1.0f };

// ロックオンマーカー
constexpr float kLockMarkerHeight = 1.6f;
constexpr float kLockMarkerScale = 1.3f;
constexpr Vector4 kLockMarkerColor = { 1.0f, 0.35f, 0.2f, 1.0f };

// 武器スロット満杯時の交換案内
constexpr int kWeaponSlotCount = WeaponManager::kSlotCount;
constexpr Vector2 kExchangeTitlePosition = { 360.0f, 220.0f };
constexpr float kExchangeTitleScale = 2.0f;
constexpr Vector4 kExchangeTitleColor = { 1.0f, 0.85f, 0.2f, 1.0f };
constexpr Vector2 kExchangeWeaponPosition = { 410.0f, 270.0f };
constexpr float kExchangeWeaponScale = 1.6f;
constexpr Vector4 kExchangeWeaponColor = { 0.8f, 0.95f, 1.0f, 1.0f };
constexpr Vector2 kExchangeHelpPosition = { 245.0f, 330.0f };
constexpr float kExchangeHelpScale = 1.35f;
constexpr Vector4 kExchangeHelpColor = { 1.0f, 1.0f, 1.0f, 1.0f };
}

/** @brief HPバー・ランク・武器欄・エディタ配置UIなどHUDのスプライトを描く（一時停止中は呼ばない） */
void GamePlayScene::DrawHudSprites() {
    // 敵頭上のHPバー（BattleTestSceneのダミーHPバーと同じ描画順序位置はUpdateEnemyHpBars()が
    // DrawStyleUI()内で毎フレーム更新している）
    // ラスボスの体力は画面下のバーで見せるので、頭上には出さない
    // 名乗りが済んだステージボスも、画面下のバーで見せる
    if (enemy_ && !enemy_->IsDefeated() && !finalBoss_.IsActive() && !bossDirector_.IsEngaged()) {
        bossHpBarBg_->Draw();
        bossHpBarFg_->Draw();
    }
    for (auto& entry : weaponEnemies_) {
        if (entry.enemy && !entry.enemy->IsDefeated() && entry.hpBarPlaced) {
            entry.hpBarBg->Draw();
            entry.hpBarFg->Draw();
        }
    }
    DrawAllyHpBar();
    if (RunData::GetInstance()->IsRunActive()) {
        playerHpBarBg_->Draw();
        playerHpBarFg_->Draw();
    }
    if (bossSlamWarningActive_) {
        bossSlamWarningSprite_->Draw();
    }

    styleRankHud_.DrawHud(); // 右上ランクの進捗バー（BattleTestSceneと同じ見た目）
    DrawWeaponSlotHud();

    sceneEditor_.DrawUIElements();
}

/** @brief Draw()の下請け エディタUI・フィニッシャー演出・フォントなど2D上乗せ描画をまとめて行う */
void GamePlayScene::DrawOverlaysAndUI() {
    spriteCommon_->CommonDrawSettings();

    // 一時停止中と開始演出・やられ演出・最初の操作説明の間はHUDを隠す
    // ボスへのとどめの演出の間も、HPバーなどは映さない
    if (hudReady_ && !pauseMenu_.IsPaused() && !stageIntro_.IsPlaying() && !playerDefeat_.IsActive() && !clearTriggered_
        && !finalBoss_.BlocksGameplay() && !stageGuide_.IsShowing() && !bossExecution_.IsActive()) {
        DrawHudSprites();
    }
    // ラスボス戦の体力バー・黒帯・名乗り（演出の間も出す）。ステージボスの画面下のバーは、とどめの間は出さない
    if (!pauseMenu_.IsPaused() && !playerDefeat_.IsActive() && !clearTriggered_ && !bossExecution_.IsActive()) {
        finalBoss_.DrawHud();
        if (!finalBoss_.IsActive() && bossDirector_.IsEngaged()) {
            stageBossHud_.Draw();
        }
    }
    stageIntro_.Draw();
    playerDefeat_.Draw();

    // 大技中と解放フレーム（凍結画面のキャプチャ前）だけ暗転を重ねる
    // 解放後の暗さは砕け散る凍結画面が持ち去るので、素の世界には重ねない
    const bool captureFrame = finisherShatter_.IsActive() && finisherShatter_.NeedCapture();
    if (finisherActive_ || captureFrame) {
        finisherOverlay_->SetColor({ kFinisherOverlayTint.x, kFinisherOverlayTint.y, kFinisherOverlayTint.z,
            HitEffectLibrary::GetInstance()->SceneParticles().finisherOverlayAlpha });
        finisherOverlay_->Update();
        finisherOverlay_->Draw();
    }
    SlashMark::GetInstance()->Draw();

    // 解放時の世界割れ（暗転+斬撃線ごと凍った画面を砕き、下から素の世界が現れる）
    if (finisherShatter_.IsActive() && IsDrawingToBackBuffer()) {
        if (finisherShatter_.NeedCapture()) {
            finisherShatter_.CaptureFrame();
        }
        // スコープを抜けた瞬間に、Apply が変えたレンダーターゲットとルートシグネチャを後続のスプライト描画用に戻す
        PipelineStateGuard restoreGuard([this] {
            SetupMainRenderTarget();
            spriteCommon_->CommonDrawSettings();
        });
        finisherShatter_.Apply();
    }

    if (pauseMenu_.IsPaused()) {
        pauseMenu_.Draw(input_);
    } else if (!stageIntro_.IsPlaying() && !playerDefeat_.IsActive() && !clearTriggered_ && !stageGuide_.IsShowing() && ShowsStageGuideText()
        && !bossExecution_.IsActive()) {
        // ゲームプレイ UI テキスト（レベルJSONの案内文）。ボスへのとどめの演出の間は出さない
        GetStageEditor().DrawUIText(fontRenderer_);
    }
    // 1面の最初の操作説明（文字と同じく最後に積んで、HUD の上に出す）
    if (!pauseMenu_.IsPaused()) {
        stageGuide_.Draw(fontRenderer_, *input_);
    }
    fontRenderer_.Draw();
    if (pauseMenu_.IsPaused()) {
        pauseMenu_.DrawCommandPreview({ dxCommon_, modelCommon_.get(), objectCommon_.get(), shadowManager_.get(), srvManager_,
            GetActiveRTVHandle() });
        spriteCommon_->CommonDrawSettings();
    }

    // クリアの決め演出: 暗幕・斬線・弾痕を重ね、割り始めるフレームでその画面を撮って破片を重ねる
    // （以降のフレームは DrawClearOverlayIfNeeded() が結果画面の上に破片を重ねる）
    if (clearTriggered_ && clearFinish_.IsActive() && clearFinish_.ShowsGameFrame()) {
        spriteCommon_->CommonDrawSettings();
        clearFinish_.DrawStrikeOverlay();
        if (glassShatter_.NeedCapture()) {
            // 色の加工中はオフスクリーンへ描いているので、そこから写してそこへ重ねる
            sceneShared::BindCaptureTarget(glassShatter_, { imageFilter_, grayscaleEffect_, hsvFilter_ });
            glassShatter_.CaptureFrame();
            glassShatter_.Apply();
        }
    }
}

// ══════════════════════════════════════════════════════
// HUD描画
// ══════════════════════════════════════════════════════

/** @brief ラン中だけ、プレイヤーの体力の数値を描画する */
void GamePlayScene::DrawRogueliteHUD() {
    auto* rd = RunData::GetInstance();
    if (!rd->IsRunActive()) {
        return;
    }

    // ボスの体力は頭上のバーと、名乗った後の画面上のバーで見せる
    UILayout& layout = UILayout::Get(kLayoutName);

    // プレイヤーHPの数値は、左上のHPバーの中の真ん中に重ねる（バーと数字を別々に探さなくて済むように）
    const std::wstring info = L"HP " + std::to_wstring(rd->GetHp()) + L" / " + std::to_wstring(rd->GetMaxHp());
    const Vector2 barPosition = layout.Pos("player_hp_bar.pos", kPlayerHpBarPosition);
    const Vector2 barSize = layout.Vec2("player_hp_bar.size", kPlayerHpBarSize);
    const float scale = layout.Float("info.scale", kInfoScale);
    const Vector2 textSize = FontRenderer::MeasureStringW(info, scale);
    constexpr float kHalf = 0.5f;
    const float textX = barPosition.x + (barSize.x - textSize.x) * kHalf;
    const float textY = barPosition.y + (barSize.y - textSize.y) * kHalf;
    // 緑でも赤でも読めるよう、黒い影を先に描いてから白で重ねる
    fontRenderer_.DrawStringW(info, textX + kInfoShadowOffset, textY + kInfoShadowOffset, scale, kInfoShadowColor);
    fontRenderer_.DrawStringW(info, textX, textY, scale, layout.Color("info.color", kInfoColor));
}

/** @brief ボス・道中の武器敵の頭上HPバーの位置と色を更新する（BattleTestSceneのダミーHPバーと同じ体裁） */
void GamePlayScene::UpdateEnemyHpBars() {
    UILayout& layout = UILayout::Get(kLayoutName);
    const float hpBarGreenScale = layout.Float("hp_bar.green_scale", kHpBarGreenScale);
    const float hpBarGreenBase = layout.Float("hp_bar.green_base", kHpBarGreenBase);
    const float enemyHpBarAlpha = layout.Float("enemy_hp_bar.alpha", kEnemyHpBarAlpha);
    // BattleTestScene::UpdateHpBars()と同じ体裁（頭上に固定サイズのバー、WorldToScreenで追従）
    const Vector3& cam = camera_->GetTranslate();
    const Vector2 barSize = layout.Vec2("enemy_hp_bar.size", kEnemyHpBarSize);
    const float barRaise = layout.Float("enemy_hp_bar.raise", kEnemyHpBarRaise);

    const sceneShared::HpBarStyle style { barSize, barRaise, hpBarGreenScale, hpBarGreenBase, enemyHpBarAlpha };
    auto layoutBar = [&](Sprite* background, Sprite* foreground, const Vector3& position, float ratio) {
        sceneShared::UpdateWorldHpBar(background, foreground, position, cam, ratio, style);
    };

    if (enemy_ && !enemy_->IsDefeated() && enemy_->GetMaxHp() > 0) {
        const float ratio = static_cast<float>(enemy_->GetHp()) / static_cast<float>(enemy_->GetMaxHp());
        layoutBar(bossHpBarBg_.get(), bossHpBarFg_.get(), enemy_->GetPosition(), ratio);
    }
    for (auto& entry : weaponEnemies_) {
        if (!entry.enemy || entry.enemy->IsDefeated() || entry.enemy->GetMaxHp() <= 0) {
            continue;
        }
        const float ratio = static_cast<float>(entry.enemy->GetHp()) / static_cast<float>(entry.enemy->GetMaxHp());
        layoutBar(entry.hpBarBg.get(), entry.hpBarFg.get(), entry.enemy->GetPosition(), ratio);
        entry.hpBarPlaced = true;
    }
}

/** @brief 左上のプレイヤーHPゲージの位置と色を更新する */
void GamePlayScene::UpdatePlayerHpBar() {
    UILayout& layout = UILayout::Get(kLayoutName);
    const float hpBarGreenScale = layout.Float("hp_bar.green_scale", kHpBarGreenScale);
    const float hpBarGreenBase = layout.Float("hp_bar.green_base", kHpBarGreenBase);
    const float playerHpBarAlpha = layout.Float("player_hp_bar.alpha", kPlayerHpBarAlpha);
    auto* rd = RunData::GetInstance();
    if (!rd->IsRunActive() || rd->GetMaxHp() <= 0) {
        return;
    }

    const Vector2 barPosition = layout.Pos("player_hp_bar.pos", kPlayerHpBarPosition);
    const Vector2 barSize = layout.Vec2("player_hp_bar.size", kPlayerHpBarSize);

    const float ratio = std::clamp(static_cast<float>(rd->GetHp()) / static_cast<float>(rd->GetMaxHp()), 0.0f, 1.0f);
    playerHpBarBg_->SetPosition(barPosition);
    playerHpBarBg_->SetSize(barSize);
    playerHpBarBg_->Update();

    playerHpBarFg_->SetColor({ 1.0f - ratio, ratio * hpBarGreenScale + hpBarGreenBase, 0.0f, playerHpBarAlpha });
    playerHpBarFg_->SetPosition(barPosition);
    playerHpBarFg_->SetSize({ barSize.x * ratio, barSize.y });
    playerHpBarFg_->Update();
}

/** @brief スタイルランクと各種HUDを描画する */
void GamePlayScene::DrawStyleUI() {
    // 表示位置・大きさ・色はすべて UI レイアウトから読む（F2 で調整できる）
    UILayout& layout = UILayout::Get(kLayoutName);
    const float guideTitleScale = layout.Float("combo_guide.title_scale", kGuideTitleScale);
    const Vector4 guideTitleColor = layout.Color("combo_guide.title_color", kGuideTitleColor);
    const float guideRouteOffsetY = layout.Float("combo_guide.route_offset_y", kGuideRouteOffsetY);
    const float guideRouteScale = layout.Float("combo_guide.route_scale", kGuideRouteScale);
    const Vector4 guideRouteColor = layout.Color("combo_guide.route_color", kGuideRouteColor);
    const float guideProgressScale = layout.Float("combo_guide.progress_scale", kGuideProgressScale);
    const Vector4 guideProgressColor = layout.Color("combo_guide.progress_color", kGuideProgressColor);
    const float guideNextOffsetY = layout.Float("combo_guide.next_offset_y", kGuideNextOffsetY);
    const float guideNextScale = layout.Float("combo_guide.next_scale", kGuideNextScale);
    const Vector4 guideNextAirColor = layout.Color("combo_guide.next_air_color", kGuideNextAirColor);
    const Vector4 guideNextOpenerColor = layout.Color("combo_guide.next_opener_color", kGuideNextOpenerColor);
    const Vector4 guideNextLauncherColor = layout.Color("combo_guide.next_launcher_color", kGuideNextLauncherColor);
    const Vector4 guideFinishColor = layout.Color("combo_guide.finish_color", kGuideFinishColor);
    const float weaponStealPromptScale = layout.Float("steal_prompt.scale", kWeaponStealPromptScale);
    const Vector4 weaponStealPromptColor = layout.Color("steal_prompt.color", kWeaponStealPromptColor);
    const float lockMarkerScale = layout.Float("lock_marker.scale", kLockMarkerScale);
    const Vector4 lockMarkerColor = layout.Color("lock_marker.color", kLockMarkerColor);
    // 文字を積み直し、HPバー・ステージ案内・ラン情報・スタイルランクを更新する
    fontRenderer_.Reset();
    // 開始演出が終わった直後は GO! の文字がまだ消えていないので、HUD と一緒に積む
    stageIntro_.QueueText(fontRenderer_);

    UpdateEnemyHpBars();
    UpdateAllyHud();
    UpdatePlayerHpBar();
    DrawStageGuide();
    DrawRogueliteHUD();
    styleRankHud_.UpdateHud(fontRenderer_); // 右上のスタイリッシュランク（BattleTestSceneと同じ体裁）

    // 基本ルートは常時見せ、コンボ開始後は現在地点と次の入力へ表示を切り替える。
    // トレーニングで覚えた操作を本編でも同じ順番で再確認できる導線にする。
    if (WeaponManager::GetInstance()->HasEquippedWeapon()) {
        const Vector2 guidePosition = layout.Pos("combo_guide.pos", kGuidePosition);
        const float guideX = guidePosition.x;
        const float guideY = guidePosition.y;
        // コンボ前は推奨ルートを、コンボ中は進み具合と次の一手を出す
        const bool chainActive = styleRankHud_.GetHitCount() > 0 || player_->IsMeleeAttacking();
        const int step = (std::max)(player_->GetComboStep(), 1);
        const int maxStep = (std::max)(player_->GetComboMax(), 1);
        // 打ち上げて空中へ行く武器と、地上の締めで戦う武器で案内を分ける（締めの説明は weapon_finishers.json の guide）
        const WeaponType weaponType = WeaponManager::GetInstance()->GetCurrent().type;
        const bool hasLauncher = GetMeleeComboSet(weaponType, WeaponManager::GetInstance()->IsBossSlotSelected()).launcher != nullptr;
        const std::wstring finisherGuide = StringUtility::ConvertString(WeaponFinisherTuning::Get(weaponType).guide);
        if (!chainActive) {
            fontRenderer_.DrawStringW(L"推奨コンボルート",
                guideX, guideY, guideTitleScale, guideTitleColor);
            const std::wstring route = hasLauncher
                ? finalBoss_.ExpandPrompts(L"[{Attack}] x2  >  [{Down}+{Attack}] 打ち上げ  >  [{Jump}] ジャンプ  >  [{Attack}] 空中追撃  >  [{Shoot}] 射撃")
                : finalBoss_.ExpandPrompts(L"[{Attack}] 連打  >  ") + finisherGuide + finalBoss_.ExpandPrompts(L"  >  [{Skill}] 固有技  >  [{Shoot}] 射撃");
            fontRenderer_.DrawStringW(route, guideX, guideY + guideRouteOffsetY, guideRouteScale, guideRouteColor);
        } else {
            std::wstring progress = L"コンボ  ";
            for (int i = 1; i <= maxStep; ++i) {
                progress += i <= step ? L"● " : L"○ ";
            }
            fontRenderer_.DrawStringW(progress, guideX, guideY, guideProgressScale, guideProgressColor);

            // 次の一手の案内（上の行から順に見て、最初に当てはまった案内を出す。最後の行は必ず当てはまる）
            struct GuideState {
                bool airborne;
                int step;
                int maxStep;
            };
            /** @brief 次の行動の案内文と表示条件の組（打ち上げを持たない武器は noLauncherText を出す） */
            struct NextHint {
                bool (*applies)(const GuideState&);
                const wchar_t* text;
                const wchar_t* noLauncherText;
                Vector4 color;
            };
            const NextHint kNextHints[] = {
                { [](const GuideState& g) {
                    return g.airborne;
                },
                    L"次  [{Attack}] 空中追撃   [{Shoot}] 射撃   [{Skill}] 固有技",
                    L"次  [{Attack}] 空中追撃   [{Shoot}] 射撃   [{Skill}] 固有技", guideNextAirColor },
                { [](const GuideState& g) {
                    return g.step < kGuideOpenerSteps;
                },
                    L"次  [{Attack}] もう一撃   または  [{Down}+{Attack}] 打ち上げ",
                    L"次  [{Attack}] もう一撃", guideNextOpenerColor },
                { [](const GuideState& g) {
                    return g.step < g.maxStep;
                },
                    L"次  [{Down}+{Attack}] 打ち上げ   [{Slot}] 武器切替",
                    L"次  [{Attack}] 締めへ   [{Slot}] 武器切替", guideNextLauncherColor },
                { [](const GuideState&) {
                    return true;
                },
                    L"締め!   [{Down}+{Attack}] 打ち上げ   [{Slot}] 別武器へ",
                    L"締め!   [{Slot}] 別武器へ", guideFinishColor },
            };
            const GuideState state = { !player_->IsOnGround(), step, maxStep };
            for (const NextHint& hint : kNextHints) {
                if (hint.applies(state)) {
                    fontRenderer_.DrawStringW(finalBoss_.ExpandPrompts(hasLauncher ? hint.text : hint.noLauncherText),
                        guideX, guideY + guideNextOffsetY, guideNextScale, hint.color);
                    break;
                }
            }
        }
    }
    // 武器の疲労警告・ノードグラフのメッセージ・武器一覧・操作説明
    DrawWeaponFatigueWarning();
    DrawScriptMessage();
    DrawWeaponListPanel();
    // ラスボス戦の間は、戦闘の前の操作説明と同じ闘技場の操作を出す
    if (finalBoss_.IsActive()) {
        sceneShared::DrawControlsPanel(fontRenderer_, layout.Pos("controls.pos", kGameplayControlsAnchor),
            L"-- " + finalBoss_.ControlsTitle() + L" --", finalBoss_.ControlsRows());
    } else {
        // 右上のランクの下に、よく使う操作だけを短く出す（全部はポーズの操作説明で見せる）
        DrawShortControls();
    }
    hud_.QueueText(fontRenderer_);
    DrawWeaponStealPrompts(weaponStealPromptScale, weaponStealPromptColor);
    DrawWeaponAcquireBanner();
    DrawOffscreenEnemyGuide();
    DrawAllyMark();

    // ── ロックオン中の対象にマーカーを出す ────────────────────────
    if (lockedEnemy_ != nullptr) {
        const Vector3 tpos = lockedEnemy_->GetPosition();
        const Vector3& cam = camera_->GetTranslate();
        float sx, sy;
        sceneShared::WorldToScreen(tpos.x, tpos.y + kLockMarkerHeight, cam.x, cam.y, sx, sy);
        const std::wstring lockText = L"▼ ロック ▼";
        fontRenderer_.DrawStringW(lockText, sx - FontRenderer::MeasureStringW(lockText, lockMarkerScale).x * kCenterFactor, sy,
            lockMarkerScale, lockMarkerColor);
    }

    // スロットが満杯の時の武器交換画面と、ボスの武器の主軸を選ぶ画面
    DrawWeaponExchange();
    DrawBossMainChoice();
    // 開始演出の直後の1フレームは、HUDの配置が済んでから描画する
    hudReady_ = true;
}

/** @brief テストシーンと共通の武器スロットUIを初期化する */
void GamePlayScene::InitializeWeaponSlotHud() {
    hud_.Add(std::make_unique<AwakenGaugeHud>());
    hud_.Add(std::make_unique<WeaponSlotHud>());
    hud_.Initialize({ *spriteCommon_, *modelCommon_, *dxCommon_, *WeaponManager::GetInstance() });
    UpdateWeaponSlotHud();
}

/** @brief 武器スロットUIの選択状態と演出を更新する */
void GamePlayScene::UpdateWeaponSlotHud() {
    HudFrame frame { *camera_, gameConstants::kFrameDeltaTime, player_->GetAwakenGauge(), player_->IsAwakened(), auraTimer_ };
    frame.input = input_;
    hud_.Update(frame);
}

/** @brief 武器スロットUIを描画する */
void GamePlayScene::DrawWeaponSlotHud() {
    hud_.Draw();
}

/** @brief 満杯時の武器交換入力を処理する */
void GamePlayScene::UpdateWeaponExchange() {
    auto* wm = WeaponManager::GetInstance();
    if (!wm->HasPendingWeapon()) {
        return;
    }

    // 十字キーは通常の武器切替と同じ並び（上=1、右=2、下=3）
    constexpr WORD kSlotPadButtons[kWeaponSlotCount] = {
        XINPUT_GAMEPAD_DPAD_UP, XINPUT_GAMEPAD_DPAD_RIGHT, XINPUT_GAMEPAD_DPAD_DOWN
    };
    for (int slot = 0; slot < kWeaponSlotCount; ++slot) {
        if (input_->TriggerKey(static_cast<uint8_t>(DIK_1 + slot)) || input_->TriggerButton(kSlotPadButtons[slot])) {
            wm->ReplacePendingWeapon(slot);
            pendingWeaponMandatory_ = false;
            hud_.Notify(HudEvent::kWeaponAcquired);
            return;
        }
    }
    if (!pendingWeaponMandatory_ && (input_->TriggerKey(DIK_BACK) || input_->TriggerButton(XINPUT_GAMEPAD_B))) {
        wm->DiscardPendingWeapon();
    }
}

/** @brief 満杯時の武器交換画面を描画する */
void GamePlayScene::DrawWeaponExchange() {
    auto* wm = WeaponManager::GetInstance();
    if (!wm->HasPendingWeapon()) {
        return;
    }

    UILayout& layout = UILayout::Get(kLayoutName);
    const Vector2 titlePosition = layout.Pos("exchange.title_pos", kExchangeTitlePosition);
    fontRenderer_.DrawStringW(L"武器スロットが満杯です", titlePosition.x, titlePosition.y,
        layout.Float("exchange.title_scale", kExchangeTitleScale), layout.Color("exchange.title_color", kExchangeTitleColor));
    const Vector2 weaponPosition = layout.Pos("exchange.weapon_pos", kExchangeWeaponPosition);
    // 開始演出と同じく日本語のスタイル名で見せる
    fontRenderer_.DrawStringW(L"入手武器  " + WeaponManager::GetDisplayName(wm->GetPendingWeapon()),
        weaponPosition.x, weaponPosition.y,
        layout.Float("exchange.weapon_scale", kExchangeWeaponScale), layout.Color("exchange.weapon_color", kExchangeWeaponColor));
    const Vector2 helpPosition = layout.Pos("exchange.help_pos", kExchangeHelpPosition);
    const bool pad = input_->IsUsingGamepad();
    const std::wstring slotKeys = pad ? L"十字キー(上・右・下)" : L"1から3";
    const std::wstring discard = pad ? L"Bで破棄" : L"Backspaceで破棄";
    fontRenderer_.DrawStringW(slotKeys + L"で交換するスロットを選択  "
            + (pendingWeaponMandatory_ ? std::wstring(L"ボスの武器は破棄できない") : discard),
        helpPosition.x, helpPosition.y,
        layout.Float("exchange.help_scale", kExchangeHelpScale), layout.Color("exchange.help_color", kExchangeHelpColor));
}

// ══════════════════════════════════════════════════════
// 一時停止メニュー
// ══════════════════════════════════════════════════════

/** @brief 一時停止メニューの項目を登録する（退出はステージセレクトへ戻る） */
void GamePlayScene::SetupPauseMenu() {
    pauseMenu_.Initialize(spriteCommon_.get(), &fontRenderer_, audio_, kLayoutName,
        { "ステージセレクト", [] {
             auto* rd = RunData::GetInstance();
             // ラスボス戦の間は保存しない（最後のステージのクリアでコンティニューは消してあり、刀だけの一時的な持ち物を残さないため）
             // チュートリアルの間も保存しない（練習で手に入れた武器を本編へ持ち込まないため）
             if (rd->IsRunActive() && !rd->IsFinalBossPending() && !rd->IsTutorialActive()) {
                 SaveDataManager::GetInstance()->SaveContinue(*rd);
             }
             SceneFlow::GetInstance()->Transition(kSceneName, "pause_quit", "MAP");
         } });
    pauseMenu_.InitializeCommandPreview(modelCommon_.get());
    pauseMenu_.EnableWeaponFusionOption();
}
/** @brief ノードグラフのメッセージ・カメラ・操作停止の受け口を登録する */
void GamePlayScene::RegisterScriptEffectHandlers() {
    auto* bridge = SceneEffectBridge::GetInstance();
    bridge->SetMessageHandler([this](const std::string& text, float seconds) {
        scriptMessage_ = StringUtility::ConvertString(text);
        scriptMessageSeconds_ = seconds;
    });
    bridge->SetCameraFocusHandler([this](float x, float y, float seconds) {
        scriptCameraFocus_ = { x, y, 0.0f };
        scriptCameraFocusSeconds_ = seconds;
    });
    bridge->SetControlLockHandler([this](bool locked) {
        // 戻した時は上書きを外して、実機の入力をそのまま使う
        if (scriptControlLocked_ && !locked) {
            input_->ClearActionOverrides();
        }
        scriptControlLocked_ = locked;
    });
}

/** @brief ノードグラフで操作を止めている間は、ポーズ以外の操作を押していない扱いにする */
void GamePlayScene::ApplyScriptControlLock() {
    if (!scriptControlLocked_) {
        return;
    }
    for (int i = 0; i < static_cast<int>(Input::Action::kCount); ++i) {
        const auto action = static_cast<Input::Action>(i);
        if (action != Input::Action::kPause) {
            input_->SetActionOverride(action, false);
        }
    }
}
} // namespace engine::game

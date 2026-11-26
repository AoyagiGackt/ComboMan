/**
 * @file GamePlaySceneGuideUI.cpp
 * @brief GamePlayScene のうち、案内の文字の表示（収集物の個数・短い操作の一覧・武器を奪う案内・武器の疲労警告・ノードグラフの文字）
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
// 武器の疲労警告はデバッグボタンの下に配置する
constexpr Vector2 kFatigueWarningPosition = { gameConstants::kScreenCenterX, 40.0f }; // 画面の上の中央
constexpr float kFatigueWarningBossBarY = 90.0f; // ボスの体力バー（上の中央、高さ 24〜75）が出ている間は、そのすぐ下に出す
constexpr float kFatigueTitleScale = 1.6f;
constexpr float kFatigueDetailScale = 1.2f;
constexpr float kFatigueDetailOffsetY = 34.0f;
constexpr Vector4 kFatigueMildColor = { 1.0f, 0.85f, 0.25f, 1.0f }; // 効きが落ち始めた
constexpr Vector4 kFatigueSevereColor = { 1.0f, 0.3f, 0.2f, 1.0f }; // ほとんど効かない
constexpr float kFatigueSevereThreshold = 0.5f; // 落ち幅がこの割合を超えたら強い警告に切り替える
constexpr float kFatigueBlinkSpeed = 10.0f;
constexpr float kFatigueBlinkMinAlpha = 0.45f;
constexpr float kPercent = 100.0f;

// 右上の短い操作の一覧（よく使う操作だけ。全部はポーズの操作説明で見せる）。位置は一覧の右端と上端（ランクとヒット数の下）
constexpr Vector2 kShortControlsAnchor = { 1256.0f, 252.0f }; // ヒット数が一番大きくなった時の 最高 N 連続 の行（y=235 付近）より下
constexpr float kShortControlsScale = 0.95f;
constexpr float kShortControlsLineGap = 4.0f; // 行と行の間
constexpr Vector4 kShortControlsKeyColor = { 1.0f, 0.85f, 0.3f, 1.0f }; // ボタン名（明るい床の上でも読めるよう黄色）
constexpr Vector4 kShortControlsTextColor = { 0.95f, 0.95f, 0.95f, 1.0f }; // 操作の名前
constexpr Vector4 kShortControlsPanelColor = { 0.0f, 0.0f, 0.0f, 0.55f }; // 文字の後ろに敷く板
constexpr float kShortControlsPadding = 8.0f; // 板の文字のまわりの余白

/** @brief 右上の短い操作の一覧の1行（ボタンと操作の名前） */
struct ShortControl {
    const wchar_t* key;
    const wchar_t* label;
};
/** @brief 右上に出す操作（上から順に） */
constexpr ShortControl kShortControls[] = {
    { L"[{Attack}]", L"攻撃" },
    { L"[{Jump}]", L"ジャンプ" },
    { L"[{Dodge}]", L"回避" },
    { L"[{Skill}]", L"固有技" },
    { L"[{Shoot}]", L"射撃" },
    { L"[{ThrowWeapon}]", L"武器を投げて壊す（2本以上）" },
    { L"[{Pause}]", L"ポーズ・操作説明" },
};

// 奪える武器のうち、案内を出さない方に付ける小さな印と、武器の位置から上へ出す高さ
constexpr float kWeaponStealMarkScale = 1.0f;
constexpr Vector4 kWeaponStealMarkColor = { 0.55f, 0.9f, 1.0f, 0.7f };
constexpr float kWeaponStealPromptHeight = 2.0f; // 武器の位置から上へ出す高さ（ロックオンの印 y+1.6 と重ならない）

// 届く距離の外の武器を指す矢印の位置・揺れ方・色
constexpr float kPickupGuideMargin = 72.0f; // 画面端から矢印までの余白
constexpr float kPickupGuideLift = 38.0f; // 武器から矢印を浮かせる高さ
constexpr float kPickupGuideBob = 8.0f; // 上下に揺れる幅
constexpr float kPickupGuideSpeed = 4.0f; // 上下に揺れる速さ
constexpr float kPickupGuideBlinkSpeed = 6.0f; // 明滅の速さ
constexpr float kPickupGuideArrowScale = 2.0f; // 矢印の大きさ
constexpr float kPickupGuideTextScale = 1.0f; // 案内の文字の大きさ
constexpr float kPickupGuideTextGap = 4.0f; // 矢印と案内の文字の間
constexpr float kPickupGuideMinAlpha = 0.6f; // 明滅で一番薄くなる時の濃さ
constexpr Vector4 kPickupGuideColor = { 1.0f, 0.85f, 0.2f, 1.0f }; // 矢印と文字の色

// 画面に敵が1体もいない時に、画面外の一番近い敵の方向を画面端の矢印で指す
constexpr Vector4 kEnemyGuideColor = { 1.0f, 0.35f, 0.3f, 1.0f }; // 矢印と文字の色
constexpr float kEnemyGuideArrowScale = 2.2f; // 矢印の大きさ
constexpr float kEnemyGuideTextScale = 1.0f; // 案内の文字の大きさ
constexpr float kEnemyGuideBlinkSpeed = 5.0f; // 明滅の速さ
constexpr float kEnemyGuidePush = 10.0f; // 指す向きへ揺らす幅
constexpr float kEnemyGuidePushSpeed = 6.0f; // 指す向きへ揺らす速さ
constexpr float kScreenHalf = 0.5f; // 画面の中心を求める割合

/**
 * @brief 画面の外の点を、画面の中心から見た向きのまま画面端の内側へ寄せ、その向きの矢印の文字を返す
 * @param screen 画面座標（画面端の内側へ寄せた位置に書き換える）
 * @param direction 画面の中心から見た向き（画面端の内側まで縮めた長さ）を返す
 */
std::wstring ClampToScreenEdge(Vector2& screen, Vector2& direction) {
    const Vector2 center = { gameConstants::kScreenWidth * kScreenHalf, gameConstants::kScreenHeight * kScreenHalf };
    const Vector2 extent = { center.x - kPickupGuideMargin, center.y - kPickupGuideMargin };
    direction = { screen.x - center.x, screen.y - center.y };
    const float ratio = (std::max)({ std::abs(direction.x) / extent.x, std::abs(direction.y) / extent.y, FLT_EPSILON });
    direction = { direction.x / ratio, direction.y / ratio };
    screen = { center.x + direction.x, center.y + direction.y };
    return std::abs(direction.x) > std::abs(direction.y)
        ? (direction.x < 0.0f ? L"◀" : L"▶") : (direction.y < 0.0f ? L"▲" : L"▼");
}
// 収集物の個数表示
constexpr Vector2 kPickupCounterPosition = { 24.0f, 540.0f };
constexpr float kPickupCounterScale = 1.35f;
constexpr Vector4 kPickupCounterColor = { 0.3f, 0.9f, 1.0f, 1.0f };
} // namespace

/** @brief プレイヤーの進行位置に対応する操作目標を描画する */
void GamePlayScene::DrawStageGuide() {
    // 区画ごとの案内文はレベルJSONのui_text（トリガーのフラグで切り替わる）が担当する。ここは収集物の個数だけ
    int collectedPickups = 0;
    int totalPickups = 0;
    GetStageEditor().GetPickupCounts(collectedPickups, totalPickups);
    if (totalPickups <= 0) {
        return;
    }
    const std::wstring coreCount = L"エネルギーコア  "
        + std::to_wstring(collectedPickups) + L" / "
        + std::to_wstring(totalPickups);
    UILayout& layout = UILayout::Get(kLayoutName);
    const Vector2 counterPosition = layout.Pos("pickup_counter.pos", kPickupCounterPosition);
    fontRenderer_.DrawStringW(coreCount, counterPosition.x, counterPosition.y,
        layout.Float("pickup_counter.scale", kPickupCounterScale), layout.Color("pickup_counter.color", kPickupCounterColor));
}
/** @brief 今奪える武器を全部集める（ボスと道中の敵で、奪える距離が違う） */
std::vector<GamePlayScene::StealTarget> GamePlayScene::CollectStealTargets() const {
    std::vector<StealTarget> targets;
    if (enemy_ && enemy_->IsDefeated() && !weaponStealTriggered_ && !mainWeaponAbsorbing_ && !finisherActive_ && !enemySlice_.IsActive()) {
        targets.push_back({ enemy_, enemy_->GetWeaponPickupPosition(), CombatTune().weaponAbsorbRange });
    }
    for (const auto& weaponEnemy : weaponEnemies_) {
        if (weaponEnemy.hasWeapon && weaponEnemy.enemy->IsDefeated() && !weaponEnemy.weaponAcquired && !weaponEnemy.absorbing) {
            targets.push_back({ weaponEnemy.enemy, weaponEnemy.enemy->GetWeaponPickupPosition(), CombatTune().absorbRange });
        }
    }
    return targets;
}

/** @brief 届く距離の中で一番近いものの番号を返す（無ければ targets.size()） */
size_t GamePlayScene::FindNearestStealTarget(const std::vector<StealTarget>& targets) const {
    const Vector3& playerPosition = player_->GetPosition();
    size_t nearest = targets.size();
    float nearestDistanceSq = FLT_MAX;
    for (size_t i = 0; i < targets.size(); ++i) {
        const float distanceSq = DistanceSquaredXY(playerPosition, targets[i].position);
        if (distanceSq <= targets[i].range * targets[i].range && distanceSq < nearestDistanceSq) {
            nearestDistanceSq = distanceSq;
            nearest = i;
        }
    }
    return nearest;
}

/** @brief このフレームに奪う相手を1体だけ決める（1回押して2体から同時に奪わないように） */
void GamePlayScene::SelectStealTarget() {
    const std::vector<StealTarget> targets = CollectStealTargets();
    const size_t nearest = FindNearestStealTarget(targets);
    selectedStealOwner_ = nearest < targets.size() ? targets[nearest].owner : nullptr;
}

/** @brief レベルに置いた案内文を出すか（操作を覚える1面だけ。ランの外のエディタのテストプレイでは、どのステージでも出す） */
bool GamePlayScene::ShowsStageGuideText() const {
    const RunData* runData = RunData::GetInstance();
    return !runData->IsRunActive() || runData->IsTutorialActive() || runData->GetPlayFloor() == 0;
}

/** @brief 右上のランクの下に、よく使う操作だけの短い一覧を出す（ボタン名は最後に使った機器の表記） */
void GamePlayScene::DrawShortControls() {
    UILayout& layout = UILayout::Get(kLayoutName);
    const float scale = layout.Float("short_controls.scale", kShortControlsScale);
    const Vector2 anchor = layout.Pos("short_controls.pos", kShortControlsAnchor);
    const Vector4 keyColor = layout.Color("short_controls.key_color", kShortControlsKeyColor);
    const Vector4 textColor = layout.Color("short_controls.text_color", kShortControlsTextColor);
    // ボタン名の列の幅をそろえ、操作の名前を同じ位置から始める
    std::vector<std::wstring> keys;
    float keyWidth = 0.0f;
    float labelWidth = 0.0f;
    for (const ShortControl& control : kShortControls) {
        keys.push_back(input_->ExpandPrompts(control.key) + L" ");
        keyWidth = (std::max)(keyWidth, FontRenderer::MeasureStringW(keys.back(), scale).x);
        labelWidth = (std::max)(labelWidth, FontRenderer::MeasureStringW(control.label, scale).x);
    }
    const float lineHeight = FontRenderer::kCharH * scale + kShortControlsLineGap;
    const float width = keyWidth + labelWidth;
    const float height = lineHeight * static_cast<float>(keys.size()) - kShortControlsLineGap;
    // 右端と上端を合わせ、後ろに暗い板を敷いてから1行ずつ積む
    const float left = anchor.x - width;
    const float top = anchor.y;
    fontRenderer_.DrawPanel(left - kShortControlsPadding, top - kShortControlsPadding, width + kShortControlsPadding * 2.0f,
        height + kShortControlsPadding * 2.0f, layout.Color("short_controls.panel_color", kShortControlsPanelColor));
    for (size_t i = 0; i < keys.size(); ++i) {
        const float y = top + lineHeight * static_cast<float>(i);
        fontRenderer_.DrawStringW(keys[i], left, y, scale, keyColor);
        fontRenderer_.DrawStringW(kShortControls[i].label, left + keyWidth, y, scale, textColor);
    }
}

/** @brief 届く距離の外にある奪える武器を、武器の上で揺れる矢印か、画面の外なら画面端の矢印で指す（横スクロールのステージ用） */
void GamePlayScene::DrawWeaponPickupGuide(const Vector3& position) {
    UILayout& layout = UILayout::Get(kLayoutName);
    const Vector3& cam = camera_->GetTranslate();
    const Vector2 center = { gameConstants::kScreenWidth * kCenterFactor, gameConstants::kScreenHeight * kCenterFactor };
    Vector2 screen = center;
    sceneShared::WorldToScreen(position.x, position.y, cam.x, cam.y, screen.x, screen.y);
    const bool onScreen = screen.x >= 0.0f && screen.x <= gameConstants::kScreenWidth
        && screen.y >= 0.0f && screen.y <= gameConstants::kScreenHeight;
    std::wstring arrow = L"▼";
    if (onScreen) {
        // 武器の上で上下に揺らし、画面の端でも文字が切れない位置に収める
        screen.y -= kPickupGuideLift + std::sin(floorElapsedSeconds_ * kPickupGuideSpeed) * kPickupGuideBob;
        screen.x = std::clamp(screen.x, kPickupGuideMargin, gameConstants::kScreenWidth - kPickupGuideMargin);
        screen.y = std::clamp(screen.y, kPickupGuideMargin, gameConstants::kScreenHeight - kPickupGuideMargin);
    } else {
        // 画面の中心から武器への向きを、画面端の内側まで縮めた位置に、その向きの矢印を出す
        Vector2 direction = {};
        arrow = ClampToScreenEdge(screen, direction);
    }
    // 矢印と、その上の短い案内を明滅させて目を引く
    Vector4 color = layout.Color("pickup_guide.color", kPickupGuideColor);
    const float wave = (1.0f + std::sin(floorElapsedSeconds_ * kPickupGuideBlinkSpeed)) * kCenterFactor;
    color.w *= kPickupGuideMinAlpha + (1.0f - kPickupGuideMinAlpha) * wave;
    const float arrowScale = layout.Float("pickup_guide.arrow_scale", kPickupGuideArrowScale);
    const float textScale = layout.Float("pickup_guide.text_scale", kPickupGuideTextScale);
    const std::wstring label = L"武器を取れ";
    const Vector2 arrowSize = FontRenderer::MeasureStringW(arrow, arrowScale);
    const Vector2 labelSize = FontRenderer::MeasureStringW(label, textScale);
    const float labelX = std::clamp(screen.x - labelSize.x * kCenterFactor, 0.0f,
        (std::max)(0.0f, gameConstants::kScreenWidth - labelSize.x));
    const float labelY = (std::max)(0.0f, screen.y - arrowSize.y * kCenterFactor - kPickupGuideTextGap - labelSize.y);
    fontRenderer_.DrawStringW(label, labelX, labelY, textScale, color);
    fontRenderer_.DrawStringW(arrow, screen.x - arrowSize.x * kCenterFactor, screen.y - arrowSize.y * kCenterFactor, arrowScale, color);
}

/** @brief 画面に敵が1体もいない時に、画面外の一番近い敵の方向を画面端の矢印で指す（閉まった扉の向こうの敵は指さない） */
void GamePlayScene::DrawOffscreenEnemyGuide() {
    // 闘技場は専用のカメラなので出さない
    if (finalBoss_.IsActive()) {
        return;
    }
    // 生きていて見えている敵（ボスと道中の敵）を集める
    std::vector<const EnemyEntity*> enemies;
    if (enemy_ != nullptr && !enemy_->IsDefeated() && enemy_->IsVisible()) {
        enemies.push_back(enemy_);
    }
    for (const WeaponEnemyEntry& entry : weaponEnemies_) {
        if (entry.enemy != nullptr && !entry.enemy->IsDefeated() && entry.enemy->IsVisible()) {
            enemies.push_back(entry.enemy);
        }
    }
    // 1体でも画面に入っていれば出さない。入っていなければ、扉に塞がれていない一番近い敵を選ぶ
    const Vector3& cam = camera_->GetTranslate();
    const Vector3& playerPos = player_->GetPosition();
    const std::vector<AABB> gates = GetStageEditor().GetClosedGateColliders();
    const EnemyEntity* nearest = nullptr;
    float nearestDistance = FLT_MAX;
    Vector2 nearestScreen = {};
    for (const EnemyEntity* enemy : enemies) {
        const Vector3 position = enemy->GetPosition();
        Vector2 screen = {};
        sceneShared::WorldToScreen(position.x, position.y, cam.x, cam.y, screen.x, screen.y);
        if (screen.x >= 0.0f && screen.x <= gameConstants::kScreenWidth && screen.y >= 0.0f && screen.y <= gameConstants::kScreenHeight) {
            return;
        }
        // プレイヤーと敵の間に閉まった扉があれば、その敵は指さない（扉を開ける前に向こうへ誘わない）
        const float left = (std::min)(playerPos.x, position.x);
        const float right = (std::max)(playerPos.x, position.x);
        const bool blocked = std::any_of(gates.begin(), gates.end(), [left, right](const AABB& gate) {
            return gate.max.x > left && gate.min.x < right;
        });
        const float distance = DistanceSquaredXY(playerPos, position);
        if (!blocked && distance < nearestDistance) {
            nearest = enemy;
            nearestDistance = distance;
            nearestScreen = screen;
        }
    }
    if (nearest == nullptr) {
        return;
    }
    // 画面端に向きの矢印を出し、指す向きへ小さく揺らして明滅させる
    Vector2 direction = {};
    Vector2 screen = nearestScreen;
    const std::wstring arrow = ClampToScreenEdge(screen, direction);
    const float length = (std::max)(std::sqrt(direction.x * direction.x + direction.y * direction.y), FLT_EPSILON);
    const float push = std::sin(floorElapsedSeconds_ * kEnemyGuidePushSpeed) * kEnemyGuidePush;
    screen = { screen.x + direction.x / length * push, screen.y + direction.y / length * push };
    UILayout& layout = UILayout::Get(kLayoutName);
    Vector4 color = layout.Color("enemy_guide.color", kEnemyGuideColor);
    const float wave = (1.0f + std::sin(floorElapsedSeconds_ * kEnemyGuideBlinkSpeed)) * kCenterFactor;
    color.w *= kPickupGuideMinAlpha + (1.0f - kPickupGuideMinAlpha) * wave;
    const float arrowScale = layout.Float("enemy_guide.arrow_scale", kEnemyGuideArrowScale);
    const float textScale = layout.Float("enemy_guide.text_scale", kEnemyGuideTextScale);
    const std::wstring label = L"敵";
    const Vector2 arrowSize = FontRenderer::MeasureStringW(arrow, arrowScale);
    const Vector2 labelSize = FontRenderer::MeasureStringW(label, textScale);
    fontRenderer_.DrawStringW(arrow, screen.x - arrowSize.x * kCenterFactor, screen.y - arrowSize.y * kCenterFactor, arrowScale, color);
    // 文字は矢印より画面の内側（中心の方）に置く
    const Vector2 inward = { -direction.x / length, -direction.y / length };
    const float gap = (arrowSize.x + labelSize.x) * kCenterFactor + kPickupGuideTextGap;
    fontRenderer_.DrawStringW(label, screen.x + inward.x * gap - labelSize.x * kCenterFactor,
        screen.y + inward.y * gap - labelSize.y * kCenterFactor, textScale, color);
}

/** @brief 奪える武器に印を出し、届く距離の一番近い1つだけに回収の操作を出す */
void GamePlayScene::DrawWeaponStealPrompts(float promptScale, const Vector4& promptColor) {
    UILayout& layout = UILayout::Get(kLayoutName);
    const float markScale = layout.Float("steal_mark.scale", kWeaponStealMarkScale);
    const Vector4 markColor = layout.Color("steal_mark.color", kWeaponStealMarkColor);
    // 押せば奪えるもの（距離の中）のうち、一番近いものを選ぶ
    const std::vector<StealTarget> targets = CollectStealTargets();
    const size_t nearest = FindNearestStealTarget(targets);
    // 選んだものには操作を、ほかは小さな印だけを、武器の上へ中央そろえで出す
    const Vector3& cam = camera_->GetTranslate();
    const std::wstring prompt = input_->ExpandPrompts(L"[{Steal}] 武器を回収");
    const std::wstring mark = L"▼";
    for (size_t i = 0; i < targets.size(); ++i) {
        // 闘技場では専用の投影を使い、横スクロール用の印との重複を避ける。
        if (finalBoss_.UsesArenaCamera()) {
            DrawArenaWeaponStealPrompt(targets[i].position, i == nearest, promptScale, promptColor);
            continue;
        }
        // 届く距離の外の武器には、拾いに行く先が分かるように大きな矢印を出す
        if (DistanceSquaredXY(player_->GetPosition(), targets[i].position) > targets[i].range * targets[i].range) {
            DrawWeaponPickupGuide(targets[i].position);
            continue;
        }
        float sx = 0.0f;
        float sy = 0.0f;
        sceneShared::WorldToScreen(targets[i].position.x, targets[i].position.y + kWeaponStealPromptHeight, cam.x, cam.y, sx, sy);
        const bool selected = i == nearest;
        const std::wstring& text = selected ? prompt : mark;
        const float scale = selected ? promptScale : markScale;
        fontRenderer_.DrawStringW(text, sx - FontRenderer::MeasureStringW(text, scale).x * kCenterFactor, sy, scale,
            selected ? promptColor : markColor);
    }
}
/** @brief 同じ武器で当て続けて効きが落ちている時に、持ち替えを促す警告を出す */
void GamePlayScene::DrawWeaponFatigueWarning() {
    UILayout& layout = UILayout::Get(kLayoutName);
    const float fatigueDetailOffsetY = layout.Float("fatigue.detail_offset_y", kFatigueDetailOffsetY);
    const Vector4 fatigueMildColor = layout.Color("fatigue.mild_color", kFatigueMildColor);
    const Vector4 fatigueSevereColor = layout.Color("fatigue.severe_color", kFatigueSevereColor);
    // 今の武器の疲労で威力が落ちていなければ出さない
    const float mult = weaponFatigue_.DamageMult(weaponFatigue_.LastIndex());
    if (mult >= 1.0f) {
        return;
    }
    // 威力の落ち具合を 0〜1 にし、一定以上なら強い警告にする
    const float minMult = CombatTuning::GetInstance()->Get().weaponFatigueMinDamageMult;
    const float severity = minMult < 1.0f ? std::clamp((1.0f - mult) / (1.0f - minMult), 0.0f, 1.0f) : 0.0f;
    const bool severe = severity >= kFatigueSevereThreshold;
    // 持ち替え先として一番休めている武器（銃を含む）を探し、押すボタンまで具体的に示す
    const auto* wm = WeaponManager::GetInstance();
    int bestSlot = -1;
    float bestFatigue = 1.0f;
    for (int slot = 0; slot < WeaponManager::kSlotCount; ++slot) {
        const int weaponIndex = wm->GetSlotWeaponIndex(slot);
        if (weaponIndex < 0 || weaponIndex == weaponFatigue_.LastIndex() || weaponIndex >= weaponFatigue_.Count()) {
            continue;
        }
        if (weaponFatigue_.At(weaponIndex) < bestFatigue) {
            bestFatigue = weaponFatigue_.At(weaponIndex);
            bestSlot = slot;
        }
    }
    // 銃のほうが休めていれば射撃を勧める
    const int gunIndex = WeaponFatigueTracker::GunIndex();
    const bool suggestGun = gunIndex != weaponFatigue_.LastIndex() && gunIndex < weaponFatigue_.Count()
        && (bestSlot < 0 || weaponFatigue_.At(gunIndex) < bestFatigue);
    // 押す入力は最後に使った機器の表記にし、持ち替え先の武器の名前も添える
    const std::wstring suggestion = (!suggestGun && bestSlot >= 0)
        ? input_->ExpandPrompts(L"[{Slot" + std::to_wstring(bestSlot + 1) + L"}] ")
            + WeaponTypeLabel(wm->GetList()[wm->GetSlotWeaponIndex(bestSlot)].type) + L"に持ち替えろ"
        : input_->ExpandPrompts(L"[{Shoot}] 射撃を混ぜろ");
    // 強い警告は点滅させ、見出しと 威力% と勧める操作 を2行で出す
    Vector2 pos = layout.Pos("fatigue.pos", kFatigueWarningPosition);
    // ボスの体力バーが出ている間は、重ならないようバーの下へ下げる
    const bool stageBossBar = enemy_ != nullptr && bossDirector_.IsEngaged() && !enemy_->IsDefeated();
    if (stageBossBar || finalBoss_.IsActive()) {
        pos.y = (std::max)(pos.y, layout.Float("fatigue.boss_bar_y", kFatigueWarningBossBarY));
    }
    Vector4 color = severe ? fatigueSevereColor : fatigueMildColor;
    if (severe) {
        const float blink = 0.5f + 0.5f * std::sin(floorElapsedSeconds_ * kFatigueBlinkSpeed);
        color.w = kFatigueBlinkMinAlpha + (1.0f - kFatigueBlinkMinAlpha) * blink;
    }
    // 各行の実測幅で中央をそろえる
    const std::wstring title = severe ? L"同じ武器ではもう効かない!" : L"同じ武器で効きが落ちてきた";
    const int percent = static_cast<int>(std::round(mult * kPercent));
    const std::wstring detail = L"威力 " + std::to_wstring(percent) + L"%    " + suggestion;
    const float titleScale = layout.Float("fatigue.title_scale", kFatigueTitleScale);
    const float detailScale = layout.Float("fatigue.detail_scale", kFatigueDetailScale);
    fontRenderer_.DrawStringW(title, pos.x - FontRenderer::MeasureStringW(title, titleScale).x * kCenterFactor,
        pos.y, titleScale, color);
    fontRenderer_.DrawStringW(detail, pos.x - FontRenderer::MeasureStringW(detail, detailScale).x * kCenterFactor,
        pos.y + fatigueDetailOffsetY, detailScale, color);
}
/** @brief 左上にステージ名の見出しを描画する（武器は左下の武器スロットHUDに出すので一覧は出さない） */
void GamePlayScene::DrawWeaponListPanel() {
    UILayout& layout = UILayout::Get(kLayoutName);
    const Vector2 weaponHudAnchor = layout.Pos("weapon_list.pos", kDefaultHudWeaponAnchor);
    sceneShared::DrawHudHeader(fontRenderer_, L"メインステージ", weaponHudAnchor);
}
} // namespace engine::game

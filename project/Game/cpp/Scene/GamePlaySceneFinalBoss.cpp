/**
 * @file GamePlaySceneFinalBoss.cpp
 * @brief GamePlayScene のうち、ラスボス戦（FinalBossBattle）へのつなぎ（用意・演出中の更新・闘技場のカメラ・ボスの攻撃の受け口）
 * @note 分割ファイル。クラスは GamePlayScene のまま、ラスボス戦まわりの定義だけをここに置く
 */
#include "GamePlayScene.h"
#include "EngineContext.h"
#include "StageEditorPlaytest.h"
#include "CrashBreadcrumbs.h"
#include "GameConstants.h"
#include "HitEffectLibrary.h"
#include "RunData.h"
#include "SlashMark.h"
#include "StageEditor.h"
#include "WinApp.h"
#include <algorithm>
#include <cmath>

namespace engine::game {
namespace {
// 刀の案内の配置と揺れ方をまとめる。
constexpr float kPickupHalf = 0.5f; ///< 中央をとるための半分の割合
constexpr float kPickupWidth = static_cast<float>(WinApp::kClientWidth);
constexpr float kPickupHeight = static_cast<float>(WinApp::kClientHeight);
constexpr Vector2 kPickupCenter = { kPickupWidth * kPickupHalf, kPickupHeight * kPickupHalf };
constexpr float kPickupMargin = 48.0f; ///< 矢印を画面の端からこれだけ内側に置く
constexpr float kPickupScale = 2.0f; ///< 矢印の文字の大きさ
constexpr float kPickupLift = 44.0f; ///< 画面内の時、刀からこれだけ上に矢印を出す
constexpr float kPickupBob = 8.0f; ///< 矢印が上下に揺れる幅
constexpr float kPickupSpeed = 4.0f; ///< 矢印が上下に揺れる速さ（ラジアン毎秒）
constexpr float kPickupPromptGap = 28.0f; ///< 矢印から回収の操作の文字までの間
constexpr float kPickupEpsilon = 0.0001f; ///< これ以下はカメラの後ろ・方向なしとみなす
constexpr Vector4 kPickupColor = { 1.0f, 0.85f, 0.25f, 1.0f }; ///< 矢印の色
} // namespace

/** @brief カメラ行列から刀を投影し、画面内の目印か画面端の方向案内を積む。 */
void GamePlayScene::DrawArenaWeaponStealPrompt(const Vector3& position, bool selected,
    float promptScale, const Vector4& promptColor) {
    // 行ベクトルの規約で透視除算前の座標を求める。
    const Matrix4x4 matrix = camera_->GetViewProjectionMatrix();
    const Vector3 clip = TransformPoint(position, matrix);
    const float w = position.x * matrix.m[0][3] + position.y * matrix.m[1][3]
        + position.z * matrix.m[2][3] + matrix.m[3][3];
    if (!std::isfinite(clip.x) || !std::isfinite(clip.y) || !std::isfinite(clip.z) || !std::isfinite(w)) {
        return;
    }
    const bool onScreen = w > kPickupEpsilon && clip.z >= 0.0f && clip.z <= w
        && std::abs(clip.x) <= w && std::abs(clip.y) <= w;
    Vector2 anchor = kPickupCenter;
    std::wstring arrow = L"▼";
    const Vector2 extent = { kPickupCenter.x - kPickupMargin, kPickupCenter.y - kPickupMargin };
    if (onScreen) {
        // 刀の上で揺らし、画面境界でも文字が見える位置に収める。
        anchor.x += clip.x / w * kPickupCenter.x;
        anchor.y -= clip.y / w * kPickupCenter.y + kPickupLift
            + std::sin(floorElapsedSeconds_ * kPickupSpeed) * kPickupBob;
        anchor.x = std::clamp(anchor.x, kPickupMargin, kPickupWidth - kPickupMargin);
        anchor.y = std::clamp(anchor.y, kPickupMargin, kPickupHeight - kPickupMargin);
    } else {
        // 背後では透視除算による左右反転を避け、真後ろは下端へ案内する。
        Vector2 direction = { clip.x * kPickupCenter.x, -clip.y * kPickupCenter.y };
        if (w <= kPickupEpsilon) {
            direction.y = (std::max)(std::abs(direction.y), kPickupEpsilon);
        }
        const float ratio = (std::max)(std::abs(direction.x) / extent.x, std::abs(direction.y) / extent.y);
        if (ratio <= kPickupEpsilon) {
            direction = { 0.0f, extent.y };
        } else {
            direction.x /= ratio;
            direction.y /= ratio;
        }
        anchor.x += direction.x;
        anchor.y += direction.y;
        arrow = std::abs(direction.x) > std::abs(direction.y)
            ? (direction.x < 0.0f ? L"◀" : L"▶") : (direction.y < 0.0f ? L"▲" : L"▼");
    }
    // 毎フレームの文字予約へ矢印と回収操作を積む。
    const Vector2 arrowSize = FontRenderer::MeasureStringW(arrow, kPickupScale);
    fontRenderer_.DrawStringW(arrow, anchor.x - arrowSize.x * kPickupHalf,
        anchor.y - arrowSize.y * kPickupHalf, kPickupScale, kPickupColor);
    if (selected && onScreen) {
        const std::wstring prompt = input_->ExpandPrompts(L"[{Steal}] 武器を回収");
        const Vector2 size = FontRenderer::MeasureStringW(prompt, promptScale);
        const float x = std::clamp(anchor.x - size.x * kPickupHalf, kPickupMargin,
            (std::max)(kPickupMargin, kPickupWidth - kPickupMargin - size.x));
        const float y = std::clamp(anchor.y + kPickupPromptGap, kPickupMargin,
            (std::max)(kPickupMargin, kPickupHeight - kPickupMargin - size.y));
        fontRenderer_.DrawStringW(prompt, x, y, promptScale, promptColor);
    }
}

/** @brief ラスボス戦のレベルを開いているか */
bool GamePlayScene::IsFinalBossMode() const {
    return RunData::GetInstance()->IsFinalBossPending();
}

/** @brief ラスボス戦の進行役を用意する（Initialize の最後に、ラスボス戦の時だけ） */
void GamePlayScene::InitializeFinalBoss() {
    if (!IsFinalBossMode()) {
        return;
    }
    FinalBossBattle::Links links;
    links.modelCommon = modelCommon_.get();
    links.spriteCommon = spriteCommon_.get();
    links.particles = pm_;
    links.font = &fontRenderer_;
    links.input = input_;
    links.player = player_.get();
    links.shaker = &cameraShaker_;
    links.rng = &rng_;
    links.strikePlayer = [this](const AABB& box, const Vector3& effectPosition, int damage) {
        return StrikePlayerFromBoss(box, effectPosition, damage);
    };
    finalBoss_.Initialize(links);
    CrashBreadcrumbs::Add("game", "ラスボス戦を用意した");
}

/** @brief 演出の途中でプレイヤーの操作を止めている間の1フレーム（止めていなければ false） */
bool GamePlayScene::UpdateFinalBossCinematic() {
    if (!finalBoss_.IsActive() || !finalBoss_.BlocksGameplay()) {
        return false;
    }
    // 敵と仕掛けも止め、HUD の文字は出さないので文字コマンドの破棄だけ行う
    GetStageEditor().SetGameplayHeld(true);
    fontRenderer_.Reset();
    finalBoss_.UpdateCinematic();
    UpdateCamera();
    player_->RefreshVisualTransforms();
    // フィニッシャーで段階が変わって演出に入った時も、切断の演出を進めて本体を戻す（戻さないと演出の間ボスが消えたままになる）
    const float dt = engine::Services().time->GetDeltaTime();
    enemySlice_.Update(dt, camera_.get());
    RestoreEnemyAfterSlice();
    SlashMark::GetInstance()->Update(dt);
    return true;
}

/** @brief 闘技場のカメラにする（やられ演出の間は横からのカメラに戻す。使ったら true） */
bool GamePlayScene::UpdateArenaCamera() {
    if (!finalBoss_.UsesArenaCamera() || playerDefeat_.IsActive()) {
        return false;
    }
    finalBoss_.ApplyCamera(*camera_);
    // カメラ揺れは決めた位置へ直接足す
    const Vector3 shake = cameraShaker_.Update(gameConstants::kFrameDeltaTime);
    if (shake.x != 0.0f || shake.y != 0.0f) {
        const Vector3 cam = camera_->GetTranslate();
        camera_->SetTranslate({ cam.x + shake.x, cam.y + shake.y, cam.z });
    }
    // 空も闘技場と同じだけ回す（回り込んだ時に景色が流れて見えるように）
    skydome_->SetRotationOffsetY(skyRotOffsetY_ + finalBoss_.GetSkyYaw());
    shadowManager_->Update(objectCommon_->GetLightDirection());
    Object3d::SetLightViewProjection(shadowManager_->GetLightViewProjection());
    return true;
}

/** @brief ラスボスの攻撃の判定をプレイヤーへ当てる（当たったら true。回避中・無敵中は当たらない） */
bool GamePlayScene::StrikePlayerFromBoss(const AABB& box, const Vector3& effectPosition, int damage) {
    if (!Collision::CheckCollision(player_->GetCollider().aabb, box)) {
        return false;
    }
    // 回避で体を抜けたらジャスト回避、無敵中は素通り
    if (TryJustDodge(effectPosition) || player_->IsInvincible()) {
        return false;
    }
    RunData::GetInstance()->TakeDamage(damage);
    player_->OnHit();
#ifdef USE_IMGUI
    stagePlaytest::Hit(GetStageEditor(), player_->GetPosition());
#endif
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Impact("enemy_swing");
    fx.Sparks("player_hit", player_->GetPosition());
    return true;
}

} // namespace engine::game

/**
 * @file GamePlaySceneFallRescue.cpp
 * @brief GamePlayScene のうち、床を抜けてステージの一番下より落ちた時に、最後に立っていた床へ戻す救済
 */
#include "GamePlayScene.h"
#include "HitEffectLibrary.h"

namespace engine::game {
namespace {
constexpr float kFallRescueDrop = 8.0f; // 最後に立っていた床からこれだけ下へ落ちたら、ステージの範囲を測って調べる
constexpr float kFallRescueMargin = 6.0f; // ステージの一番下からこれだけ下へ抜けたら、床を抜けたとみなす
constexpr float kFallRescueLift = 0.5f; // 戻す時に床から少し浮かせる高さ（床の中へ埋めないように）
} // namespace

/** @brief 床を抜けてステージの一番下より落ちたら、最後に立っていた床へ戻す（毎フレーム、当たり判定の後に呼ぶ） */
void GamePlayScene::UpdateFallRescue() {
    const Vector3 position = player_->GetPosition();
    // 床に立っている間は、その位置を戻す先として覚える
    if (player_->IsOnGround()) {
        lastSafeGround_ = position;
        hasSafeGround_ = true;
        return;
    }
    // 毎フレーム範囲を測ると重いので、戻す先から大きく落ちた時だけ調べる
    if (!hasSafeGround_ || position.y > lastSafeGround_.y - kFallRescueDrop) {
        return;
    }
    const StageBounds bounds = GetStageBounds();
    if (!bounds.fromLevel || position.y > bounds.bottom - kFallRescueMargin) {
        return;
    }
    // 自動テストプレイなら、原因を追えるよう直前の様子と一緒に知らせてから戻す
    ReportPlayerFall(position);
    player_->PlaceAfterFallRescue({ lastSafeGround_.x, lastSafeGround_.y + kFallRescueLift, lastSafeGround_.z });
    player_->RefreshVisualTransforms();
    // 戻ったことが分かるよう、着地の土煙を出す
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Ring("jump_smoke", lastSafeGround_);
}

} // namespace engine::game

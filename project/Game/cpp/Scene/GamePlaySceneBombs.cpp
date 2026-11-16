/**
 * @file GamePlaySceneBombs.cpp
 * @brief 爆弾（爆発する樽）を攻撃で吹き飛ばし、ぶつかった所で爆発させる処理
 */
#include "GamePlayScene.h"
#include "GameConstants.h"
#include "GameFlags.h"
#include "HitEffectLibrary.h"
#include "RunData.h"
#include "StageEditor.h"
#include <cmath>
#include <string>

namespace engine::game {
namespace {
/** @brief 爆弾とプレイヤーの横のずれがこれ以下なら、飛ばす向きはプレイヤーの向きで決める */
constexpr float kKickDirectionDeadZone = 0.05f;
} // namespace

/** @brief 攻撃を当てた爆弾を吹き飛ばす（吹き飛ばせない物・飛ばす枠が無い時は false） */
bool GamePlayScene::TryKickBomb(const BreakableRef& barrel, const BreakableSettings& settings, const BombPayload& payload) {
    const BombKickTuning& tuning = BombKickTuningStore::Get();
    // 爆風を持たない物（壁など）と、武器を指定して壊す物は吹き飛ばさない
    if (!tuning.enabled || settings.radius <= 0.0f || !settings.requiredWeapon.empty()) {
        return false;
    }
    // 近接でも銃でも、プレイヤーから見て爆弾の向こう側へ飛ばす（真上や真下で向きが決まらない時は向いている方）
    const float offsetX = barrel.GetPosition().x - player_->GetPosition().x;
    const float launchDirX = std::abs(offsetX) > kKickDirectionDeadZone ? offsetX : player_->GetLastDirX();
    if (!kickedBombs_.Launch(barrel.GetPosition(), launchDirX, settings.scale, payload)) {
        return false;
    }
    // 置いてあった樽は消し、飛んでいる見た目へ入れ替える
    barrel.Destroy();
    GameFlags::GetInstance()->SetFlag("broken_" + std::string(settings.name), true);
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Impact(tuning.launchImpact);
    fx.Star("breakable_damage", barrel.GetPosition());
    return true;
}

/** @brief 爆風の範囲にいるプレイヤーと敵へダメージを与える */
void GamePlayScene::ApplyBombBlast(const Vector3& position, const BombPayload& payload) {
    const float radiusSq = payload.radius * payload.radius;
    // プレイヤーはジャスト回避か無敵なら巻き込まれない
    if (DistanceSquaredXY(player_->GetPosition(), position) <= radiusSq && !TryJustDodge(position) && !player_->IsInvincible()) {
        RunData::GetInstance()->TakeDamage(payload.playerDamage);
        player_->OnHit();
    }
    if (enemy_ && !enemy_->IsDefeated() && DistanceSquaredXY(enemy_->GetPosition(), position) <= radiusSq) {
        enemy_->TakeDamage(payload.enemyDamage);
    }
    for (auto& entry : weaponEnemies_) {
        if (!entry.enemy->IsDefeated() && DistanceSquaredXY(entry.enemy->GetPosition(), position) <= radiusSq) {
            entry.enemy->TakeDamage(payload.enemyDamage);
        }
    }
}

/** @brief 生きている敵の当たり判定のどれかに、球が重なったか */
bool GamePlayScene::BombTouchesEnemy(const Vector3& position, float radius) const {
    // 敵の箱を爆弾の半径だけ広げ、中心が入ったかで調べる
    const auto touches = [&](const Vector3& enemyPosition) {
        const AABB box = EnemyHitBoxAt(enemyPosition);
        return position.x >= box.min.x - radius && position.x <= box.max.x + radius
            && position.y >= box.min.y - radius && position.y <= box.max.y + radius;
    };
    if (enemy_ && !enemy_->IsDefeated() && enemy_->IsVisible() && touches(enemy_->GetPosition())) {
        return true;
    }
    for (const auto& entry : weaponEnemies_) {
        if (!entry.enemy->IsDefeated() && touches(entry.enemy->GetPosition())) {
            return true;
        }
    }
    return false;
}

/** @brief 飛んでいる爆弾を進め、ぶつかった爆弾を爆発させる */
void GamePlayScene::UpdateKickedBombs() {
    // 飛んでいる爆弾が無ければ、当たり判定の一覧を作らずに済ませる
    if (!kickedBombs_.HasActive()) {
        return;
    }
    const BombKickTuning& tuning = BombKickTuningStore::Get();
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    const auto hitsEnemy = [this](const Vector3& position, float radius) {
        return BombTouchesEnemy(position, radius);
    };
    const auto emitTrail = [&](const Vector3& position) {
        fx.Sparks(tuning.trailSparks, position);
    };
    const std::vector<BombDetonation> detonations = kickedBombs_.Update(
        gameConstants::kFrameDeltaTime, GetStageEditor().GetSolidColliders(), hitsEnemy, emitTrail);
    // ぶつかった所で、置いてあった時と同じ爆風と演出を出す
    for (const BombDetonation& detonation : detonations) {
        ApplyBombBlast(detonation.position, detonation.payload);
        EmitBreakableDestructionEffect(detonation.position, detonation.position, detonation.payload.radius);
    }
}

} // namespace engine::game

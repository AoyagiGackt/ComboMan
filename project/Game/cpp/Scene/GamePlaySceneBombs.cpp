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
#include <algorithm>
#include <cmath>
#include <string>

namespace engine::game {
namespace {
/** @brief 爆弾とプレイヤーの横のずれがこれ以下なら、飛ばす向きはプレイヤーの向きで決める */
constexpr float kKickDirectionDeadZone = 0.05f;
/** @brief 飛んでくる壁に当たったボスののけぞる秒数（ボスは吹き飛ばさない） */
constexpr float kWallBossHitstunSeconds = 0.4f;
} // namespace

/** @brief プレイヤーから見て対象の向こう側の向き（真上や真下で決まらない時は向いている方） */
float GamePlayScene::AwayFromPlayerDirX(const Vector3& position) const {
    const float offsetX = position.x - player_->GetPosition().x;
    return std::abs(offsetX) > kKickDirectionDeadZone ? offsetX : player_->GetLastDirX();
}

/** @brief 攻撃を当てた爆弾を吹き飛ばす（吹き飛ばせない物・飛ばす枠が無い時は false） */
bool GamePlayScene::TryKickBomb(const BreakableRef& barrel, const BreakableSettings& settings, const BombPayload& payload) {
    const BombKickTuning& tuning = BombKickTuningStore::Get();
    // 爆風を持たない物（壁など）は一撃では吹き飛ばさない（壁は壊れた時に LaunchBrokenWall で飛ばす）
    if (!tuning.enabled || settings.radius <= 0.0f || settings.isWall) {
        return false;
    }
    // 近接でも銃でも、プレイヤーから見て爆弾の向こう側へ飛ばす
    if (!kickedBombs_.Launch(barrel.GetPosition(), AwayFromPlayerDirX(barrel.GetPosition()), settings.scale, payload)) {
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
    const auto sweepEnemies = [this](const AABB& box, float dirX, std::vector<const void*>& alreadyHit) {
        SweepEnemiesWithWall(box, dirX, alreadyHit);
    };
    const auto emitTrail = [&](const Vector3& position) {
        fx.Sparks(tuning.trailSparks, position);
    };
    const std::vector<BombDetonation> detonations = kickedBombs_.Update(
        gameConstants::kFrameDeltaTime, GetStageEditor().GetSolidColliders(), hitsEnemy, sweepEnemies, emitTrail);
    // ぶつかった所で、置いてあった時と同じ爆風と演出を出す（壁は爆風なしで砕けるだけ）
    for (const BombDetonation& detonation : detonations) {
        if (detonation.kind == KickedObjectKind::kBomb) {
            ApplyBombBlast(detonation.position, detonation.payload);
        }
        EmitBreakableDestructionEffect(detonation.position, detonation.position, detonation.payload.radius);
    }
}

/** @brief 壊した壁をプレイヤーの反対側へ吹き飛ばす（飛ばす枠が無い時は false） */
bool GamePlayScene::LaunchBrokenWall(const BreakableRef& wall, const BreakableSettings& settings) {
    const BombKickTuning& tuning = BombKickTuningStore::Get();
    if (!tuning.enabled) {
        return false;
    }
    const BombPayload noBlast = {};
    if (!kickedBombs_.Launch(wall.GetPosition(), AwayFromPlayerDirX(wall.GetPosition()), settings.scale, noBlast,
            KickedObjectKind::kWall)) {
        return false;
    }
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Impact(tuning.launchImpact);
    return true;
}

/** @brief 飛んでいる壁の箱に重なった敵を、まだ巻き込んでいなければ吹き飛ばす（ボスはダメージ、道中の敵は倒す） */
void GamePlayScene::SweepEnemiesWithWall(const AABB& box, float dirX, std::vector<const void*>& alreadyHit) {
    const int damage = BombKickTuningStore::Get().wallEnemyDamage;
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    // 一度巻き込んだ敵は、同じ壁でもう一度は飛ばさない
    const auto firstContact = [&](const EnemyEntity* enemy, const AABB& enemyBox) {
        if (std::find(alreadyHit.begin(), alreadyHit.end(), enemy) != alreadyHit.end()) {
            return false;
        }
        if (!Collision::CheckCollision(box, enemyBox)) {
            return false;
        }
        alreadyHit.push_back(enemy);
        return true;
    };
    // ボスは吹き飛ばさず、ダメージとのけぞりだけ与える
    if (enemy_ && !enemy_->IsDefeated() && enemy_->IsVisible() && firstContact(enemy_, GetEnemyHitBox())) {
        enemy_->TakeDamage(damage);
        enemy_->ApplyHitstun(kWallBossHitstunSeconds, true);
        fx.Star("breakable_damage", enemy_->GetPosition());
    }
    // 道中の敵は倒してから壁と同じ向きへ回りながら打ち飛ばす（生きたまま壁の向こうへ落ちると部屋の残りの敵に数えられて扉が開かない）
    for (auto& entry : weaponEnemies_) {
        EnemyEntity* enemy = entry.enemy;
        if (enemy->IsDefeated() || !firstContact(enemy, EnemyHitBoxAt(enemy->GetPosition()))) {
            continue;
        }
        enemy->TakeDamage(enemy->GetHp());
        enemy->ApplyHomeRun(dirX);
        fx.Star("breakable_damage", enemy->GetPosition());
    }
}

} // namespace engine::game

/**
 * @file GamePlaySceneActionFeel.cpp
 * @brief GamePlayScene のうち、アクションの手応え（吹き飛んだ敵どうしの衝突・壁と床の跳ね返りの演出・部屋の最後の1体・ジャスト回避の後の遅さ）
 * @note 調整値は Resources/Config/action_feel.json（ActionFeelTuning）。演出の名前は hit_effects.json、音の名前は sounds.json
 */
#include "GamePlayScene.h"
#include "ActionFeelTuning.h"
#include "Collision.h"
#include "GameSounds.h"
#include "HitEffectLibrary.h"
#include "StageEditor.h"
#include "TimeManager.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace engine::game {
namespace {
constexpr float kCollideHitstunSeconds = 0.3f; // 吹き飛んだ敵に当てられた敵ののけぞりの秒数
constexpr float kBossCollideHitstunSeconds = 0.2f; // ボスは吹き飛ばさず、短くのけぞらせるだけ
constexpr float kHalf = 0.5f;
// 演出の名前（hit_effects.json）と音の名前（sounds.json）
constexpr const char* kCollideEffect = "enemy_collide";
constexpr const char* kWallBounceEffect = "wall_bounce";
constexpr const char* kFloorBounceEffect = "floor_bounce";
constexpr const char* kRoomClearEffect = "room_clear";
constexpr const char* kDodgeSlowEffect = "dodge_slow";
constexpr const char* kCollideSound = "enemyCollide";
constexpr const char* kWallBounceSound = "wallBounce";
constexpr const char* kFloorBounceSound = "floorBounce";
constexpr const char* kRoomClearSound = "roomClear";
constexpr const char* kDodgeSlowSound = "dodgeSlow";

/** @brief 2点の真ん中 */
Vector3 Midpoint(const Vector3& a, const Vector3& b) {
    return { (a.x + b.x) * kHalf, (a.y + b.y) * kHalf, (a.z + b.z) * kHalf };
}
} // namespace

/** @brief アクションの手応え（吹き飛んだ敵どうしの衝突・跳ね返りの演出・部屋の最後の1体）を1フレーム進める */
void GamePlayScene::UpdateActionFeel() {
    UpdateEnemyBounceEffects();
    UpdateFlyingEnemyCollisions();
    UpdateRoomClearFinale();
}

/** @brief 吹き飛んで横へ速く飛んでいる敵か（倒れて打ち飛ばされている敵も含む） */
bool GamePlayScene::IsFlyingEnemy(const EnemyEntity& enemy) const {
    const bool launchedAway = enemy.IsBlownAway() || enemy.IsDefeated();
    return launchedAway && enemy.IsVisible() && std::abs(enemy.GetKnockVelX()) >= ActionFeel().collideMinSpeed;
}

/** @brief 吹き飛んだ敵がほかの敵に当たったら、相手にダメージを与えて同じ向きへ飛ばす（連鎖は決めた深さまで） */
void GamePlayScene::UpdateFlyingEnemyCollisions() {
    const ActionFeelTuning& feel = ActionFeel();
    if (!feel.collideEnabled || TimeManager::GetInstance()->IsHitStopped()) {
        return;
    }
    // 飛び終えた敵（と、もういない敵）の記録を消す。記録の鍵は比べるだけで、指す先には触らない
    std::vector<const EnemyEntity*> flying;
    for (const auto& entry : weaponEnemies_) {
        if (entry.enemy != nullptr && IsFlyingEnemy(*entry.enemy)) {
            flying.push_back(entry.enemy);
        }
    }
    const auto isStillFlying = [&](const EnemyEntity* enemy) {
        return std::find(flying.begin(), flying.end(), enemy) != flying.end();
    };
    std::erase_if(flyerHits_, [&](const auto& record) {
        return !isStillFlying(record.first);
    });
    std::erase_if(flyerChain_, [&](const auto& record) {
        return !isStillFlying(record.first) && !flyerHits_.contains(record.first);
    });

    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    const float playerX = player_->GetPosition().x;
    for (auto& flyerEntry : weaponEnemies_) {
        EnemyEntity* flyer = flyerEntry.enemy;
        if (flyer == nullptr || !IsFlyingEnemy(*flyer)) {
            continue;
        }
        const int chain = flyerChain_.contains(flyer) ? flyerChain_[flyer] : 0;
        std::vector<const EnemyEntity*>& alreadyHit = flyerHits_[flyer];
        const AABB flyerBox = EnemyHitBoxAt(flyer->GetPosition());
        const float dirX = flyer->GetKnockVelX() > 0.0f ? 1.0f : -1.0f;
        // 1体に当たるごとに、当たった所で演出を出して飛んでいた敵の勢いを落とす
        const auto onContact = [&](const EnemyEntity& target) {
            const Vector3 contact = Midpoint(flyer->GetPosition(), target.GetPosition());
            fx.Impact(kCollideEffect);
            fx.Ring(kCollideEffect, contact);
            fx.Sparks(kCollideEffect, contact);
            gameSounds::PlayStyled(kCollideSound, "");
            styleMeter_ = std::clamp(styleMeter_ + feel.collideStyleGain, 0.0f, 1.0f);
            flyer->SetKnockVelX(flyer->GetKnockVelX() * feel.collideFlyerSpeedKeep);
        };
        // ボスは吹き飛ばさず、ダメージとのけぞりだけ与える
        if (enemy_ != nullptr && !enemy_->IsDefeated() && enemy_->IsVisible()
            && std::find(alreadyHit.begin(), alreadyHit.end(), enemy_) == alreadyHit.end()
            && Collision::CheckCollision(flyerBox, GetEnemyHitBox())) {
            alreadyHit.push_back(enemy_);
            enemy_->TakeDamage(feel.collideDamage);
            enemy_->ApplyHitstun(kBossCollideHitstunSeconds, true);
            onContact(*enemy_);
        }
        for (auto& targetEntry : weaponEnemies_) {
            EnemyEntity* target = targetEntry.enemy;
            if (target == nullptr || target == flyer || target->IsDefeated() || !target->IsVisible()) {
                continue;
            }
            if (std::find(alreadyHit.begin(), alreadyHit.end(), target) != alreadyHit.end()
                || !Collision::CheckCollision(flyerBox, EnemyHitBoxAt(target->GetPosition()))) {
                continue;
            }
            alreadyHit.push_back(target);
            target->TakeDamage(feel.collideDamage);
            if (target->IsDefeated()) {
                // 倒れた敵は回りながら同じ向きへ打ち飛ばす（生きたまま部屋の外へ出て、残りの敵に数えられないように）
                target->ApplyHomeRun(dirX);
            } else {
                // 連鎖できる深さなら大きく吹き飛ばし、当てられた敵もさらに別の敵へ当たれるようにする
                const bool chains = chain < feel.collideMaxChain;
                target->ApplyComboReaction(dirX, feel.collideKnockY, false, playerX, chains);
                target->ApplyHitstun(kCollideHitstunSeconds, true);
            }
            flyerChain_[target] = chain + 1;
            onContact(*target);
        }
    }
}

/** @brief 壁か床で跳ね返った敵に、跳ね返った所で演出を出す（跳ね返り自体は敵の側で起きる） */
void GamePlayScene::UpdateEnemyBounceEffects() {
    const ActionFeelTuning& feel = ActionFeel();
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    const auto showBounce = [&](EnemyEntity* enemy) {
        Vector3 position {};
        bool onWall = false;
        if (enemy == nullptr || !enemy->ConsumeBounce(position, onWall)) {
            return;
        }
        fx.Impact(onWall ? kWallBounceEffect : kFloorBounceEffect);
        fx.Ring(onWall ? kWallBounceEffect : kFloorBounceEffect, position);
        if (onWall) {
            fx.Sparks(kWallBounceEffect, position);
        }
        gameSounds::PlayStyled(onWall ? kWallBounceSound : kFloorBounceSound, "");
        styleMeter_ = std::clamp(styleMeter_ + feel.bounceStyleGain, 0.0f, 1.0f);
    };
    showBounce(enemy_);
    for (auto& entry : weaponEnemies_) {
        showBounce(entry.enemy);
    }
}

/** @brief 扉の条件が見ている敵グループの最後の1体を倒した瞬間に、スローと寄りで締める（ボスの部屋は除く） */
void GamePlayScene::UpdateRoomClearFinale() {
    const ActionFeelTuning& feel = ActionFeel();
    // 演出を出さない間も様子は覚え続ける（演出が明けた後に、前に成立した条件で遅れて出さないように）
    const bool canStart = feel.roomClearEnabled && !clearTriggered_ && !bossExecution_.IsActive() && !finalBoss_.IsActive();
    const std::vector<EnemyGroupWatch> watches = GetStageEditor().CollectEnemyGroupWatches();
    for (const EnemyGroupWatch& watch : watches) {
        RoomClearWatch& state = roomClearWatches_[watch.conditionName];
        // 倒れた瞬間の敵の位置を、最後に倒した敵として覚える
        for (const WatchedEnemyState& enemy : watch.enemies) {
            const auto found = state.defeated.find(enemy.name);
            const bool wasDefeated = found != state.defeated.end() && found->second;
            if (state.initialized && enemy.defeated && !wasDefeated) {
                state.lastDefeatedName = enemy.name;
                state.lastDefeatedPosition = enemy.position;
                state.hasLastDefeated = true;
            }
            state.defeated[enemy.name] = enemy.defeated;
        }
        const bool justMet = state.initialized && watch.met && !state.met;
        state.met = watch.met;
        state.initialized = true;
        if (!justMet || !canStart || !state.hasLastDefeated) {
            continue;
        }
        // ボスがいる部屋は、ボスのとどめの演出に任せる
        const std::vector<CombatEnemyRef> combatEnemies = GetStageEditor().GetCombatEnemies();
        const bool bossRoom = std::any_of(combatEnemies.begin(), combatEnemies.end(), [&](const CombatEnemyRef& ref) {
            return ref.isStageBoss && std::any_of(watch.enemies.begin(), watch.enemies.end(), [&](const WatchedEnemyState& enemy) {
                return enemy.name == ref.name;
            });
        });
        if (!bossRoom) {
            StartRoomClearFinale(state.lastDefeatedPosition);
        }
    }
}

/** @brief 部屋の最後の1体を倒した所で、スロー・寄り・閃光・音で締める */
void GamePlayScene::StartRoomClearFinale(const Vector3& position) {
    const ActionFeelTuning& feel = ActionFeel();
    // 締めの寄りとブラーを借り、そのスローより部屋の締めのスローを優先する（後から頼んだ遅さが使われる）
    comboFeel_.TriggerFinisherImpact(position);
    TimeManager::GetInstance()->RequestSlowMotion(feel.roomClearSlowSeconds, feel.roomClearSlowScale);
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Impact(kRoomClearEffect);
    fx.Ring(kRoomClearEffect, position);
    fx.Flash(kRoomClearEffect);
    gameSounds::PlayStyled(kRoomClearSound, "");
}

/** @brief ジャスト回避の後に、プレイヤーの周りの敵だけを遅くする（ボスの台本の技と味方は遅くならない） */
void GamePlayScene::SlowEnemiesAfterJustDodge() {
    const ActionFeelTuning& feel = ActionFeel();
    if (!feel.dodgeSlowEnabled) {
        return;
    }
    const Vector3 center = player_->GetPosition();
    const float radiusSquared = feel.dodgeSlowRadius * feel.dodgeSlowRadius;
    const auto slow = [&](EnemyEntity* enemy) {
        if (enemy == nullptr || enemy->IsDefeated()) {
            return;
        }
        const Vector3 position = enemy->GetPosition();
        const float dx = position.x - center.x;
        const float dy = position.y - center.y;
        if (dx * dx + dy * dy <= radiusSquared) {
            enemy->ApplyTimeSlow(feel.dodgeSlowSeconds, feel.dodgeSlowScale);
        }
    };
    slow(enemy_);
    for (auto& entry : weaponEnemies_) {
        slow(entry.enemy);
    }
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Ring(kDodgeSlowEffect, center);
    fx.Flash(kDodgeSlowEffect);
    gameSounds::PlayStyled(kDodgeSlowSound, "");
}

} // namespace engine::game

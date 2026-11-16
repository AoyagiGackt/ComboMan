/**
 * @file GamePlaySceneMonsters.cpp
 * @brief 道中のモンスターの個性の動き（ドラゴンは火を吐く・スケルトンは骨を投げる・スライムは跳ねる・コウモリは急降下）
 */
#include "GamePlayScene.h"
#include "GameConstants.h"
#include "HitEffectLibrary.h"
#include "MonsterTraitTuning.h"
#include <algorithm>
#include <cmath>

namespace engine::game {
namespace {
constexpr float kFirstActionDelay = 1.0f; // 近づいてから最初の動きまでの最短の秒数（入った瞬間に一斉に動かないように）
constexpr float kWarningPulseSeconds = 0.15f; // 身構えている間に輪を出す間隔
constexpr float kSwoopTargetHeight = 0.4f; // 急降下でプレイヤーの足元からこの高さを狙う
constexpr float kHalf = 0.5f; // 扇の真ん中を求める

/** @brief 横の向き（+1/-1）を返す（0 は +1 にする） */
float SignOf(float value) {
    return value >= 0.0f ? 1.0f : -1.0f;
}

/** @brief 2点の間を割合 t でつなぐ */
Vector3 LerpPoint(const Vector3& from, const Vector3& to, float t) {
    return { from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t, from.z + (to.z - from.z) * t };
}
} // namespace

/** @brief 道中のモンスターの個性の動きを1フレーム進める（毎フレーム呼ぶ） */
void GamePlayScene::UpdateMonsterTraits(float dt) {
    for (auto& entry : weaponEnemies_) {
        EnemyEntity* enemy = entry.enemy;
        // 倒れたら途中の動きをやめる（急降下の途中なら台本モードを外す）
        if (enemy == nullptr || enemy->IsDefeated()) {
            if (enemy != nullptr && entry.trait.phase != MonsterTraitState::Phase::kIdle) {
                enemy->SetScripted(false);
            }
            entry.trait = {};
            continue;
        }
        const MonsterTraitData& data = MonsterTraitTuning::Get(enemy->GetMonsterKind());
        // 跳ねた後や打ち上げられた後に着地した瞬間の粒と輪
        if (enemy->JustLanded()) {
            const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
            fx.SparksIfNamed(data.landSparks, enemy->GetPosition());
            fx.RingIfNamed(data.landRing, enemy->GetPosition());
        }
        if (data.trait == MonsterTrait::kNone) {
            continue;
        }
        UpdateMonsterTrait(entry, data, dt);
    }
}

/** @brief モンスター1体の個性の動きを進める（待つ → 身構える → 動く） */
void GamePlayScene::UpdateMonsterTrait(WeaponEnemyEntry& entry, const MonsterTraitData& data, float dt) {
    EnemyEntity& enemy = *entry.enemy;
    MonsterTraitState& state = entry.trait;
    const Vector3& ppos = player_->GetPosition();
    const Vector3 epos = enemy.GetPosition();
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    // 吹き飛んでいる・止まっている間は動きを始めない（身構えている途中なら潰れる）
    const bool reeling = enemy.IsInHitstun() || enemy.IsLaunched() || !enemy.IsVisible();
    switch (state.phase) {
    case MonsterTraitState::Phase::kIdle: {
        const bool inRange = DistanceSquaredXY(ppos, epos) <= data.range * data.range;
        if (!inRange) {
            state.timer = (std::max)(state.timer, kFirstActionDelay);
            return;
        }
        state.timer -= dt;
        if (state.timer > 0.0f || reeling || enemy.IsScripted()) {
            return;
        }
        // 跳ねるは身構えずにすぐ跳ぶ
        if (data.trait == MonsterTrait::kHop) {
            enemy.ApplyComboReaction(SignOf(ppos.x - epos.x) * data.hopX, data.hopY, false, ppos.x, true);
            fx.SparksIfNamed(data.actSparks, epos);
            state.timer = data.interval;
            return;
        }
        state.phase = MonsterTraitState::Phase::kTelegraph;
        state.timer = data.telegraphSeconds;
        state.pulse = 0.0f;
        enemy.FaceTowardX(ppos.x);
        return;
    }
    case MonsterTraitState::Phase::kTelegraph:
        // 攻撃を当てて止めれば、身構えを潰せる
        if (reeling) {
            state.phase = MonsterTraitState::Phase::kIdle;
            state.timer = data.interval;
            return;
        }
        state.pulse -= dt;
        if (state.pulse <= 0.0f) {
            state.pulse = kWarningPulseSeconds;
            fx.Ring(data.warningRing, epos);
        }
        state.timer -= dt;
        if (state.timer <= 0.0f) {
            PerformMonsterTrait(entry, data);
        }
        return;
    case MonsterTraitState::Phase::kSwoopDown:
    case MonsterTraitState::Phase::kSwoopUp:
        UpdateMonsterSwoop(entry, data, dt);
        return;
    }
}

/** @brief 身構え終わったモンスターの動きを出す（火を吐く・骨を投げる・急降下を始める） */
void GamePlayScene::PerformMonsterTrait(WeaponEnemyEntry& entry, const MonsterTraitData& data) {
    EnemyEntity& enemy = *entry.enemy;
    MonsterTraitState& state = entry.trait;
    const Vector3& ppos = player_->GetPosition();
    const Vector3 epos = enemy.GetPosition();
    const Vector3 mouth = { epos.x, epos.y + data.launchHeight, epos.z };
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Sparks(data.actSparks, mouth);
    state.phase = MonsterTraitState::Phase::kIdle;
    state.timer = data.interval;
    // 火を吐く（まっすぐ扇状）と骨を投げる（山なり）は、共通の弾を出す処理で出す
    if (data.trait == MonsterTrait::kFireBreath || data.trait == MonsterTrait::kBoneThrow) {
        EnemyVolleyShot shot;
        shot.count = data.count;
        shot.spreadDegrees = data.spreadDegrees;
        shot.speed = data.speed;
        shot.riseSpeed = data.riseSpeed;
        shot.gravity = data.trait == MonsterTrait::kBoneThrow ? data.gravity : 0.0f;
        shot.lifetime = data.lifetime;
        shot.damage = data.damage;
        SpawnEnemyVolley(mouth, ppos, shot);
        return;
    }
    if (data.trait == MonsterTrait::kSwoop) {
        // 今の所からプレイヤーの所まで降り、元の所へ戻る（その間は台本モードで動かす）
        state.from = epos;
        state.to = { ppos.x, ppos.y + kSwoopTargetHeight, epos.z };
        state.elapsed = 0.0f;
        state.struck = false;
        state.phase = MonsterTraitState::Phase::kSwoopDown;
        enemy.SetScripted(true);
        enemy.SetScriptedFacing(state.to.x - state.from.x);
    }
}

/** @brief origin から target へ向けて弾をまとめて出す（gravity が 0 なら扇状に狙い、そうでなければ山なりに投げる） */
void GamePlayScene::SpawnEnemyVolley(const Vector3& origin, const Vector3& target, const EnemyVolleyShot& shot) {
    const int count = (std::max)(shot.count, 1);
    const float middle = static_cast<float>(count - 1) * kHalf;
    const float aim = std::atan2(target.y - origin.y, target.x - origin.x);
    const float dirX = SignOf(target.x - origin.x);
    for (int i = 0; i < count; ++i) {
        const float offset = (static_cast<float>(i) - middle) * shot.spreadDegrees * gameConstants::kDegToRad;
        EnemyBullet bullet;
        bullet.pos = origin;
        if (shot.gravity > 0.0f) {
            // 山なり: 横の速さは同じで、上向きの速さを数ごとに少しずつ変える
            bullet.vel = { dirX * shot.speed, shot.riseSpeed + offset * shot.speed, 0.0f };
        } else {
            // まっすぐ: 狙う向きから扇状に広げる
            bullet.vel = { std::cos(aim + offset) * shot.speed, std::sin(aim + offset) * shot.speed, 0.0f };
        }
        bullet.timer = shot.lifetime;
        bullet.damage = shot.damage;
        bullet.gravity = shot.gravity;
        enemyBullets_.push_back(bullet);
    }
}

/** @brief 急降下と戻りを進め、降りる途中で体が当たったらダメージ（1回だけ） */
void GamePlayScene::UpdateMonsterSwoop(WeaponEnemyEntry& entry, const MonsterTraitData& data, float dt) {
    EnemyEntity& enemy = *entry.enemy;
    MonsterTraitState& state = entry.trait;
    // 殴られたら急降下をやめ、その場から殴られた動きに任せる（台本のまま元の場所へ戻ると、殴っている最中に飛んでいく）
    if (enemy.IsInHitstun()) {
        enemy.SetScripted(false);
        state.phase = MonsterTraitState::Phase::kIdle;
        state.timer = data.interval;
        return;
    }
    state.elapsed += dt;
    const bool down = state.phase == MonsterTraitState::Phase::kSwoopDown;
    const float seconds = down ? data.swoopSeconds : data.returnSeconds;
    const float t = seconds > 0.0f ? std::clamp(state.elapsed / seconds, 0.0f, 1.0f) : 1.0f;
    // 降りる時は加速し、戻る時は減速する
    const Vector3 position = down ? LerpPoint(state.from, state.to, t * t) : LerpPoint(state.to, state.from, 1.0f - (1.0f - t) * (1.0f - t));
    enemy.SetPosition(position);
    if (down && !state.struck && StrikePlayerFromBoss(EnemyHitBoxAt(position), position, data.damage)) {
        state.struck = true;
    }
    if (t < 1.0f) {
        return;
    }
    if (down) {
        state.phase = MonsterTraitState::Phase::kSwoopUp;
        state.elapsed = 0.0f;
        return;
    }
    // 戻り終えたらふだんの動きへ戻す
    enemy.SetScripted(false);
    state.phase = MonsterTraitState::Phase::kIdle;
    state.timer = data.interval;
}

} // namespace engine::game

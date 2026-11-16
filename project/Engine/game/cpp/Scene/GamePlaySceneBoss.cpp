/**
 * @file GamePlaySceneBoss.cpp
 * @brief ステージボス戦（ひるみ値・第2段階・突進やダッシュ・画面上の体力バー）をシーンへつなぐ
 */
#include "GamePlayScene.h"
#include "EnemyTuning.h"
#include "HitEffectLibrary.h"
#include "StageEditor.h"
#include "StringUtility.h"
#include <algorithm>

namespace engine::game {
namespace {
constexpr float kStageHalfDepth = 0.5f; // 2.5Dステージの奥行き半分
constexpr float kBossWallBandBottom = 0.2f; // ボスの足元からこの高さより上に掛かる箱を壁とみなす（低い段差は床として越える）
constexpr float kBossWallBandTop = 1.0f; // 壁とみなす高さの上端（ボスの足元から）
constexpr float kBossBodyHalfWidth = 0.4f; // 壁に当たったかを調べる時のボスの体の半分の幅
constexpr float kBossGroundProbeDepth = 0.6f; // ボスの位置からこの深さまでに床の上面が無ければ、足場の外とみなす
constexpr float kBossSightThickness = 0.1f; // ボスからプレイヤーが見えるかを調べる横に細い箱の厚さ
constexpr float kBossWarningPulseSeconds = 0.12f; // 身構えている間に輪を出す間隔
constexpr float kBossTrailSeconds = 0.04f; // 走っている間に火花を出す間隔
constexpr float kWaveSparkSeconds = 0.05f; // 衝撃波が走る所に土煙を出す間隔
constexpr float kLeapHitHalfHeight = 1.5f; // 跳びかかりの着地の周りで、上下に届く半分の高さ
} // namespace

/** @brief その位置へボスを動かすと、壁にめり込むか足場の外へ出るか（ボスは高さが変わらないので、床の無い所へは行かせない） */
bool GamePlayScene::IsBossBlockedAt(const Vector3& position) const {
    // 足元の床は数えず、体の高さに掛かる箱だけを壁とみなす
    const AABB body = { { position.x - kBossBodyHalfWidth, position.y + kBossWallBandBottom, -kStageHalfDepth },
        { position.x + kBossBodyHalfWidth, position.y + kBossWallBandTop, kStageHalfDepth } };
    const Vector3 current = enemy_ != nullptr ? enemy_->GetPosition() : position;
    bool hasGround = false;
    for (const AABB& solid : GetStageEditor().GetSolidColliders()) {
        // ボス自身の体の箱は除く
        const bool ownBody = current.x >= solid.min.x && current.x <= solid.max.x
            && current.y >= solid.min.y && current.y <= solid.max.y;
        if (ownBody) {
            continue;
        }
        if (Collision::CheckCollision(body, solid)) {
            return true;
        }
        // 真下の少し下までに上面がある箱を、立っていられる床とみなす
        const bool underFeet = position.x >= solid.min.x && position.x <= solid.max.x;
        const bool topNearFeet = solid.max.y <= position.y + kBossWallBandBottom && solid.max.y >= position.y - kBossGroundProbeDepth;
        hasGround = hasGround || (underFeet && topNearFeet);
    }
    return !hasGround;
}

/** @brief ボスとプレイヤーの間に、壁や閉じた扉が無いか（ボスの体の高さで横に細い箱を通して調べる。低い段差は越えられるので数えない） */
bool GamePlayScene::BossCanSeePlayer() const {
    if (enemy_ == nullptr) {
        return false;
    }
    const Vector3 boss = enemy_->GetPosition();
    const float playerX = player_->GetPosition().x;
    const float sightY = boss.y + kBossWallBandTop;
    const AABB sight = { { (std::min)(boss.x, playerX), sightY, -kStageHalfDepth },
        { (std::max)(boss.x, playerX), sightY + kBossSightThickness, kStageHalfDepth } };
    // 敵の体は除いた地形だけで調べる（ボス自身やほかの敵で見えなくならないように）
    for (const AABB& solid : GetStageEditor().GetTerrainColliders()) {
        if (Collision::CheckCollision(sight, solid)) {
            return false;
        }
    }
    return true;
}

/** @brief ボス戦の進行役を1フレーム進め、起きたことに合わせて演出と当たり判定を出す */
void GamePlayScene::UpdateStageBoss(float dt) {
    if (!enemy_ || finalBoss_.IsActive()) {
        return;
    }
    const BossBattleTuning& tuning = BossBattleTuningStore::Get();
    BossFrameInput input;
    input.playerPosition = player_->GetPosition();
    input.isBlocked = [this](const Vector3& position) {
        return IsBossBlockedAt(position);
    };
    input.canSeePlayer = BossCanSeePlayer();
    BossFrameOutput output;
    bossDirector_.Update(dt, input, output);

    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    const Vector3 bossPosition = enemy_->GetPosition();
    // ひるんだ: 揺れとボスから広がる輪で、今が攻め時だと見せる
    if (output.staggerStarted) {
        fx.Impact(tuning.staggerImpact);
        fx.Ring(tuning.staggerRing, bossPosition);
    }
    // 第2段階: 咆哮して体の色を変える
    if (output.rageStarted) {
        fx.Impact(tuning.rageImpact);
        fx.Flash(tuning.rageFlash);
        fx.Ring(tuning.rageRing, bossPosition);
        enemy_->SetColor(tuning.rageColor);
    }
    if (output.slamRequested) {
        bossSlamRequested_ = true;
    }
    // 身構えている間は一定の間隔で輪を出して、次に何が来るかを見せる
    bossWarningPulse_ -= dt;
    const bool telegraphing = output.chargeTelegraph || output.dashTelegraph || !output.warningRing.empty();
    if (telegraphing && bossWarningPulse_ <= 0.0f) {
        bossWarningPulse_ = kBossWarningPulseSeconds;
        const std::string& ring = !output.warningRing.empty() ? output.warningRing
            : (output.chargeTelegraph ? tuning.chargeWarningRing : tuning.dashWarningRing);
        fx.Ring(ring, bossPosition);
    }
    // 投げる: ボスの手元からプレイヤーへ向けて、その技の中身で弾を出す
    if (output.volley != nullptr) {
        const BossVolleyData& volley = *output.volley;
        EnemyVolleyShot shot;
        shot.count = volley.count;
        shot.spreadDegrees = volley.spreadDegrees;
        shot.speed = volley.speed;
        shot.riseSpeed = volley.riseSpeed;
        shot.gravity = volley.gravity;
        shot.lifetime = volley.lifetime;
        shot.damage = volley.damage;
        SpawnEnemyVolley({ bossPosition.x, bossPosition.y + volley.launchHeight, bossPosition.z }, player_->GetPosition(), shot);
        fx.Sparks(tuning.chargeTrailSparks, bossPosition);
    }
    // 地面を叩く: 左右へ1つずつ衝撃波を走らせる
    if (output.waveStarted) {
        fx.Impact("boss_slam");
        fx.Ring("boss_slam_inner", bossPosition);
        for (const float dirX : { -1.0f, 1.0f }) {
            BossWave wave;
            wave.x = bossPosition.x;
            wave.y = bossPosition.y;
            wave.dirX = dirX;
            bossWaves_.push_back(wave);
        }
    }
    // 跳びかかりの着地
    if (output.leapLanded) {
        LandBossLeap(bossPosition);
    }
    UpdateBossWaves(dt);
    // 走っている間は足元に火花を残し、体が当たったらダメージ
    if (output.rushing) {
        bossTrailTimer_ -= dt;
        if (bossTrailTimer_ <= 0.0f) {
            bossTrailTimer_ = kBossTrailSeconds;
            fx.Sparks(output.charging ? tuning.chargeTrailSparks : tuning.dashTrailSparks, bossPosition);
        }
        if (StrikePlayerFromBoss(EnemyHitBoxAt(bossPosition), bossPosition, output.strikeDamage)) {
            bossDirector_.OnStrikeLanded();
        }
    }
    UpdateStageBossHud(dt);
}

/** @brief 当てた攻撃でボスが吹き飛んだりのけぞったりするか（ステージボスはひるんでいる間だけ。ラスボスは今までどおり） */
bool GamePlayScene::BossReactsToHits() const {
    return finalBoss_.IsActive() || bossDirector_.AllowsReaction();
}

/** @brief 画面上の体力バーを更新する */
void GamePlayScene::UpdateStageBossHud(float dt) {
    FinalBossHud::State state;
    const bool engaged = enemy_ != nullptr && bossDirector_.IsEngaged() && !enemy_->IsDefeated() && !finalBoss_.IsActive();
    state.visibleBar = engaged;
    state.hpRatio = (enemy_ != nullptr && enemy_->GetMaxHp() > 0)
        ? static_cast<float>(enemy_->GetHp()) / static_cast<float>(enemy_->GetMaxHp())
        : 0.0f;
    state.rage = bossDirector_.IsRaging();
    // 名前は体力バーの上にだけ出す（登場の演出は出さないので、黒帯や画面中央の名前は使わない）
    state.bossName = StringUtility::ConvertString(bossDirector_.Profile().name);
    stageBossHud_.Update(state, dt);
}

/** @brief 衝撃波を進め、プレイヤーに当たったらダメージ（毎フレーム呼ぶ） */
void GamePlayScene::UpdateBossWaves(float dt) {
    if (bossWaves_.empty()) {
        return;
    }
    const BossBattleTuning& tuning = BossBattleTuningStore::Get();
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    for (BossWave& wave : bossWaves_) {
        const float step = tuning.waveSpeed * dt;
        wave.x += wave.dirX * step;
        wave.traveled += step;
        // 走る所に土煙を残す
        wave.sparkTimer -= dt;
        const Vector3 position = { wave.x, wave.y, 0.0f };
        if (wave.sparkTimer <= 0.0f) {
            wave.sparkTimer = kWaveSparkSeconds;
            fx.Sparks(tuning.waveSparks, position);
        }
        // 足元から決めた高さまでに当たる（ジャンプで跳び越えればかわせる）
        const AABB box = { { wave.x - tuning.waveHalfWidth, wave.y, -kStageHalfDepth },
            { wave.x + tuning.waveHalfWidth, wave.y + tuning.waveHeight, kStageHalfDepth } };
        if (!wave.struck && StrikePlayerFromBoss(box, position, tuning.waveDamage)) {
            wave.struck = true;
        }
    }
    // 走り切ったか、壁や足場の端に来た衝撃波は消す
    bossWaves_.erase(std::remove_if(bossWaves_.begin(), bossWaves_.end(),
                         [&](const BossWave& wave) {
                             return wave.traveled >= tuning.waveDistance || IsBossBlockedAt({ wave.x, wave.y, 0.0f });
                         }),
        bossWaves_.end());
}

/** @brief 跳びかかりの着地の周りにダメージと演出を出す */
void GamePlayScene::LandBossLeap(const Vector3& position) {
    const BossBattleTuning& tuning = BossBattleTuningStore::Get();
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Impact("boss_slam");
    fx.Ring("boss_slam_outer", position, { .speed = tuning.leapRadius });
    fx.Sparks("boss_slam", position);
    const AABB area = { { position.x - tuning.leapRadius, position.y - kLeapHitHalfHeight, -kStageHalfDepth },
        { position.x + tuning.leapRadius, position.y + kLeapHitHalfHeight, kStageHalfDepth } };
    StrikePlayerFromBoss(area, position, tuning.leapDamage);
}

/** @brief ボスの叩きつけの範囲（第2段階で広がる） */
float GamePlayScene::BossSlamRadius() const {
    return EnemyTuning::GetInstance()->BossSlam().radius * bossDirector_.SlamRadiusMult();
}

} // namespace engine::game

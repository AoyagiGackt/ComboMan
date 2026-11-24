/**
 * @file GamePlaySceneBoss.cpp
 * @brief ステージボス戦（ひるみ値・第2段階・突進やダッシュ・画面上の体力バー）をシーンへつなぐ
 */
#include "GamePlayScene.h"
#include "EnemyTuning.h"
#include "HitEffectLibrary.h"
#include "ParticleFieldLibrary.h"
#include "StageEditor.h"
#include "StringUtility.h"
#include <algorithm>
#include <cmath>
#include <random>

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
constexpr float kVortexDebrisSeconds = 0.03f; // 渦が引き寄せている間に、まわりから土煙を出す間隔
constexpr float kVortexDebrisRingRatio = 0.85f; // 土煙を出す円の半径（引き寄せる半径に対する割合）
constexpr float kVortexDebrisSpreadY = 1.2f; // 土煙を出す高さのばらつき（渦の中心から上下）
constexpr float kVortexWarningSeconds = 0.18f; // 引き寄せている間に、中心から輪を出す間隔
constexpr float kVortexMinPullDistance = 0.3f; // これより中心に近いプレイヤーは引き寄せない（中心で震えないように）
constexpr float kVortexPullReachY = 2.5f; // 渦の中心から上下にこの高さまでのプレイヤーを引き寄せる
constexpr float kVortexBurstHalfHeight = 1.5f; // 弾けた時に上下に届く半分の高さ
constexpr float kTwoPi = 6.28318530718f;
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
    return !IsWallBetweenEnemyAndPlayer(enemy_->GetPosition());
}

/** @brief 敵とプレイヤーの間に、壁や閉じた扉があるか */
bool GamePlayScene::IsWallBetweenEnemyAndPlayer(const Vector3& enemyPosition) const {
    const float playerX = player_->GetPosition().x;
    const float sightY = enemyPosition.y + kBossWallBandTop;
    const AABB sight = { { (std::min)(enemyPosition.x, playerX), sightY, -kStageHalfDepth },
        { (std::max)(enemyPosition.x, playerX), sightY + kBossSightThickness, kStageHalfDepth } };
    // 敵の体は除いた地形だけで調べる（相手自身やほかの敵で見えなくならないように）
    const std::vector<AABB> solids = GetStageEditor().GetTerrainColliders();
    return std::any_of(solids.begin(), solids.end(), [&](const AABB& solid) {
        return Collision::CheckCollision(sight, solid);
    });
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
    KeepStageBossOnGround();
    // 今の技と区切りに合わせた動きをボスの体へ渡す（倒れたら外し、膝をつく姿勢に任せる）
    if (enemy_->IsDefeated()) {
        enemy_->ClearMotion();
    } else {
        enemy_->SetMotionFrame(bossMotion_.Update(bossDirector_.BossKey(), output.motion, dt));
    }

    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    const Vector3 bossPosition = enemy_->GetPosition();
    // ひるんだ: 揺れとボスから広がる輪で、今が攻め時だと見せる
    if (output.staggerStarted) {
        fx.Impact(tuning.staggerImpact);
        fx.Ring(tuning.staggerRing, bossPosition);
        ParticleFieldLibrary::Emit(pm_, "boss_stagger", bossPosition);
    }
    // 第2段階: 咆哮して体の色を変える
    if (output.rageStarted) {
        fx.Impact(tuning.rageImpact);
        fx.Flash(tuning.rageFlash);
        fx.Ring(tuning.rageRing, bossPosition);
        enemy_->SetColor(tuning.rageColor);
        // 咆哮の風圧で、まわりの粒を渦を巻きながら吹き飛ばす
        ParticleFieldLibrary::Emit(pm_, "boss_roar", bossPosition);
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
        ParticleFieldLibrary::Emit(pm_, "boss_slam", bossPosition);
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
    // 渦: 引き寄せている間は粒とプレイヤーを中心へ寄せ、時間が来たら弾けさせる
    if (output.vortexPulling) {
        UpdateBossVortexPull(dt, bossPosition, output.vortexProgress);
    }
    if (output.vortexBurst) {
        BurstBossVortex(bossPosition);
    }
    UpdateBossWaves(dt);
    // 走っている間は足元に火花を残し、体が当たったらダメージ
    if (output.rushing) {
        // 走る後ろに、足元の火花や土煙を引きずる場を置く
        ParticleFieldLibrary::Emit(pm_, "boss_charge", bossPosition, output.rushDirX);
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

/**
 * @brief ボスがふだんの動き（追いかける・ひるんで吹き飛ぶ）で壁にめり込むか足場の外へ出たら、最後に立てていた所へ戻す
 * @note ボスは地形に合わせて高さを変えないので、追いかけて段差や足場の端を越えると宙に浮く。技の動きは進行役が自分で止める
 */
void GamePlayScene::KeepStageBossOnGround() {
    if (enemy_ == nullptr || enemy_->IsDefeated() || enemy_->IsScripted() || enemy_->IsLaunched()) {
        bossSafePosition_.reset();
        return;
    }
    const Vector3 position = enemy_->GetPosition();
    if (!IsBossBlockedAt(position)) {
        bossSafePosition_ = position;
        return;
    }
    if (bossSafePosition_) {
        GetStageEditor().PlaceEnemyAt(enemy_, *bossSafePosition_);
    }
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
        // 走る先端で土煙を押し上げる（このフレームだけの場を毎フレーム置き直して、衝撃波についていかせる）
        ParticleFieldLibrary::Emit(pm_, "boss_wave", position, wave.dirX);
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
    ParticleFieldLibrary::Emit(pm_, "boss_leap", position);
    const AABB area = { { position.x - tuning.leapRadius, position.y - kLeapHitHalfHeight, -kStageHalfDepth },
        { position.x + tuning.leapRadius, position.y + kLeapHitHalfHeight, kStageHalfDepth } };
    StrikePlayerFromBoss(area, position, tuning.leapDamage);
}

/** @brief 渦で引き寄せている間の1フレーム（まわりから土煙を出して吸い込ませ、プレイヤーも中心へ寄せる） */
void GamePlayScene::UpdateBossVortexPull(float dt, const Vector3& bossPosition, float progress) {
    const BossBattleTuning& tuning = BossBattleTuningStore::Get();
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    const Vector3 center = { bossPosition.x, bossPosition.y + tuning.vortexHeight, bossPosition.z };
    // 粒を吸い込む渦の場（GPU パーティクルの更新が、範囲の中の粒を回しながら中心へ寄せる）
    ParticleFieldLibrary::Emit(pm_, "boss_vortex", bossPosition);
    // まわりの円の上から土煙を出す（出た粒が場に乗って渦を巻きながら吸い込まれる）
    bossVortexDebrisTimer_ -= dt;
    if (bossVortexDebrisTimer_ <= 0.0f) {
        bossVortexDebrisTimer_ = kVortexDebrisSeconds;
        std::uniform_real_distribution<float> angleDist(0.0f, kTwoPi);
        std::uniform_real_distribution<float> heightDist(-kVortexDebrisSpreadY, kVortexDebrisSpreadY);
        const float angle = angleDist(rng_);
        const float ring = tuning.vortexPullRadius * kVortexDebrisRingRatio;
        fx.Sparks(tuning.vortexDebris, { center.x + std::cos(angle) * ring, center.y + heightDist(rng_), center.z });
    }
    // 中心から輪を出して、弾けるまでの間を見せる
    bossVortexWarningTimer_ -= dt;
    if (bossVortexWarningTimer_ <= 0.0f) {
        bossVortexWarningTimer_ = kVortexWarningSeconds;
        fx.Ring(tuning.vortexWarningRing, center, { .speed = tuning.vortexBurstRadius });
    }
    // プレイヤーを中心へ寄せる（中心に近いほど強く、弾ける直前ほど強い。歩けば逆らえる程度）
    const Vector3& playerPosition = player_->GetPosition();
    const float dx = center.x - playerPosition.x;
    const float distance = std::abs(dx);
    const bool inReach = distance < tuning.vortexPullRadius && distance > kVortexMinPullDistance
        && std::abs(playerPosition.y - center.y) < kVortexPullReachY;
    if (!inReach || player_->IsInvincible()) {
        return;
    }
    const float closeness = 1.0f - distance / tuning.vortexPullRadius;
    const float step = tuning.vortexPullSpeed * (closeness + progress) * dt;
    player_->AddExternalShiftX((dx > 0.0f ? 1.0f : -1.0f) * (std::min)(step, distance - kVortexMinPullDistance));
}

/** @brief 渦が弾けた瞬間（中心のまわりにダメージを出し、吸い込んだ粒を一気に吹き飛ばす） */
void GamePlayScene::BurstBossVortex(const Vector3& bossPosition) {
    const BossBattleTuning& tuning = BossBattleTuningStore::Get();
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    const Vector3 center = { bossPosition.x, bossPosition.y + tuning.vortexHeight, bossPosition.z };
    fx.Impact("boss_slam");
    fx.Ring("boss_slam_inner", center, { .speed = tuning.vortexBurstRadius });
    fx.Ring("boss_slam_outer", center, { .speed = tuning.vortexBurstRadius });
    fx.Sparks("boss_slam", center);
    ParticleFieldLibrary::Emit(pm_, "boss_vortex_burst", bossPosition);
    const AABB area = { { center.x - tuning.vortexBurstRadius, center.y - kVortexBurstHalfHeight, -kStageHalfDepth },
        { center.x + tuning.vortexBurstRadius, center.y + kVortexBurstHalfHeight, kStageHalfDepth } };
    StrikePlayerFromBoss(area, center, tuning.vortexDamage);
}

/** @brief ボスの叩きつけの範囲（第2段階で広がる） */
float GamePlayScene::BossSlamRadius() const {
    return EnemyTuning::GetInstance()->BossSlam().radius * bossDirector_.SlamRadiusMult();
}

} // namespace engine::game

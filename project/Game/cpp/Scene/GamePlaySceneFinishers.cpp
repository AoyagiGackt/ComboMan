/**
 * @file GamePlaySceneFinishers.cpp
 * @brief 地上コンボの最後の段が当たった時に出す、武器ごとの締めの動作（突き飛ばし・衝撃波・叩きつけ・引き寄せ・回り込み・投げ）
 */
#include "GamePlayScene.h"
#include "CombatTuning.h"
#include "GameConstants.h"
#include "HitEffectLibrary.h"
#include "MeleeCombo.h"
#include "StageEditor.h"
#include "WeaponFinisherTuning.h"
#include "WeaponManager.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>

namespace engine::game {
namespace {
constexpr float kStageHalfDepth = 0.5f; // 2.5Dステージの奥行き半分
constexpr const char* kThrownWeaponModel = "Resources/MedievalWeaponsPack/OBJ/Axe_Double.obj";
constexpr const char* kThrownWeaponTexture = "Resources/MedievalWeaponsPack/OBJ/Axe_DoublePalette.png";
constexpr float kThrowHeight = 0.6f; // 投げる武器を足元からこの高さで飛ばす
constexpr float kWarpBodyHalfWidth = 0.3f; // 引き寄せた敵が壁の中かを調べる時の体の半分の幅
constexpr float kWarpBodyBottom = 0.2f; // 足元の床を数えないよう、この高さより上だけを調べる
constexpr float kWarpBodyTop = 1.2f; // 体の上端（足元から）
constexpr float kWallColumnTolerance = 0.05f; // 真ん中の横の位置がこれだけ近いブロックは、同じ列に積まれた壁とみなす
constexpr float kFootClearance = 0.05f; // プレイヤーの体の下端をこれだけ上げて調べる（立っている床に触れているだけでは壁と数えない）
constexpr float kGroundProbeDepth = 0.6f; // 足元からこの深さまでに床の上面が無ければ、立てない所とみなす
constexpr float kHalf = 0.5f;
constexpr float kPathSideClearance = 0.01f; // 通り道を調べる時に左右を縮める幅（壁にぴったり付いて立っているだけでは塞がれたと数えない）
constexpr float kThrowHitReactionScale = 0.5f; // 投げた武器が当たった時の吹き飛びは、締めの半分にする
constexpr float kSlamImpactShare = 0.5f; // 振り下ろしの演出は、届く長さのこの割合の所に出す

/** @brief 段がその武器の地上コンボの最後の段か（技IDで比べる。合体中は最後に増えた素材の締め、ボスの武器は専用のコンボで見る） */
bool IsLastGroundStep(WeaponType type, const MeleeAttackDef& attack, bool fused, bool bossWeapon) {
    if (fused) {
        return attack.finisher;
    }
    const MeleeComboSet& set = GetMeleeComboSet(type, bossWeapon);
    return set.ground.count > 0 && std::strcmp(set.ground.data[set.ground.count - 1].id, attack.id) == 0;
}

/** @brief 横の向き（+1/-1）を返す（0 は +1 にする） */
float SignOf(float value) {
    return value >= 0.0f ? 1.0f : -1.0f;
}
} // namespace

/** @brief 締めの動作で狙える生きた敵を集める（ボスと道中の敵） */
std::vector<GamePlayScene::FinisherTarget> GamePlayScene::CollectFinisherTargets() const {
    std::vector<FinisherTarget> targets;
    if (enemy_ != nullptr && !enemy_->IsDefeated() && enemy_->IsVisible()) {
        targets.push_back({ enemy_, true });
    }
    for (const auto& entry : weaponEnemies_) {
        if (entry.enemy != nullptr && !entry.enemy->IsDefeated()) {
            targets.push_back({ entry.enemy, false });
        }
    }
    return targets;
}

/** @brief 締めの動作のダメージを与える（ボスはひるんでいない間は吹き飛ばさないので、反応してよいかも返す） */
bool GamePlayScene::DamageByFinisher(const FinisherTarget& target, float damageMult, float weaponDamage) {
    const int damage = (std::max)(1,
        static_cast<int>(std::round(weaponDamage * damageMult / CombatTune().meleeDamageDivisor * CurrentDamageMult())));
    target.enemy->TakeDamage(damage);
    return !target.isBoss || BossReactsToHits();
}

/** @brief 地上コンボの最後の段が当たった瞬間なら、今の武器の締めの動作を出す（毎フレーム呼ぶ） */
void GamePlayScene::UpdateWeaponFinisher() {
    // 覚醒の追加ヒット・空中・固有技の連撃・打ち上げ技では出さない
    const MeleeAttackDef* attack = player_->GetActiveMeleeAttack();
    if (!player_->JustComboHit() || player_->JustComboExtraHit() || attack == nullptr || attack->launcher
        || !player_->IsOnGround() || player_->IsSkillSequenceActive()) {
        return;
    }
    auto* wm = WeaponManager::GetInstance();
    if (!wm->HasEquippedWeapon()) {
        return;
    }
    if (!IsLastGroundStep(wm->GetCurrent().type, *attack, wm->IsCurrentFused(), wm->IsBossSlotSelected())) {
        return;
    }
    // ボスへの通常の当たり（UpdateStyleAndUI）が吹き飛ばしを上書きしないよう、出すのはその後（ApplyPendingWeaponFinisher）
    weaponFinisherPending_ = true;
}

/** @brief 覚えておいた締めの動作を出す（ボスへの通常の当たりの処理の後に呼ぶ） */
void GamePlayScene::ApplyPendingWeaponFinisher() {
    if (!weaponFinisherPending_) {
        return;
    }
    weaponFinisherPending_ = false;
    auto* wm = WeaponManager::GetInstance();
    if (!wm->HasEquippedWeapon()) {
        return;
    }
    const WeaponFinisherData& data = WeaponFinisherTuning::Get(wm->GetCurrent().type);
    /** @brief 動作の種類と、その動作を出す関数の対応 */
    struct ActionEntry {
        WeaponFinishAction action;
        void (GamePlayScene::*perform)(const WeaponFinisherData&);
    };
    static constexpr ActionEntry kActions[] = {
        { WeaponFinishAction::kWarpBehind, &GamePlayScene::FinishWarpBehind },
        { WeaponFinishAction::kThrust, &GamePlayScene::FinishThrust },
        { WeaponFinishAction::kShockwave, &GamePlayScene::FinishShockwave },
        { WeaponFinishAction::kSlamDown, &GamePlayScene::FinishSlamDown },
        { WeaponFinishAction::kPullSpin, &GamePlayScene::FinishPullSpin },
        { WeaponFinishAction::kThrow, &GamePlayScene::FinishThrow },
    };
    for (const ActionEntry& entry : kActions) {
        if (entry.action == data.action) {
            (this->*entry.perform)(data);
            // 出した瞬間の揺れと、出した所の演出
            const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
            fx.Impact(data.impact);
            return;
        }
    }
}

/** @brief 前方の箱（足元から前へ length、上下に halfHeight）に重なる敵へ、関数を呼ぶ */
void GamePlayScene::ForEachTargetInFront(float length, float halfHeight, const std::function<void(const FinisherTarget&)>& visit) {
    const Vector3& ppos = player_->GetPosition();
    const float dirX = player_->GetLastDirX();
    const AABB area = { { (std::min)(ppos.x, ppos.x + dirX * length), ppos.y - halfHeight, -kStageHalfDepth },
        { (std::max)(ppos.x, ppos.x + dirX * length), ppos.y + halfHeight, kStageHalfDepth } };
    for (const FinisherTarget& target : CollectFinisherTargets()) {
        if (Collision::CheckCollision(area, EnemyHitBoxAt(target.enemy->GetPosition()))) {
            visit(target);
        }
    }
}

/** @brief 槍: 前へ長く突き、当たった敵を横へ突き飛ばす */
void GamePlayScene::FinishThrust(const WeaponFinisherData& data) {
    const Vector3& ppos = player_->GetPosition();
    const float dirX = player_->GetLastDirX();
    ForEachTargetInFront(data.length, data.halfHeight, [&](const FinisherTarget& target) {
        if (DamageByFinisher(target, data.damageMult, CurrentWeaponDamage())) {
            target.enemy->ApplyComboReaction(dirX * data.knockX, data.knockY, false, ppos.x, true);
            target.enemy->ApplyHitstun(data.stunSeconds, true);
        }
    });
    // 突きの先まで火花を走らせる
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    const Vector3 tip = { ppos.x + dirX * data.length, ppos.y, ppos.z };
    fx.Sparks(data.sparks, tip);
    fx.Ring(data.ring, tip);
}

/** @brief ハンマー: 地面を叩き、周りの敵を衝撃波で浮かせて崩す */
void GamePlayScene::FinishShockwave(const WeaponFinisherData& data) {
    const Vector3& ppos = player_->GetPosition();
    for (const FinisherTarget& target : CollectFinisherTargets()) {
        const Vector3 epos = target.enemy->GetPosition();
        if (std::abs(epos.x - ppos.x) > data.radius || std::abs(epos.y - ppos.y) > data.halfHeight) {
            continue;
        }
        if (DamageByFinisher(target, data.damageMult, CurrentWeaponDamage())) {
            // 叩いた所から外へ向けて崩す
            target.enemy->ApplyComboReaction(SignOf(epos.x - ppos.x) * data.knockX, data.knockY, false, ppos.x, true);
            target.enemy->ApplyHitstun(data.stunSeconds, true);
        }
    }
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Ring(data.ring, ppos, { .speed = data.radius });
    fx.Sparks(data.sparks, ppos);
}

/** @brief 大剣: 前へ振り下ろし、浮いている敵は地面へ叩きつけ、地上の敵は長く止める */
void GamePlayScene::FinishSlamDown(const WeaponFinisherData& data) {
    const Vector3& ppos = player_->GetPosition();
    const float dirX = player_->GetLastDirX();
    ForEachTargetInFront(data.length, data.halfHeight, [&](const FinisherTarget& target) {
        if (DamageByFinisher(target, data.damageMult, CurrentWeaponDamage())) {
            // 下向きの速さを与えると、浮いている敵は地面へ落ち、地上の敵はその場に留まる
            target.enemy->Launch(-data.slamSpeed);
            target.enemy->ApplyHitstun(data.stunSeconds, true);
        }
    });
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    const Vector3 impact = { ppos.x + dirX * data.length * kSlamImpactShare, ppos.y, ppos.z };
    fx.Ring(data.ring, impact);
    fx.Sparks(data.sparks, impact);
}

/** @brief 鎌: 回りながら周りの敵を手元へ引き寄せる */
void GamePlayScene::FinishPullSpin(const WeaponFinisherData& data) {
    const Vector3& ppos = player_->GetPosition();
    for (const FinisherTarget& target : CollectFinisherTargets()) {
        const Vector3 epos = target.enemy->GetPosition();
        const float dx = epos.x - ppos.x;
        if (std::abs(dx) > data.radius || std::abs(epos.y - ppos.y) > data.halfHeight) {
            continue;
        }
        if (!DamageByFinisher(target, data.damageMult, CurrentWeaponDamage())) {
            continue;
        }
        // 離れている敵だけを、手元の決めた距離まで寄せる（寄せた先が壁の中なら寄せない）
        if (std::abs(dx) > data.offset) {
            const Vector3 pulled = { ppos.x + SignOf(dx) * data.offset, epos.y, epos.z };
            // 閉じた扉の向こうの敵を、扉ごしに手元へ引き抜かないよう通り道も調べる
            const float halfWidth = target.enemy->WallBodyHalfWidth(kWarpBodyHalfWidth);
            if (!IsSolidAt(pulled) && !IsEnemyPathBlocked(epos, pulled, halfWidth)) {
                target.enemy->SetPosition(pulled);
            }
        }
        target.enemy->ApplyHitstun(data.stunSeconds, true);
    }
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Ring(data.ring, ppos, { .speed = data.radius });
    fx.Sparks(data.sparks, ppos);
}

/** @brief 短剣: 前にいる一番近い敵の背後へ回り込み、振り向いて斬る */
void GamePlayScene::FinishWarpBehind(const WeaponFinisherData& data) {
    const Vector3 ppos = player_->GetPosition();
    const float dirX = player_->GetLastDirX();
    // 前にいて届く距離の、一番近い敵を探す
    const FinisherTarget* nearest = nullptr;
    float nearestDistance = FLT_MAX;
    const std::vector<FinisherTarget> targets = CollectFinisherTargets();
    for (const FinisherTarget& target : targets) {
        const Vector3 epos = target.enemy->GetPosition();
        const float ahead = (epos.x - ppos.x) * dirX;
        if (ahead < 0.0f || ahead > data.radius || std::abs(epos.y - ppos.y) > data.halfHeight || ahead >= nearestDistance) {
            continue;
        }
        nearestDistance = ahead;
        nearest = &target;
    }
    if (nearest == nullptr) {
        return;
    }
    const Vector3 epos = nearest->enemy->GetPosition();
    const Vector3 behind = { epos.x + dirX * data.offset, ppos.y, ppos.z };
    // 扉に押し付けられた敵の背後は扉の向こう側になるので、立てる所でも途中に壁があれば回り込まない
    if (!CanPlayerStandAt(behind) || IsPlayerPathBlocked(behind)) {
        return;
    }
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Ring(data.ring, ppos);
    player_->SetPosition(behind);
    player_->FaceTarget(epos);
    fx.Ring(data.ring, behind);
    fx.Sparks(data.sparks, epos);
    if (DamageByFinisher(*nearest, data.damageMult, CurrentWeaponDamage())) {
        nearest->enemy->ApplyHitstun(data.stunSeconds, true);
    }
}

/** @brief 斧: 前へ投げ、貫通して戻ってくる（当たりは UpdateThrownWeapons で数える） */
void GamePlayScene::FinishThrow(const WeaponFinisherData& data) {
    const Vector3& ppos = player_->GetPosition();
    ThrownWeaponParams params;
    params.speed = data.throwSpeed;
    params.distance = data.throwDistance;
    params.spinSpeed = data.throwSpinSpeed;
    params.hitRadius = data.throwHitRadius;
    params.maxSeconds = data.throwMaxSeconds;
    params.scale = data.throwScale;
    const Vector3 start = { ppos.x, ppos.y + kThrowHeight, ppos.z };
    if (thrownWeapons_.Launch(start, player_->GetLastDirX(), params)) {
        const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
        fx.Sparks(data.sparks, start);
    }
}

/** @brief 投げる武器の見た目を用意する（シーンの初期化で1回。ゲーム中は作り直さない） */
void GamePlayScene::InitializeThrownWeapons() {
    thrownWeapons_.Initialize(modelCommon_.get(), kThrownWeaponModel, kThrownWeaponTexture);
}

/** @brief 投げた武器を進め、当たった敵にダメージを与える（毎フレーム呼ぶ） */
void GamePlayScene::UpdateThrownWeapons() {
    if (!thrownWeapons_.HasActive()) {
        return;
    }
    const WeaponFinisherData& data = WeaponFinisherTuning::Get(WeaponType::kAxe);
    // 投げた武器と重なる生きた敵を返す
    const auto query = [this](const Vector3& position, float radius) {
        std::vector<EnemyEntity*> hits;
        for (const FinisherTarget& target : CollectFinisherTargets()) {
            const AABB box = EnemyHitBoxAt(target.enemy->GetPosition());
            if (position.x >= box.min.x - radius && position.x <= box.max.x + radius
                && position.y >= box.min.y - radius && position.y <= box.max.y + radius) {
                hits.push_back(target.enemy);
            }
        }
        return hits;
    };
    const Vector3& ppos = player_->GetPosition();
    const Vector3 returnTarget = { ppos.x, ppos.y + kThrowHeight, ppos.z };
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    for (const ThrownWeaponHit& hit : thrownWeapons_.Update(gameConstants::kFrameDeltaTime, returnTarget, query)) {
        // 当たった時にまだ生きているかを見直す（同じフレームの別の当たりで倒れていることがある）
        if (hit.target == nullptr || hit.target->IsDefeated()) {
            continue;
        }
        const FinisherTarget target = { hit.target, hit.target == enemy_ };
        if (DamageByFinisher(target, data.damageMult, ThrownWeaponDamage())) {
            const float pushDir = SignOf(hit.target->GetPosition().x - hit.position.x);
            hit.target->ApplyComboReaction(pushDir * data.knockX * kThrowHitReactionScale, data.knockY, false, ppos.x, false);
            hit.target->ApplyHitstun(data.stunSeconds, true);
        }
        fx.Sparks(data.sparks, hit.position);
    }
}

/** @brief 今持っている武器の威力 */
float GamePlayScene::CurrentWeaponDamage() const {
    return WeaponManager::GetInstance()->GetCurrent().damage;
}

/** @brief 投げる武器（斧）の威力（投げた後に持ち替えても変わらないよう、斧のデータから読む） */
float GamePlayScene::ThrownWeaponDamage() const {
    for (const WeaponData& weapon : WeaponManager::GetInstance()->GetList()) {
        if (weapon.type == WeaponType::kAxe) {
            return weapon.damage;
        }
    }
    return CurrentWeaponDamage();
}

/** @brief プレイヤーがその位置に立てるか（体が壁にめり込まず、足元に床がある） */
bool GamePlayScene::CanPlayerStandAt(const Vector3& position) const {
    // 今の体の箱を、その位置へずらして使う（足元は床に触れているだけでは数えないよう少し上げる）
    const Vector3& current = player_->GetPosition();
    const AABB now = player_->GetCollider().aabb;
    const float dx = position.x - current.x;
    const float dy = position.y - current.y;
    const AABB body = { { now.min.x + dx, now.min.y + dy + kFootClearance, now.min.z }, { now.max.x + dx, now.max.y + dy, now.max.z } };
    const float feetY = now.min.y + dy;
    bool hasGround = false;
    for (const AABB& solid : GetStageEditor().GetTerrainColliders()) {
        if (Collision::CheckCollision(body, solid)) {
            return false;
        }
        // 真下の少し下までに上面がある箱を、立てる床とみなす
        const bool underFeet = position.x >= solid.min.x && position.x <= solid.max.x;
        const bool topNearFeet = solid.max.y <= feetY + kFootClearance && solid.max.y >= feetY - kGroundProbeDepth;
        hasGround = hasGround || (underFeet && topNearFeet);
    }
    return hasGround;
}

/** @brief プレイヤーを今の位置から横へその位置まで動かす通り道に、壁や閉じた扉があるか */
bool GamePlayScene::IsPlayerPathBlocked(const Vector3& position) const {
    return IsPlayerSweepBlocked(player_->GetPosition(), { position.x, player_->GetPosition().y, position.z });
}

/** @brief プレイヤーを from から横へ to まで動かす通り道に、壁や閉じた扉があるか（高さは to で見る） */
bool GamePlayScene::IsPlayerSweepBlocked(const Vector3& from, const Vector3& to) const {
    // 体の箱を to の高さに置き、from から to まで横に伸ばした箱で調べる（足元の床は数えないよう少し上げる）
    const AABB now = player_->GetCollider().aabb;
    const Vector3& current = player_->GetPosition();
    const float halfWidth = (now.max.x - now.min.x) * kHalf;
    const float bottom = now.min.y - current.y + to.y;
    const float top = now.max.y - current.y + to.y;
    const AABB path = { { (std::min)(from.x, to.x) - halfWidth + kPathSideClearance, bottom + kFootClearance, now.min.z },
        { (std::max)(from.x, to.x) + halfWidth - kPathSideClearance, top, now.max.z } };
    const std::vector<AABB> solids = GetStageEditor().GetTerrainColliders();
    return std::any_of(solids.begin(), solids.end(), [&](const AABB& solid) {
        return Collision::CheckCollision(path, solid);
    });
}

/** @brief プレイヤーが from から to へ動いた間に、体の高さにある壁や閉じた扉の真ん中を横切ったか */
bool GamePlayScene::DidPlayerCrossWall(const Vector3& from, const Vector3& to) const {
    if (from.x == to.x) {
        return false;
    }
    // 体の高さは動く前と後の両方で見る（跳び越えた壁や、上から落ちてきた所の床は数えない）
    const AABB now = player_->GetCollider().aabb;
    const Vector3& current = player_->GetPosition();
    const float bottomOffset = now.min.y - current.y + kFootClearance;
    const float topOffset = now.max.y - current.y;
    const auto atBodyHeight = [&](const AABB& solid, float y) {
        return solid.max.y > y + bottomOffset && solid.min.y < y + topOffset;
    };
    const float minX = (std::min)(from.x, to.x);
    const float maxX = (std::max)(from.x, to.x);
    const std::vector<AABB> solids = GetStageEditor().GetTerrainColliders();
    // 高さ1のブロックを積んだ壁もあるので、横切った真ん中の位置に並ぶブロックを1本の列として見て、
    // 動く前の体の高さにも動いた後の体の高さにも、その列のどれかがあれば壁を横切ったとみなす（低い壁を跳び越えた時は数えない）
    const auto columnAt = [&](float centerX, float y) {
        return std::any_of(solids.begin(), solids.end(), [&](const AABB& other) {
            return std::abs((other.min.x + other.max.x) * kHalf - centerX) < kWallColumnTolerance && atBodyHeight(other, y);
        });
    };
    for (const AABB& solid : solids) {
        const float centerX = (solid.min.x + solid.max.x) * kHalf;
        if (minX < centerX && centerX < maxX && columnAt(centerX, from.y) && columnAt(centerX, to.y)) {
            return true;
        }
    }
    return false;
}

/** @brief 敵を from から横へ to まで動かす通り道に、壁や閉じた扉があるか */
bool GamePlayScene::IsEnemyPathBlocked(const Vector3& from, const Vector3& to, float halfWidth) const {
    // 体の高さの帯を、動く前から動いた後まで横に伸ばした箱で調べる（足元の床は数えない）
    const AABB path = { { (std::min)(from.x, to.x) - halfWidth + kPathSideClearance, from.y + kWarpBodyBottom, -kStageHalfDepth },
        { (std::max)(from.x, to.x) + halfWidth - kPathSideClearance, from.y + kWarpBodyTop, kStageHalfDepth } };
    const std::vector<AABB> solids = GetStageEditor().GetTerrainColliders();
    return std::any_of(solids.begin(), solids.end(), [&](const AABB& solid) {
        return Collision::CheckCollision(path, solid);
    });
}

/** @brief その位置に立つと壁の中か（足元の床は数えない） */
bool GamePlayScene::IsSolidAt(const Vector3& position) const {
    const AABB body = { { position.x - kWarpBodyHalfWidth, position.y + kWarpBodyBottom, -kStageHalfDepth },
        { position.x + kWarpBodyHalfWidth, position.y + kWarpBodyTop, kStageHalfDepth } };
    const std::vector<AABB> solids = GetStageEditor().GetTerrainColliders();
    return std::any_of(solids.begin(), solids.end(), [&](const AABB& solid) {
        return Collision::CheckCollision(body, solid);
    });
}

} // namespace engine::game

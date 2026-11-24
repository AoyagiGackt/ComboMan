/**
 * @file AllyBoss.cpp
 * @brief 味方のボス（AllyBoss）の実装。自動で近くの敵を狙い、2P の時は操作どおりに動いて近接で攻撃する
 */
#include "AllyBoss.h"
#include "GroundSupport.h"
#include "GameConstants.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace engine::game {
namespace {
constexpr const char* kTuningPath = "Resources/Config/ally_boss.json";
constexpr float kBodyHalfWidth = 0.35f; // 床に立つ時と弾の当たりに使う体の半分の幅
constexpr float kBodyHeight = 1.6f; // 弾の当たりに使う体の高さ
constexpr float kFallLimitBelowPlayer = 12.0f; // プレイヤーよりこれだけ下へ落ちたら、プレイヤーの近くへ戻す
constexpr float kStepDeadZone = 0.05f; // これより小さい左右の入力は止まっているとみなす
constexpr float kAttackStopDistance = 0.9f; // 自動の時、敵とこの距離まで近づいたら止まって攻撃する
constexpr float kHalf = 0.5f; // 半分にする割合
constexpr float kFaceOnly = 0.5f; // 自動の時に、歩かずに向きだけ変える入力の大きさ（1未満なら歩かない）
constexpr float kWallBandBottom = 0.3f; // 壁に当たるかを見る体の下端（足元から。段差の上面は見ない）

/** @brief 体が地形にめり込んでいるか（壁と閉じた扉で止めるために使う） */
bool HitsWall(const Vector3& position, float footOffset, const std::vector<engine::AABB>& terrain) {
    const float bottom = position.y - footOffset + kWallBandBottom;
    const float top = position.y - footOffset + kBodyHeight;
    return std::any_of(terrain.begin(), terrain.end(), [&](const engine::AABB& block) {
        return position.x + kBodyHalfWidth > block.min.x && position.x - kBodyHalfWidth < block.max.x
            && top > block.min.y && bottom < block.max.y;
    });
}

/** @brief ally_boss.json の項目 */
constexpr JsonField<AllyBossTuning> kTuningFields[] = {
    { nullptr, "enabled", &AllyBossTuning::enabled },
    { nullptr, "maxHp", &AllyBossTuning::maxHp },
    { "fused", "hpMult", &AllyBossTuning::fusedHpMult },
    { "fused", "damageMult", &AllyBossTuning::fusedDamageMult },
    { nullptr, "damage", &AllyBossTuning::damage },
    { nullptr, "attackInterval", &AllyBossTuning::attackInterval },
    { nullptr, "attackHitTime", &AllyBossTuning::attackHitTime },
    { nullptr, "attackDuration", &AllyBossTuning::attackDuration },
    { nullptr, "comboWindow", &AllyBossTuning::comboWindow },
    { nullptr, "comboSteps", &AllyBossTuning::comboSteps },
    { nullptr, "attackReach", &AllyBossTuning::attackReach },
    { nullptr, "attackHeight", &AllyBossTuning::attackHeight },
    { nullptr, "hitstunSeconds", &AllyBossTuning::hitstunSeconds },
    { nullptr, "walkSpeed", &AllyBossTuning::walkSpeed },
    { nullptr, "jumpSpeed", &AllyBossTuning::jumpSpeed },
    { nullptr, "gravity", &AllyBossTuning::gravity },
    { nullptr, "maxFallSpeed", &AllyBossTuning::maxFallSpeed },
    { nullptr, "dodgeSpeed", &AllyBossTuning::dodgeSpeed },
    { nullptr, "dodgeSeconds", &AllyBossTuning::dodgeSeconds },
    { nullptr, "hurtInvincibleSeconds", &AllyBossTuning::hurtInvincibleSeconds },
    { nullptr, "aggroRange", &AllyBossTuning::aggroRange },
    { nullptr, "followDistance", &AllyBossTuning::followDistance },
    { nullptr, "leashDistance", &AllyBossTuning::leashDistance },
    { nullptr, "enemyHitReach", &AllyBossTuning::enemyHitReach },
    { nullptr, "enemyHitHeight", &AllyBossTuning::enemyHitHeight },
    { nullptr, "bulletHitRadius", &AllyBossTuning::bulletHitRadius },
    { nullptr, "respawnSeconds", &AllyBossTuning::respawnSeconds },
    { nullptr, "idleSeconds", &AllyBossTuning::idleSeconds },
    { nullptr, "footOffset", &AllyBossTuning::footOffset },
    { nullptr, "screenMargin", &AllyBossTuning::screenMargin },
    { nullptr, "color", &AllyBossTuning::color },
    { nullptr, "cloneScale", &AllyBossTuning::cloneScale },
    { "rim", "color", &AllyBossTuning::rimColor },
    { "rim", "power", &AllyBossTuning::rimPower },
    { "rim", "intensity", &AllyBossTuning::rimIntensity },
    { "rim", "pulseSpeed", &AllyBossTuning::rimPulseSpeed },
    { "rim", "pulseDepth", &AllyBossTuning::rimPulseDepth },
    { "arena", "assistDamage", &AllyBossTuning::arenaAssistDamage },
    { "arena", "assistCooldown", &AllyBossTuning::arenaAssistCooldown },
    { "arena", "autoAssistInterval", &AllyBossTuning::arenaAutoAssistInterval },
    { "arena", "assistSeconds", &AllyBossTuning::arenaAssistSeconds },
    { "arena", "assistHitTime", &AllyBossTuning::arenaAssistHitTime },
    { "arena", "assistSide", &AllyBossTuning::arenaAssistSide },
    { "keys", "left", &AllyBossTuning::keyLeft },
    { "keys", "right", &AllyBossTuning::keyRight },
    { "keys", "jump", &AllyBossTuning::keyJump },
    { "keys", "attack", &AllyBossTuning::keyAttack },
    { "keys", "dodge", &AllyBossTuning::keyDodge },
    { "keys", "joinA", &AllyBossTuning::keyJoinA },
    { "keys", "joinB", &AllyBossTuning::keyJoinB },
    { "keys", "summon", &AllyBossTuning::keySummon },
};
} // namespace

/** @brief 調整値を読む（初回だけファイルを読み、以後は覚えた値を返す） */
const AllyBossTuning& AllyTune() {
    static const AllyBossTuning kTuning = ReadJsonFields(JsonHelper::Load(kTuningPath), kTuningFields);
    return kTuning;
}

/** @brief 使う前の準備（モデルの共通設定を覚える。体はまだ作らない） */
void AllyBoss::Initialize(engine::graphics::ModelCommon* modelCommon) {
    modelCommon_ = modelCommon;
    active_ = false;
    body_ = nullptr;
    respawnTimer_ = 0.0f;
}

/** @brief 主軸の武器の体を返す（まだ無ければ作る。作った体はステージを出るまで残す） */
PlayerCloneBody* AllyBoss::BodyFor(WeaponType weapon) {
    auto found = bodies_.find(weapon);
    if (found != bodies_.end()) {
        return found->second.get();
    }
    // プレイヤーと同じ見た目の体に、ボスの武器を持たせる（作った時に一度行列も作る）
    auto body = std::make_unique<PlayerCloneBody>();
    body->Initialize(modelCommon_, position_, weapon);
    body->SetVisible(false);
    PlayerCloneBody* raw = body.get();
    bodies_.emplace(weapon, std::move(body));
    return raw;
}

/** @brief 呼び出す（主軸の武器の体を出してプレイヤーの近くに置く） */
void AllyBoss::Summon(WeaponType weapon, const Vector4& color, const Vector3& position, bool fused) {
    fused_ = fused;
    // 前の体は隠し、主軸の武器の体を出す
    if (body_ != nullptr) {
        body_->SetVisible(false);
    }
    weapon_ = weapon;
    position_ = position;
    body_ = BodyFor(weapon);
    body_->SetColor(color);
    UpdateRimGlow();
    body_->SetPosition(position_);
    body_->SetVisible(!hidden_);
    body_->RefreshVisualTransforms();
    hp_ = MaxHp();
    velocityY_ = 0.0f;
    invincibleTimer_ = AllyTune().hurtInvincibleSeconds;
    attackTimer_ = -1.0f;
    comboStep_ = 0;
    comboQueued_ = false;
    dodgeTimer_ = 0.0f;
    autoAttackCooldown_ = 0.0f;
    active_ = true;
}

/** @brief 引っ込める（体を隠す。体力はそのまま次に呼んだ時に満タンへ戻す） */
void AllyBoss::Dismiss() {
    active_ = false;
    if (body_ != nullptr) {
        body_->SetVisible(false);
    }
}

/** @brief 止めている間（演出中など）に、体を隠すか戻すかを決める */
void AllyBoss::SetHidden(bool hidden) {
    hidden_ = hidden;
    if (body_ != nullptr) {
        body_->SetVisible(active_ && !hidden_);
    }
}

/** @brief 倒れてから再び呼べるまでの時間だけを進める（呼び出していない間も毎フレーム呼ぶ） */
void AllyBoss::TickRespawn(float dt) {
    respawnTimer_ = (std::max)(respawnTimer_ - dt, 0.0f);
}

/** @brief 今の体力の上限（合体していれば上がる） */
int AllyBoss::MaxHp() const {
    const float mult = fused_ ? AllyTune().fusedHpMult : 1.0f;
    return (std::max)(1, static_cast<int>(std::round(static_cast<float>(AllyTune().maxHp) * mult)));
}

/** @brief 体力の割合（0〜1） */
float AllyBoss::HpRatio() const {
    return static_cast<float>(hp_) / static_cast<float>(MaxHp());
}

/** @brief 一番近い生きている敵を返す（狙う距離の外なら nullptr） */
EnemyEntity* AllyBoss::NearestEnemy(const AllySurroundings& around) const {
    EnemyEntity* nearest = nullptr;
    float best = AllyTune().aggroRange;
    for (EnemyEntity* enemy : around.enemies) {
        const float distance = std::abs(enemy->GetPosition().x - position_.x);
        if (distance < best) {
            best = distance;
            nearest = enemy;
        }
    }
    return nearest;
}

/** @brief 自動の時の左右の動きを決める（狙う敵へ近づくか、プレイヤーの後ろへ戻る） */
float AllyBoss::ThinkMoveX(const AllySurroundings& around, bool& wantsAttack) const {
    wantsAttack = false;
    // プレイヤーから離れすぎていれば、敵よりもプレイヤーへ戻るのを先にする
    const float toPlayer = around.playerPosition.x - position_.x;
    if (std::abs(toPlayer) > AllyTune().leashDistance * kHalf) {
        return toPlayer > 0.0f ? 1.0f : -1.0f;
    }
    if (const EnemyEntity* target = NearestEnemy(around)) {
        const float toEnemy = target->GetPosition().x - position_.x;
        if (std::abs(toEnemy) <= kAttackStopDistance + AllyTune().attackReach * kHalf) {
            wantsAttack = true;
            return toEnemy > 0.0f ? kFaceOnly : -kFaceOnly;
        }
        return toEnemy > 0.0f ? 1.0f : -1.0f;
    }
    // 敵がいなければ、プレイヤーの少し後ろで待つ
    const float behind = around.playerPosition.x - (toPlayer > 0.0f ? AllyTune().followDistance : -AllyTune().followDistance);
    const float toBehind = behind - position_.x;
    return std::abs(toBehind) > AllyTune().followDistance * kHalf ? (toBehind > 0.0f ? 1.0f : -1.0f) : 0.0f;
}

/** @brief 攻撃の段を始める */
void AllyBoss::StartAttack() {
    attackTimer_ = 0.0f;
    attackHitDone_ = false;
    if (body_ != nullptr) {
        body_->PlayScriptedAttackAnimation();
    }
}

/** @brief 重力と床の当たり判定で高さを決める */
void AllyBoss::ApplyGravity(const std::vector<engine::AABB>& terrain) {
    const AllyBossTuning& tune = AllyTune();
    velocityY_ = (std::max)(velocityY_ - tune.gravity, -tune.maxFallSpeed);
    const float previousY = position_.y;
    position_.y += velocityY_;
    onGround_ = false;
    if (velocityY_ > 0.0f) {
        return;
    }
    // 移動前の足元より下にある一番高い床の上面に、落ちてきた分だけ乗せる
    const Vector3 probe = { position_.x, previousY, position_.z };
    const std::optional<float> floor = FindSupportingFloor(probe, kBodyHalfWidth, 0.0f, tune.footOffset, terrain);
    if (floor && position_.y - tune.footOffset <= *floor) {
        position_.y = *floor + tune.footOffset;
        velocityY_ = 0.0f;
        onGround_ = true;
    }
}

/** @brief 1フレーム進める（自動か 2P の操作で動き、攻撃する） */
bool AllyBoss::Update(const AllySurroundings& around, const AllyControl* control) {
    if (!active_ || body_ == nullptr) {
        return false;
    }
    const AllyBossTuning& tune = AllyTune();
    const float dt = gameConstants::kFrameDeltaTime;
    UpdateRimGlow();
    invincibleTimer_ = (std::max)(invincibleTimer_ - dt, 0.0f);
    autoAttackCooldown_ = (std::max)(autoAttackCooldown_ - dt, 0.0f);

    // 左右の動きと攻撃の入力を、2P の操作か自動の考えから決める
    float moveX = 0.0f;
    bool attackPressed = false;
    if (control != nullptr) {
        moveX = control->moveX;
        attackPressed = control->attack;
        if (control->jump && onGround_) {
            velocityY_ = tune.jumpSpeed;
        }
        if (control->dodge && dodgeTimer_ <= 0.0f) {
            dodgeTimer_ = tune.dodgeSeconds;
            invincibleTimer_ = (std::max)(invincibleTimer_, tune.dodgeSeconds);
        }
    } else {
        bool wantsAttack = false;
        moveX = ThinkMoveX(around, wantsAttack);
        attackPressed = wantsAttack && autoAttackCooldown_ <= 0.0f;
    }

    // 攻撃: 2P は押すたびに段が進み、自動は間隔ごとに1段だけ振る
    const bool attacking = attackTimer_ >= 0.0f;
    if (attackPressed) {
        if (!attacking) {
            comboStep_ = 1;
            StartAttack();
            autoAttackCooldown_ = tune.attackInterval;
        } else if (control != nullptr && comboStep_ < tune.comboSteps && attackTimer_ >= tune.attackDuration - tune.comboWindow) {
            comboQueued_ = true;
        }
    }
    bool hitNow = false;
    if (attackTimer_ >= 0.0f) {
        attackTimer_ += dt;
        if (!attackHitDone_ && attackTimer_ >= tune.attackHitTime) {
            attackHitDone_ = true;
            hitNow = true;
        }
        if (attackTimer_ >= tune.attackDuration) {
            // 次の段を先に押していれば続けて振る
            if (comboQueued_) {
                comboQueued_ = false;
                ++comboStep_;
                StartAttack();
            } else {
                attackTimer_ = -1.0f;
                comboStep_ = 0;
            }
        }
    }

    // 横の動き（攻撃中は止まり、回避中は向いている逆へ飛びのく）
    if (std::abs(moveX) > kStepDeadZone) {
        facing_ = moveX > 0.0f ? 1.0f : -1.0f;
    }
    float stepX = attackTimer_ >= 0.0f ? 0.0f : moveX * tune.walkSpeed;
    if (std::abs(moveX) < 1.0f && control == nullptr) {
        stepX = 0.0f;
    }
    if (dodgeTimer_ > 0.0f) {
        dodgeTimer_ = (std::max)(dodgeTimer_ - dt, 0.0f);
        stepX = -facing_ * tune.dodgeSpeed;
    }
    position_.x += stepX;
    // 壁や閉じた扉に入ったら、入る前へ戻す
    if (around.terrain != nullptr && stepX != 0.0f && HitsWall(position_, tune.footOffset, *around.terrain)) {
        position_.x -= stepX;
    }
    // 2P の時は画面の外へ出さない
    if (control != nullptr && around.cameraHalfWidth > 0.0f) {
        const float limit = around.cameraHalfWidth - tune.screenMargin;
        position_.x = std::clamp(position_.x, around.cameraX - limit, around.cameraX + limit);
    }
    if (around.terrain != nullptr) {
        ApplyGravity(*around.terrain);
    }
    // プレイヤーから離れすぎた・下へ落ちた時は、プレイヤーの近くへ戻す
    const bool tooFar = std::abs(position_.x - around.playerPosition.x) > tune.leashDistance;
    const bool fellOut = position_.y < around.playerPosition.y - kFallLimitBelowPlayer;
    if (tooFar || fellOut) {
        position_ = { around.playerPosition.x - facing_ * tune.followDistance, around.playerPosition.y, around.playerPosition.z };
        velocityY_ = 0.0f;
    }

    // 体の見た目へ写す（走りと待機は動いたかで決め、攻撃の見た目は攻撃の段が持つ）
    body_->SetPosition(position_);
    body_->SetScriptedFacing(facing_);
    if (attackTimer_ < 0.0f) {
        body_->PlayHeldAnimation(std::abs(stepX) > 0.0f);
    }
    body_->Update();
    return hitNow;
}

/** @brief 攻撃の当たる箱に入っている敵を集める */
std::vector<EnemyEntity*> AllyBoss::CollectHitTargets(const AllySurroundings& around) const {
    std::vector<EnemyEntity*> targets;
    const float reach = AllyTune().attackReach;
    const float height = AllyTune().attackHeight;
    const float nearX = facing_ > 0.0f ? position_.x - kBodyHalfWidth : position_.x - reach;
    const float farX = facing_ > 0.0f ? position_.x + reach : position_.x + kBodyHalfWidth;
    for (EnemyEntity* enemy : around.enemies) {
        const Vector3 position = enemy->GetPosition();
        if (position.x >= nearX && position.x <= farX && std::abs(position.y - position_.y) <= height) {
            targets.push_back(enemy);
        }
    }
    return targets;
}

/** @brief 敵の攻撃を受ける（無敵の間は受けない） */
bool AllyBoss::TakeDamage(int amount) {
    if (!active_ || invincibleTimer_ > 0.0f || amount <= 0) {
        return false;
    }
    hp_ = (std::max)(hp_ - amount, 0);
    invincibleTimer_ = AllyTune().hurtInvincibleSeconds;
    if (hp_ > 0) {
        return false;
    }
    // 倒れたら消えて、しばらく呼べない
    Dismiss();
    respawnTimer_ = AllyTune().respawnSeconds;
    return true;
}

/** @brief 指定した位置が体に当たるか（敵の弾の当たり） */
bool AllyBoss::Overlaps(const Vector3& position, float radius) const {
    if (!active_ || hidden_) {
        return false;
    }
    const bool overlapX = std::abs(position.x - position_.x) <= kBodyHalfWidth + radius;
    const bool overlapY = position.y >= position_.y - radius && position.y <= position_.y + kBodyHeight + radius;
    return overlapX && overlapY;
}

/** @brief ラスボス戦の援護を始める（主軸の武器の体を一瞬だけ出して斬らせる） */
void AllyBoss::StartArenaAssist(WeaponType weapon, const Vector4& color, const Vector3& position, float facing) {
    body_ = BodyFor(weapon);
    weapon_ = weapon;
    facing_ = facing;
    body_->SetColor(color);
    UpdateRimGlow();
    body_->SetPosition(position);
    body_->SetScriptedFacing(facing);
    body_->SetVisible(true);
    body_->PlayScriptedAttackAnimation();
    body_->Update();
    assistTimer_ = 0.0f;
    assistHitDone_ = false;
    assistCooldown_ = AllyTune().arenaAssistCooldown;
}

/** @brief ラスボス戦の援護を1フレーム進める（斬る瞬間なら true。終わったら体を隠す） */
bool AllyBoss::UpdateArenaAssist() {
    if (assistTimer_ < 0.0f || body_ == nullptr) {
        return false;
    }
    const AllyBossTuning& tune = AllyTune();
    assistTimer_ += gameConstants::kFrameDeltaTime;
    UpdateRimGlow();
    body_->Update();
    bool hitNow = false;
    if (!assistHitDone_ && assistTimer_ >= tune.arenaAssistHitTime) {
        assistHitDone_ = true;
        hitNow = true;
    }
    // 見せ終えたら体を隠す（ふだんの味方を呼んでいれば、そちらは隠したまま）
    if (assistTimer_ >= tune.arenaAssistSeconds) {
        assistTimer_ = -1.0f;
        body_->SetVisible(active_ && !hidden_);
    }
    return hitNow;
}

/** @brief 次に援護できるまでの残り秒数を進め、残りを返す */
float AllyBoss::TickAssistCooldown(float dt) {
    assistCooldown_ = (std::max)(assistCooldown_ - dt, 0.0f);
    return assistCooldown_;
}

/** @brief 体のふちの光を進める（味方の目印。ゆっくり強まったり弱まったりさせる） */
void AllyBoss::UpdateRimGlow() {
    if (body_ == nullptr) {
        return;
    }
    const AllyBossTuning& tune = AllyTune();
    rimTimer_ += gameConstants::kFrameDeltaTime;
    // 0〜1 で揺らし、一番弱い時でも pulseDepth の分しか減らさない（消えて敵と紛れないように）
    const float wave = (1.0f + std::sin(rimTimer_ * tune.rimPulseSpeed)) * kHalf;
    const float intensity = tune.rimIntensity * (1.0f - tune.rimPulseDepth * wave);
    body_->SetRimGlow(tune.rimColor, tune.rimPower, intensity);
}

/** @brief 体を描く（ラスボス戦の援護の間も描く） */
void AllyBoss::Draw() {
    if (body_ != nullptr && ((active_ && !hidden_) || IsAssisting())) {
        body_->Draw();
    }
}

} // namespace engine::game

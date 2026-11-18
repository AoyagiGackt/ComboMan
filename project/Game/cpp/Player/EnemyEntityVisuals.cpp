/** @file EnemyEntityVisuals.cpp
 * @brief 敵の装備位置とヒットストップ中の見た目更新
 */
#include "EnemyEntity.h"
#include "BossExecutionTuning.h"
#include "EnemyTuning.h"
#include "GameConstants.h"
#include <algorithm>
#include <cmath>
namespace engine::game {
/** @brief 落とした武器を地面まで落とし、寝かせた姿勢で置く */
void EnemyEntity::UpdateDroppedWeapon() {
    if (!weaponDropped_ || droppedWeaponLanded_) {
        return;
    }
    droppedWeaponVelY_ -= kDroppedWeaponGravity;
    droppedWeaponPos_.y += droppedWeaponVelY_;
    droppedWeaponSpin_ += kDroppedWeaponSpinPerFrame;
    if (droppedWeaponPos_.y <= droppedWeaponGroundY_) {
        droppedWeaponPos_.y = droppedWeaponGroundY_;
        droppedWeaponLanded_ = true;
    }
}

/** @brief 見た目を置く位置（位置は床の上面から corpseFootOffset だけ上に置いているので、その分だけ下げて足元を床にそろえる） */
Vector3 EnemyEntity::VisualPosition() const {
    // 膝をついたボスは、膝が床に付くまで沈め、肩で息をするように小さく上下させる
    if (IsKneeling()) {
        const BossKneelTuning& kneel = BossExecutionTuningStore::Get().kneel;
        const float breath = std::sin(offerTimer_ * kneel.breathSpeed) * kneel.breathAmount;
        return { pos_.x, pos_.y - EnemyTuning::GetInstance()->Basic().corpseFootOffset - kneel.sink + breath, pos_.z };
    }
    // 倒れた体は床に寝かせた時にめり込まないよう浮かせてあるので、下げない
    if (defeated_) {
        return pos_;
    }
    return { pos_.x, pos_.y - EnemyTuning::GetInstance()->Basic().corpseFootOffset, pos_.z };
}

/** @brief 武器の位置を本体の手元（落としていれば落とした場所）へ合わせる */
void EnemyEntity::PlaceWeapon(const Vector3& bodyPos) {
    if (weaponDropped_) {
        weaponObject_->SetPosition(droppedWeaponPos_);
        return;
    }
    weaponObject_->SetPosition({ bodyPos.x + facingSign_ * kWeaponOffsetX, bodyPos.y + kWeaponOffsetY, bodyPos.z + kWeaponOffsetZ });
}

/** @brief 武器を奪いに行く位置（落とした武器があればそこ、倒れていれば寝た体の真ん中、それ以外は足元） */
Vector3 EnemyEntity::GetWeaponPickupPosition() const {
    if (weaponDropped_) {
        return droppedWeaponPos_;
    }
    if (!defeated_) {
        return pos_;
    }
    // 体は足元を軸にZ軸まわりへ倒れるので、立っていた時の体の真ん中を同じだけ回した位置を使う
    const float lean = object_->GetRotation().z;
    return { pos_.x - std::sin(lean) * kCorpseCenterHeight, pos_.y + std::cos(lean) * kCorpseCenterHeight, pos_.z };
}

/** @brief 撃破後、まだ吸い寄せていない武器を回収位置の上へ浮かべて回し、光らせる */
void EnemyEntity::UpdateOfferedWeapon(bool advance) {
    if (!defeated_ || weaponPulled_) {
        return;
    }
    if (advance) {
        offerTimer_ += gameConstants::kFrameDeltaTime;
    }
    // 膝をつくボスは武器を浮かべず、地面に突き立てたまま光らせる
    if (IsKneeling()) {
        if (!kneelPoseApplied_) {
            ApplyKneelPose();
        }
        PlaceKneelWeapon();
        return;
    }
    // 手元（落ちた武器なら落ちた場所）から、回収位置の上へ勢いよく上がって減速する
    const float rise = (std::min)(offerTimer_ / kOfferRiseSeconds, 1.0f);
    const float eased = 1.0f - (1.0f - rise) * (1.0f - rise);
    const Vector3 from = weaponObject_->GetTransform().translate;
    const Vector3 base = GetWeaponPickupPosition();
    const float hover = std::sin(offerTimer_ * kOfferHoverSpeed) * kOfferHoverAmplitude;
    const Vector3 to = { base.x, base.y + kOfferHeight + hover, base.z };
    weaponObject_->SetPosition({ from.x + (to.x - from.x) * eased, from.y + (to.y - from.y) * eased, from.z + (to.z - from.z) * eased });
    weaponObject_->SetRotation({ kOfferTilt, offerTimer_ * kOfferSpinSpeed, 0.0f });
    const float scale = 1.0f + (kOfferScaleBoost - 1.0f) * eased;
    weaponObject_->SetScale({ weaponScale_.x * scale, weaponScale_.y * scale, weaponScale_.z * scale });
    // 撃破後の灰色ではなく、白と淡い青の間で明滅させて拾えることを目立たせる
    constexpr float kHalf = 0.5f;
    const float pulse = kHalf + kHalf * std::sin(offerTimer_ * kOfferPulseSpeed);
    weaponObject_->SetColor({
        kOfferDimColor.x + (1.0f - kOfferDimColor.x) * pulse,
        kOfferDimColor.y + (1.0f - kOfferDimColor.y) * pulse,
        kOfferDimColor.z + (1.0f - kOfferDimColor.z) * pulse,
        kOfferDimColor.w });
}

/** @brief 膝をつく姿勢（ボーンの曲げ・ゆっくりの待機アニメ）を掛け始める */
void EnemyEntity::ApplyKneelPose() {
    const BossKneelTuning& kneel = BossExecutionTuningStore::Get().kneel;
    std::vector<ReachPoseLayer::Reach> reaches;
    reaches.reserve(kneel.reaches.size());
    for (const KneelReach& reach : kneel.reaches) {
        reaches.push_back({ reach.bone, reach.direction, reach.weight });
    }
    kneelLayer_.SetReaches(reaches);
    object_->SetPoseLayer(&kneelLayer_);
    // 倒れた瞬間の攻撃の姿勢で固まらないよう、待機アニメをゆっくり流して息づかいを見せる
    object_->SetAnimation(idleAnimation_);
    object_->SetAnimSpeed(kneel.animSpeed);
    animationState_ = VisualAnim::kIdle;
    kneelPoseApplied_ = true;
}

/** @brief 膝をついている間、武器を体の前の地面に突き立てて明滅させる */
void EnemyEntity::PlaceKneelWeapon() {
    const BossKneelTuning& kneel = BossExecutionTuningStore::Get().kneel;
    const float facingYaw = facingSign_ >= 0.0f ? gameConstants::kHalfPi : -gameConstants::kHalfPi;
    weaponObject_->SetPosition({ pos_.x + facingSign_ * kneel.weaponOffset.x, pos_.y + kneel.weaponOffset.y, pos_.z + kneel.weaponOffset.z });
    weaponObject_->SetRotation({ 0.0f, facingYaw, kneel.weaponTilt });
    weaponObject_->SetScale(weaponScale_);
    // 撃破後の灰色ではなく、白と淡い青の間で明滅させて抜き取れることを目立たせる
    constexpr float kHalf = 0.5f;
    const float pulse = kHalf + kHalf * std::sin(offerTimer_ * kneel.weaponPulseSpeed);
    weaponObject_->SetColor({
        kneel.weaponDimColor.x + (1.0f - kneel.weaponDimColor.x) * pulse,
        kneel.weaponDimColor.y + (1.0f - kneel.weaponDimColor.y) * pulse,
        kneel.weaponDimColor.z + (1.0f - kneel.weaponDimColor.z) * pulse,
        kneel.weaponDimColor.w });
}

/** @brief ヒットストップ中の1フレーム（物理・AIを止め、被弾直後なら本体を左右に震わせて描画行列だけ更新する） */
void EnemyEntity::UpdateDuringHitStop() {
    // 1フレームだけ立つフラグは停止前のフレームでシーン側が読み終えているため、停止明けに二重に拾われないよう落とす
    justLanded_ = false;
    justFiredAttack_ = false;
    justStartedTelegraph_ = false;

    Vector3 visualPos = VisualPosition();
    if (hitFlashTimer_ > 0.0f) {
        // 殴られた敵だけを白く光らせたまま左右に小刻みに震わせる
        ++hitStopShakeFrame_;
        visualPos.x += (hitStopShakeFrame_ % 2 == 0 ? 1.0f : -1.0f) * kHitStopShakeAmplitude;
        SetBodyColor(defeated_ ? kDefeatedColor : kHitFlashColor);
        weaponObject_->SetColor(defeated_ ? kDefeatedColor : kHitFlashColor);
        const float punchedScale = kBodyScale * (1.0f + kHitScalePunch);
        object_->SetScale({ punchedScale, punchedScale, punchedScale });
    }
    // ボスの技の動きで持ち上げていたら、止まっている間もその高さを保つ
    if (motionApplied_) {
        visualPos.y += motionFrame_.pose.lift;
    }
    object_->SetPosition(visualPos);
    PlaceWeapon(visualPos);

    // アニメーションも止めたまま行列だけ更新する（カメラ揺れで画面に張り付かないように）
    const float animSpeed = object_->GetAnimSpeed();
    object_->SetAnimSpeed(0.0f);
    object_->Update();
    object_->SetAnimSpeed(animSpeed);
    // 技の動きの最中は、武器を手元に付けたままにする
    PlaceMotionWeapon();
    UpdateOfferedWeapon(false);
    weaponObject_->Update();
    SyncMonsterVisual(false);
}

/** @brief 位置だけ再計算する（AI/物理は一切進めない、StageEditor等が外部からpos_を書き換えた後の追従用） */
void EnemyEntity::RefreshVisualTransforms() {
    const Vector3 visualPos = VisualPosition();
    object_->SetPosition(visualPos);
    object_->Update();
    // 手に持つ武器も体へ付け直す（付け直さないと、開始演出などで止めている間は作った時の位置に残り、手ぶらに見える）
    if (!defeated_) {
        PlaceWeapon(visualPos);
        PlaceMotionWeapon();
        weaponObject_->Update();
    }
    SyncMonsterVisual(false);
}

/** @brief HP を回復する（増えた時は体を一瞬緑に光らせ、回復されたことを見た目で伝える） */
void EnemyEntity::Heal(int amount) {
    if (defeated_) {
        return;
    }
    const int before = hp_;
    hp_ = (std::min)(hp_ + amount, maxHp_);
    if (hp_ > before) {
        healFlashTimer_ = EnemyTuning::GetInstance()->Basic().healFlashSeconds;
    }
}

/** @brief 回復されて光っている間、渡した色を緑へ寄せて返す（時間を進めるのは Update だけ） */
Vector4 EnemyEntity::ApplyHealGlow(const Vector4& color, bool advance) {
    const BasicEnemyTuning& tuning = EnemyTuning::GetInstance()->Basic();
    if (healFlashTimer_ <= 0.0f || tuning.healFlashSeconds <= 0.0f) {
        return color;
    }
    if (advance) {
        healFlashTimer_ = (std::max)(healFlashTimer_ - gameConstants::kFrameDeltaTime, 0.0f);
    }
    // 光り始めが一番強く、時間とともに元の色へ戻す
    const float glow = healFlashTimer_ / tuning.healFlashSeconds;
    const Vector4& green = tuning.healFlashColor;
    return { color.x + (green.x - color.x) * glow, color.y + (green.y - color.y) * glow, color.z + (green.z - color.z) * glow, color.w };
}

/** @brief AI を止めて外から動かす間のアニメを、動いているかで待機か走りに合わせる */
void EnemyEntity::PlayHeldAnimation(bool moving) {
    if (defeated_ || MotionOwnsAnimation()) {
        return;
    }
    const VisualAnim desired = moving ? VisualAnim::kRun : VisualAnim::kIdle;
    if (desired != animationState_ || motionAnimOverride_) {
        object_->SetAnimation(moving ? runAnimation_ : idleAnimation_);
        animationState_ = desired;
        motionAnimOverride_ = false;
    }
}

/** @brief 台本で動かしている時に、攻撃の見た目を頭から出す（味方のボスの攻撃に使う） */
void EnemyEntity::PlayScriptedAttackAnimation() {
    if (defeated_ || MotionOwnsAnimation()) {
        return;
    }
    object_->SetAnimation(attackAnimation_);
    animationState_ = VisualAnim::kAttack;
    motionAnimOverride_ = false;
}

/** @brief 台本で動かす間の1フレーム（位置・向き・武器の位置を見た目へ反映するだけで、AI と物理は進めない） */
void EnemyEntity::UpdateScripted() {
    // 攻撃の合図は台本の側が出すので、自前の攻撃は始めない
    justLanded_ = false;
    justFiredAttack_ = false;
    justStartedTelegraph_ = false;
    knockVelX_ = 0.0f;
    hitFlashTimer_ = (std::max)(hitFlashTimer_ - gameConstants::kFrameDeltaTime, 0.0f);
    // 向きと位置を反映して行列を更新する
    const float facingYaw = facingSign_ >= 0.0f ? gameConstants::kHalfPi : -gameConstants::kHalfPi;
    object_->SetRotation({ 0.0f, facingYaw, 0.0f });
    weaponObject_->SetRotation({ 0.0f, facingYaw, kWeaponRestTilt });
    const Vector3 visualPos = VisualPosition();
    object_->SetPosition(visualPos);
    PlaceWeapon(visualPos);
    // ボスの技の動きは体の行列を作る前に掛け、武器は体の行列ができてから手元へ付ける
    ApplyMotionBody();
    object_->Update();
    PlaceMotionWeapon();
    weaponObject_->Update();
    // モンスターの見た目にも位置と向きを写す（写さないと急降下などで見た目だけその場に残り、体力バーや当たりとずれる）
    SyncMonsterVisual(true);
}
} // namespace engine::game

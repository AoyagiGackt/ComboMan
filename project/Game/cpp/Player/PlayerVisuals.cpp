/**
 * @file PlayerVisuals.cpp
 * @brief プレイヤーの見た目・アニメーション・武器装着・描画
 */
#include "Player.h"
#include "GameActions.h"
#include "SceneVisualTuning.h"
#include "CombatTuning.h"
#include "CharacterVisuals.h"
#include "Easing.h"
#include "GameConstants.h"
#include "GravityBody.h"
#include "Input.h"
#include "ModelCommon.h"
#include "ModelManager.h"
#include "OutlineEffect.h"
#include "PipelineStateGuard.h"
#include "PlayerTuning.h"
#include "Weapon.h"
#include "WeaponManager.h"
#include <algorithm>
#include <cmath>
#include <limits>
using namespace engine::graphics;

namespace engine::game {
namespace {
/** @brief 読み込み済みの調整値を取得する */
const PlayerTuning& Tuning() {
    return PlayerTuning::Get();
}

// 本体・武器の黒縁アウトライン（背景との同化と武器の視認性の低さを補う）
// 幅・濃さともに控えめにし、異常が起きているように見えるほど主張しないようにする

constexpr float kStanceFadeInPerSecond = 6.0f; // 技を振り終えてから武器の構えへ戻る速さ（1秒あたりの効き具合）
constexpr float kStanceFadeOutPerSecond = 20.0f; // 技や回避を始めた時に武器の構えを薄める速さ
constexpr const char* kStanceClipPrefix = "stance_"; // combo_motion.json の構えのクリップ名（stance_<武器種別名>）

// フィニッシャーの溜め: 地上の最後の段の振りかぶりをゆっくり取り、前へ少し倒して震わせる
constexpr float kFinisherWindupHold = 0.85f; // 溜めきった時に振りかぶる所（段の当たる瞬間までの割合。1 で当たる直前）
constexpr float kFinisherChargeLean = 0.1f; // 溜めきった時に前へ倒す角度（ラジアン）
constexpr float kFinisherTrembleSpeed = 45.0f; // 溜めの震えの速さ（ラジアン毎秒）
constexpr float kFinisherTrembleAmount = 0.012f; // 溜めきった時の震えの幅（ラジアン）

// 状態ごとの本体の色
constexpr Vector4 kWhite = { 1.0f, 1.0f, 1.0f, 1.0f };
constexpr float kHalfWave = 0.5f; // sin を 0〜1 へ寄せる振幅とオフセット

/** @brief 状態ごとの本体の色（条件が成り立つ行のうち先頭を使う） */
struct BodyTint {
    bool active;
    Vector4 color;
};

}

// ══════════════════════════════════════════════════════
// 初期化
// ══════════════════════════════════════════════════════

/** @brief 見た目を差し替える静的モデル（とテクスチャ）を設定する。空のパスなら差し替えを解除する */
void Player::SetStaticVisualModel(const std::string& modelPath, const std::string& texturePath) {
    staticOverride_.Set(modelCommon_, modelPath, texturePath);
}

/** @brief 移動・接地状況から再生すべきアニメーション状態を切り替える */
void Player::UpdateAnimationState(bool isMoving, bool airborne) {
    // フィニッシャー溜め中は静止ポーズを維持（タイマー管理は Update() 側）
    if (finisher_.charging) {
        return;
    }

    // 射撃中は近接アニメを終了して移動系の構えを土台にする
    if (gunCombo_.GetActive() != nullptr) {
        attackAnimTimer_ = float{};
        rig_->object->SetAnimSpeed(Tuning().gunBaseAnimSpeed);
    }

    // 攻撃モーション中は再生し切るまで状態遷移しない
    if (attackAnimTimer_ > 0.0f) {
        attackAnimTimer_ -= gameConstants::kFrameDeltaTime;
        if (attackAnimTimer_ > 0.0f) {
            return;
        }
        rig_->object->SetAnimSpeed(1.0f); // 攻撃用の速度倍率を戻す
    }

    // 上の行ほど優先。どれにも当たらなければ待機
    const struct {
        bool active;
        const IAnimState* state;
    } candidates[] = {
        { inWater_, &SwimAnim() },
        { airborne, &JumpAnim() },
        { isMoving, &RunAnim() },
    };
    const IAnimState* newState = &IdleAnim();
    for (const auto& candidate : candidates) {
        if (candidate.active) {
            newState = candidate.state;
            break;
        }
    }
    // 構え系の近接武器、または銃を左手に構えている間は武器持ちバリエーション（IdleHold/RunHold）を使う
    // （銃は常時左手に追従表示されるため、素のIdle/Runのままだと構えていないように見えてしまう）
    const Skeleton& skel = rig_->object->GetSkeleton();
    bool hasGunBone = skel.HasJoint(rig_->gunBoneName);
    auto* weaponManager = WeaponManager::GetInstance();
    // やられ演出で武器を手放した後は構えない
    bool hold = !weaponsDropped_ && !explorationMode_
        && ((weaponManager->HasEquippedWeapon() && UsesHoldPose(weaponManager->GetCurrent().type)) || hasGunBone);
    if (newState == animState_ && hold == animHold_) {
        return;
    }
    animState_ = newState;
    animHold_ = hold;
    animState_->Enter(*this, hold, isMoving);
}

//  Anim State（移動系アニメーション状態）

/** @brief 装備の有無に応じた待機アニメーションを開始する状態 */
class Player::IdleAnimState : public IAnimState {
public:
    /** @brief この状態へ入る際の初期設定を適用する */
    void Enter(Player& player, bool hold, bool) const override {
        player.rig_->object->SetAnimation(hold ? player.rig_->idleHoldAnim : player.rig_->idleAnim);
    }
};
/** @brief 装備の有無に応じた走行アニメーションを開始する状態（歩くだけの状態では歩き） */
class Player::RunAnimState : public IAnimState {
public:
    /** @brief この状態へ入る際の初期設定を適用する */
    void Enter(Player& player, bool hold, bool) const override {
        if (player.explorationMode_) {
            player.rig_->object->SetAnimation(player.rig_->walkAnim);
            return;
        }
        player.rig_->object->SetAnimation(hold ? player.rig_->runHoldAnim : player.rig_->runAnim);
    }
};
/** @brief 移動入力に応じたジャンプアニメーションを開始する状態 */
class Player::JumpAnimState : public IAnimState {
public:
    /** @brief この状態へ入る際の初期設定を適用する */
    void Enter(Player& player, bool, bool isMoving) const override {
        player.rig_->object->SetAnimation(isMoving ? player.rig_->runningJumpAnim : player.rig_->jumpAnim);
    }
};
/** @brief 水中移動のアニメーションを開始する状態 */
class Player::SwimAnimState : public IAnimState {
public:
    /** @brief この状態へ入る際の初期設定を適用する */
    void Enter(Player& player, bool, bool) const override {
        player.rig_->object->SetAnimation(player.rig_->swimAnim);
    }
};
/** @brief 攻撃制御側が設定したアニメーションを保つ状態 */
class Player::AttackAnimState : public IAnimState {
public:
    /** @brief この状態へ入る際の初期設定を適用する */
    void Enter(Player&, bool, bool) const override { }
};

/** @brief 待機アニメーションの共有インスタンスを返す */
const Player::IAnimState& Player::IdleAnim() {
    static const IdleAnimState kInstance;
    return kInstance;
}

/** @brief 走りアニメーションの共有インスタンスを返す */
const Player::IAnimState& Player::RunAnim() {
    static const RunAnimState kInstance;
    return kInstance;
}

/** @brief ジャンプアニメーションの共有インスタンスを返す */
const Player::IAnimState& Player::JumpAnim() {
    static const JumpAnimState kInstance;
    return kInstance;
}

/** @brief 泳ぎアニメーションの共有インスタンスを返す */
const Player::IAnimState& Player::SwimAnim() {
    static const SwimAnimState kInstance;
    return kInstance;
}

/** @brief 攻撃アニメーションの共有インスタンスを返す */
const Player::IAnimState& Player::AttackAnim() {
    static const AttackAnimState kInstance;
    return kInstance;
}

/** @brief 本体のボーンへ装備を付け直して各モデルの行列を更新する */
void Player::UpdateAttachedWeapons() {
    // 本体の更新を終えてから装備の位置と行列を確定する
    AttachActiveWeapons();
    weaponVisuals_.UpdateObjects();
}

/** @brief 見た目のトランスフォーム行列だけを再計算する（ゲームロジックは一切進めない） */
void Player::RefreshVisualTransforms() {
    // アニメ状態は一切変えず、現在のpos_を使って見た目のトランスフォームだけ再計算する
    // （StageEditorのギズモ等、Update()を通さずpos_だけ直接書き換えられた場合に追従させるため）
    Vector3 modelPos = { pos_.x, pos_.y + rig_->modelOffsetY + groundVisualCorrection_, pos_.z };
    modelPos = modelPos + previewOffset_; // 見本のプレビュー中だけずらす（ふだんは0）
    rig_->object->SetPosition(modelPos);

    // SkinnedObject3d::Update()はアニメ時刻も進めてしまうため、一時的に速度0にして完全静止させる
    float savedSpeed = rig_->object->GetAnimSpeed();
    rig_->object->SetAnimSpeed(0.0f);
    SyncMotionLayer();
    rig_->object->Update();
    rig_->object->SetAnimSpeed(savedSpeed);

    // 武器/銃は本体のボーンに追従しているため、本体の位置更新後に付け直さないと古い位置に取り残される
    UpdateAttachedWeapons();
}

/** @brief 演出用に、入力と物理を進めずに見た目だけを通常どおり進める（向き・待機/落下のアニメ・武器の付け方） */
void Player::UpdateCinematicPose(bool airborne, const Vector3& bodyRotation, bool moving) {
    // アニメの選び方は通常と同じ（宙なら落下、そうでなければ待機）。
    // 接地状態（物理の状態）は書き換えずに、宙にいるかだけを渡す
    UpdateAnimationState(moving, airborne);

    const float yaw = FacingYaw();
    const Vector3 modelPos = { pos_.x, pos_.y + rig_->modelOffsetY + groundVisualCorrection_, pos_.z };
    rig_->object->SetPosition(modelPos);
    rig_->object->SetRotation({ bodyRotation.x, yaw + bodyRotation.y, bodyRotation.z });
    SyncMotionLayer();
    rig_->object->Update(); // アニメの時刻も通常どおり進める

    // 手に持つ武器と銃をボーンへ付け直す（付けないと初期位置のまま本体の中に見える）
    UpdateAttachedWeapons();
}

/** @brief 入口演出用に本体の倍率と本体・装備の透明度を設定する */
void Player::SetPortalAppearance(float scale, float alpha) {
    portalAlpha_ = std::clamp(alpha, 0.0f, 1.0f);
    const float size = rig_->modelScale * scale;
    rig_->object->SetScale({ size, size, size });
    const Vector4 color = { 1.0f, 1.0f, 1.0f, portalAlpha_ };
    rig_->object->SetColor(color);
    weaponVisuals_.SetColor(color);
}

/** @brief やられ演出を始める（攻撃モーションを打ち切り、武器を手から放して構えないアニメにする。銃は隠す） */
void Player::BeginDefeatPose() {
    // 攻撃の途中でやられても、次の UpdateAnimationState で構えない待機か落下のアニメへ切り替わるようにする
    weaponsDropped_ = true;
    attackAnimTimer_ = 0.0f;
    rig_->object->SetAnimSpeed(1.0f);
}

/** @brief 手放した近接武器の位置とZ軸回りの回転を決める */
void Player::SetDroppedWeaponPose(const Vector3& position, float spinZ) {
    droppedWeaponPosition_ = position;
    droppedWeaponSpin_ = spinZ;
}

/** @brief 演出中のアニメの再生速度（1=通常、0で止める） */
void Player::SetCinematicAnimationSpeed(float speed) {
    rig_->object->SetAnimSpeed(speed);
}

/** @brief 演出の間だけ、加算モーションの代わりに掛けるポーズレイヤー（nullptr で加算モーションへ戻す） */
void Player::SetCinematicPoseLayer(const IPoseLayer* layer) {
    rig_->object->SetPoseLayer(layer != nullptr ? layer : &motionLayer_);
}

/** @brief 覚醒残像・アニメーション状態・プレイヤー色などUpdate()末尾の表示関連処理をまとめて更新する */
void Player::UpdateVisualState(Input* input) {
    float yaw = FacingYaw();

    // ── 覚醒残像スポーン＆フェード ──
    Vector3 modelPos = { pos_.x, pos_.y + rig_->modelOffsetY + groundVisualCorrection_, pos_.z };
    // バックステップは後ろへ反りながら小さく跳ねる弧で見せる（当たり判定の位置は動かさない）
    float backDodgeLean = 0.0f;
    if (dodge_.active && dodge_.backward) {
        const float arc = std::sin(std::clamp(dodge_.timer / Tuning().dodgeDuration, 0.0f, 1.0f) * gameConstants::kPi);
        backDodgeLean = Tuning().backDodgeLean * arc;
        modelPos.y += Tuning().backDodgeHopHeight * arc;
    }
    bool isRampage = IsRampaging();
    // 回避中も薄い残像を出して、瞬間的に位置が飛んだのではなく素早く動いたことを見せる
    // 固有技の連撃中も残像を引き、回転の軌跡を見せる
    afterImageRenderer_.Update(isRampage || dodge_.active || meleeCombo_.IsSequenceActive(), isRampage, modelPos,
        yaw + (gunCombo_.GetActive() != nullptr ? float{} : meleeCombo_.GetBodyTurnOffset()), spinAngle_);

    // ── アニメーション状態（接地中の左右移動入力で Idle/Run、空中で Jump）──
    bool isMovingHoriz = input->PushAction(GameAction::kMoveLeft)
        || input->PushAction(GameAction::kMoveRight) || strafeMoving_;
    UpdateAnimationState(isMovingHoriz, !onGround_);

    // ── プレイヤー色（上の行ほど優先。どれにも当たらなければ白）──
    const float rampageBlink = std::sin(rampageData_.juggleSlashCount * SceneVisualTuning::Player().rampageBlinkSpeed) * kHalfWave + kHalfWave;
    const float awakenPulse = std::sin(awaken_.timer * SceneVisualTuning::Player().awakenPulseSpeed) * SceneVisualTuning::Player().awakenPulseAmplitude + SceneVisualTuning::Player().awakenPulseBase;
    const BodyTint tints[] = {
        { finisher_.charging, kWhite }, // 色自体は白のまま、リムの発光だけで魅せる
        { IsRampaging(), { 1.0f, 1.0f - rampageBlink * SceneVisualTuning::Player().rampageGreenFade, 1.0f - rampageBlink * SceneVisualTuning::Player().rampageBlueFade, 1.0f } }, // 白→青白点滅
        { awaken_.isAwakened, { SceneVisualTuning::Player().awakenRed * awakenPulse, SceneVisualTuning::Player().awakenGreen * awakenPulse, 1.0f, 1.0f } }, // 青くパルス
    };
    Vector4 bodyColor = kWhite;
    for (const BodyTint& tint : tints) {
        if (tint.active) {
            bodyColor = tint.color;
            break;
        }
    }
    rig_->object->SetColor(bodyColor);

    // 溜めが深まるほどリムライトを強めて集中が高まる感じを出す（解放の瞬間が一番明るい）
    float rimIntensity = finisher_.charging
        ? kRimIntensity + (1.0f - finisher_.chargeTimer / CombatTune().FinisherChargeDuration()) * kFinisherRimBoost
        : kRimIntensity;
    rig_->object->SetRimIntensity(rimIntensity);
    weaponVisuals_.SetRimIntensity(rimIntensity);

    // 攻撃段ごとの体の傾き（前後=X/左右ロール=Z）。Ball旋回のspinAngle_とはZ軸を共有するが
    // 両者は排他（旋回はBall選択中のみ、攻撃レンはコンボ中のみ）なので単純加算でよい
    // 射撃の骨姿勢に近接の全身回転や傾きを重ねない
    Vector3 bodyLean = gunCombo_.GetActive() != nullptr ? Vector3{} : meleeCombo_.GetBodyLeanOffset();
    // フィニッシャーの溜め: 溜まるほど前へ倒し、力をこめて小刻みに震える
    if (finisher_.charging) {
        const float charge = FinisherChargeProgress();
        bodyLean.x += kFinisherChargeLean * easing::EaseOutCubic(charge)
            + std::sin(finisher_.chargeTimer * kFinisherTrembleSpeed) * kFinisherTrembleAmount * charge;
    }
    rig_->object->SetPosition(modelPos);
    // 回転斬りの段では体ごとその場で回る（向きに足すので、武器もボーン追従でいっしょに回る）
    rig_->object->SetRotation({ bodyLean.x + backDodgeLean, yaw + (gunCombo_.GetActive() != nullptr ? float{} : meleeCombo_.GetBodyTurnOffset()), spinAngle_ * gameConstants::kDegToRad + bodyLean.z });
    SyncMotionLayer();
    rig_->object->Update();
}

/** @brief 今の段と経過割合を加算モーションのレイヤーへ渡す（リグを評価する直前に呼ぶ） */
void Player::SyncMotionLayer() {
    // フィニッシャーの溜めの間は、地上の最後の段を振りかぶった所まで、溜めに合わせてゆっくり構える
    if (finisher_.charging) {
        SyncStanceLayer(true);
        const MeleeAttackDef* pose = FinisherChargePose();
        const ComboMotionLibrary* library = ComboMotionLibrary::GetInstance();
        const MotionClip* clip = (pose != nullptr && library->IsEnabled()) ? library->Find(rig_->motionRigId, pose->id) : nullptr;
        const float windup = (pose != nullptr && pose->duration > 0.0f) ? pose->hitTime / pose->duration * kFinisherWindupHold : 0.0f;
        motionLayer_.SetClip(clip, windup * easing::EaseOutCubic(FinisherChargeProgress()));
        return;
    }
    // コンボの段タイマーを唯一の時計にして、加算キーを段の経過割合で評価する
    // 同時入力では銃のクリップだけを選び、同じ骨への二重加算を防ぐ
    const GunShotDef* shot = gunCombo_.GetActive();
    const ComboMotionLibrary* gunLibrary = ComboMotionLibrary::GetInstance();
    SyncStanceLayer(shot != nullptr || meleeCombo_.GetActive() != nullptr);
    if (shot != nullptr) {
        motionLayer_.SetClip(gunLibrary->IsEnabled() ? gunLibrary->Find(rig_->motionRigId, shot->id) : nullptr,
            gunCombo_.GetMotionProgress());
        return;
    }
    const MeleeAttackDef* attack = meleeCombo_.GetActive();
    const ComboMotionLibrary* library = ComboMotionLibrary::GetInstance();
    const MotionClip* clip = (attack != nullptr && library->IsEnabled())
        ? library->Find(rig_->motionRigId, attack->id)
        : nullptr;
    motionLayer_.SetClip(clip, meleeCombo_.GetProgress());
}

/** @brief フィニッシャーの溜めで取る構え（持っている武器の地上の最後の段。武器が無ければ nullptr） */
const MeleeAttackDef* Player::FinisherChargePose() const {
    const auto* wm = WeaponManager::GetInstance();
    if (!wm->HasEquippedWeapon()) {
        return nullptr;
    }
    const MeleeComboSet& set = GetMeleeComboSet(wm->GetCurrent().type, wm->IsBossSlotSelected());
    return set.ground.count > 0 ? &set.ground[set.ground.count - 1] : nullptr;
}

/** @brief フィニッシャーの溜めの進み具合（0 で溜め始め、1 で溜めきった） */
float Player::FinisherChargeProgress() const {
    const float duration = CombatTune().FinisherChargeDuration();
    return duration > 0.0f ? std::clamp(1.0f - finisher_.chargeTimer / duration, 0.0f, 1.0f) : 1.0f;
}

/** @brief 持っている武器の構えを、技を振っていない間だけ重ねる（技の間は素早く薄める） */
void Player::SyncStanceLayer(bool attacking) {
    const auto* wm = WeaponManager::GetInstance();
    const ComboMotionLibrary* library = ComboMotionLibrary::GetInstance();
    // 武器を持ち、立ち回りの最中（技・回避・ワープ・水中・やられ中でない）だけ構える
    const bool canHoldStance = wm->HasEquippedWeapon() && library->IsEnabled() && !weaponsDropped_ && !inWater_ && !explorationMode_;
    const bool wantsStance = canHoldStance && !attacking && !dodge_.active && !warp_.active;
    const float rate = wantsStance ? kStanceFadeInPerSecond : kStanceFadeOutPerSecond;
    const float target = wantsStance ? 1.0f : 0.0f;
    const float step = rate * gameConstants::kFrameDeltaTime;
    stanceWeight_ = target > stanceWeight_ ? (std::min)(stanceWeight_ + step, target) : (std::max)(stanceWeight_ - step, target);
    // 武器を持っていなければ構えのクリップ自体を外す
    const MotionClip* stance = canHoldStance
        ? library->Find(rig_->motionRigId, std::string(kStanceClipPrefix) + WeaponTypeName(wm->GetCurrent().type))
        : nullptr;
    motionLayer_.SetStance(stance, stanceWeight_);
}

/** @brief 近接の段を指定時刻の姿勢で見せる（モーション編集のプレビュー用） */
void Player::PreviewMeleePose(const MeleeAttackDef* attack, float normalizedTime) {
    if (attack == nullptr) {
        EndMeleePreview();
        return;
    }
    // 射撃の見本から移る時は、射撃の姿勢を外す（残すと射撃の加算モーションが優先される）
    if (gunPreviewActive_) {
        gunCombo_.Reset();
        gunPreviewActive_ = false;
    }
    meleeCombo_.SetPreviewPose(attack, normalizedTime);

    // ベースアニメもゲーム中と同じ対応（段の経過秒 × 再生速度）で同じ時刻に合わせる
    const engine::graphics::Animation& anim = attack->slashAnim ? rig_->slashAnim : rig_->punchAnim;
    if (previewAnim_ != &anim) {
        rig_->object->SetAnimation(anim);
        previewAnim_ = &anim;
    }
    float animTime = std::clamp(normalizedTime, 0.0f, 1.0f) * attack->duration * attack->animSpeed;
    if (anim.duration > 0.0f) {
        animTime = std::fmod(animTime, anim.duration);
    }
    rig_->object->SetAnimTime(animTime);

    // 体の傾き・その場回転も同じ段時刻で評価する
    const float yaw = FacingYaw();
    const Vector3 bodyLean = meleeCombo_.GetBodyLeanOffset();
    rig_->object->SetRotation({ bodyLean.x, yaw + (gunCombo_.GetActive() != nullptr ? float{} : meleeCombo_.GetBodyTurnOffset()), bodyLean.z });
    RefreshVisualTransforms();
}

/** @brief 指定した武器を持たせて、近接の段を指定時刻の姿勢で見せる（コマンド表の見本用） */
void Player::PreviewMeleePose(const MeleeAttackDef* attack, float normalizedTime, WeaponType weapon) {
    if (attack == nullptr) {
        EndMeleePreview();
        return;
    }
    previewWeaponActive_ = true;
    previewWeapon_ = weapon;
    PreviewMeleePose(attack, normalizedTime);
}

/** @brief 射撃の段を指定時刻の姿勢で見せる（とどめの演出用） */
void Player::PreviewGunPose(const GunShotDef* shot, float normalizedTime) {
    if (shot == nullptr) {
        EndMeleePreview();
        return;
    }
    // 近接の見本から移る時は、持たせた武器と攻撃のアニメを外して待機の構えを土台にする
    previewWeaponActive_ = false;
    meleeCombo_.Reset();
    if (previewAnim_ != nullptr) {
        LeavePreviewAnimation();
    }
    gunCombo_.SetPreviewPose(shot, normalizedTime);
    gunPreviewActive_ = true;
    // 土台のアニメ・向き・加算モーション・銃の付け直しは、演出用の更新と同じ
    UpdateCinematicPose(false, { }, false);
}

/** @brief 近接と射撃のプレビューを終えて攻撃していない状態へ戻す */
void Player::EndMeleePreview() {
    previewWeaponActive_ = false;
    previewOffset_ = { };
    meleeCombo_.Reset();
    const bool gunPreview = gunPreviewActive_;
    if (gunPreview) {
        gunCombo_.Reset();
        gunPreviewActive_ = false;
    }
    if (previewAnim_ != nullptr || gunPreview) {
        LeavePreviewAnimation();
    }
    const float yaw = FacingYaw();
    rig_->object->SetRotation({ 0.0f, yaw, 0.0f });
    RefreshVisualTransforms();
}

/** @brief プレビューで差し替えた攻撃の姿勢から、今の接地・構えに合った移動系のアニメへ入り直す */
void Player::LeavePreviewAnimation() {
    // 攻撃の残り時間と速度を戻し、今の接地・構えの条件から移動系アニメへ入り直させる
    // （状態を攻撃扱いにしておくと、UpdateAnimationState()が同じ状態とみなして入り直しを省かない）
    attackAnimTimer_ = 0.0f;
    rig_->object->SetAnimSpeed(1.0f);
    animState_ = &AttackAnim();
    UpdateAnimationState(false, !onGround_);
    previewAnim_ = nullptr;
}

/** @brief 現在のスタイルに対応する近接武器と銃を、それぞれの手ボーンへ追従させる */
void Player::AttachActiveWeapons() {
    auto* wm = WeaponManager::GetInstance();
    WeaponAttachRequest request;
    request.body = rig_->object.get();
    request.meleeBoneName = rig_->meleeBoneName;
    request.gunBoneName = rig_->gunBoneName;
    // 見本のプレビュー中は、装備に関係なく見本の武器を持たせる
    request.hasMelee = previewWeaponActive_ || wm->HasEquippedWeapon();
    if (request.hasMelee) {
        request.meleeType = previewWeaponActive_ ? previewWeapon_ : wm->GetCurrent().type;
        request.meleeDisarmed = BehaviorFor(request.meleeType).IsDisarmed();
    }
    request.exploring = explorationMode_;
    request.dropped = weaponsDropped_;
    request.droppedPosition = droppedWeaponPosition_;
    request.droppedSpin = droppedWeaponSpin_;
    // 技の見本では装備に関係ない武器を持たせるので、合体の見た目は付けない
    request.fusionPartner = previewWeaponActive_ ? nullptr : wm->GetCurrentFusionPartner();
    request.gripOffset = heldGripOffset_;
    request.gunType = wm->GetRanged().type;
    request.gunPoseOffset = gunCombo_.GetPoseOffset();
    // 固有技で投げた武器は、飛んでいる間その位置へ動かす
    request.thrownPose = [this](WeaponType type, Vector3& position, float& spinZ) {
        return BehaviorFor(type).GetThrownPose(position, spinZ);
    };
    request.discardThrow = HasWeaponInFlight() ? &weaponThrow_ : nullptr;
    weaponVisuals_.Attach(request);
}


void Player::SetHeldWeaponWorldCorrection(const Matrix4x4& correction) {
    weaponVisuals_.SetCorrection(correction);
    UpdateAttachedWeapons();
}

/** @brief 現在表示中の近接武器の手元ワールド座標を返す（武器トレイル演出用） */
Vector3 Player::GetActiveWeaponWorldPosition() const {
    Vector3 position;
    if (weaponVisuals_.GetActiveMeleeWorldPosition(position)) {
        return position;
    }
    return { pos_.x, pos_.y + rig_->modelOffsetY, pos_.z };
}

/** @brief 手に持っている近接武器の種類と、武器モデル空間→ワールドの行列を返す（刃の軌跡演出用） */
bool Player::GetHeldWeaponTransform(WeaponType& outType, Matrix4x4& outMatrix) const {
    return weaponVisuals_.GetHeldTransform(outType, outMatrix);
}

/** @brief プレイヤーモデルを描画する */
void Player::Draw() {
    // 見た目を差し替えている間は、差し替えたモデルだけを足元に合わせて描く
    if (staticOverride_.IsActive()) {
        staticOverride_.Draw({ pos_.x, pos_.y - kBodyHalfExtent + groundVisualCorrection_, pos_.z }, FacingYaw());
        return;
    }
    // 本体を先に描き、半透明の残像は後から重ねる（残像も奥行きを書くので、先に描くと重なった所の本体が隠れて透明に見える）
    DrawBodyAndWeapons();
    afterImageRenderer_.Draw();
}

/** @brief 残像を除いて本体と持っている武器だけを描く（コマンド表の見本用） */
void Player::DrawBodyAndWeapons() {
    // 黒縁アウトライン（本体・武器とも先に描いてから通常描画で上書きする）
    auto* outline = OutlineEffect::GetInstance();
    Vector4 outlineColor = SceneVisualTuning::Player().outlineColor;
    outlineColor.w *= portalAlpha_;
    outline->SetColor(outlineColor);
    outline->SetWidth(SceneVisualTuning::Player().outlineWidth);
    // 銃のモデルは調整値で出すと決めた時だけ描く（両手持ちの構えでは左手が柄を握り、持たせると向きが狂うため）
    const bool drawGun = SceneVisualTuning::Player().showGunModel && !gunHidden_;
    {
        // スコープを抜けた瞬間に必ず通常描画用のPSOへ復帰する（早期returnや将来のコード追加があっても復帰忘れが起きない）
        PipelineStateGuard restoreGuard([this] {
            if (modelCommon_) {
                modelCommon_->CommonDrawSettings();
                // ルートシグネチャの切り替えでライト/シャドウマップの束縛が失われているため再バインドする
                Object3d::RebindCommonLighting(modelCommon_->GetDxCommon()->GetCommandList());
            }
        });
        outline->BeginOutlinePass();
        weaponVisuals_.DrawOutline(outline, drawGun);
        rig_->object->DrawOutline(outline);
    }

    // 通常描画
    weaponVisuals_.Draw(drawGun);
    rig_->object->Draw();
}

/** @brief 静的モデル（GetModel）を本体と同じ所に置く時の位置と倍率 */
void Player::GetStaticModelPlacement(Vector3& outPosition, float& outScale) const {
    outPosition = { pos_.x, pos_.y + rig_->modelOffsetY + groundVisualCorrection_, pos_.z };
    outScale = rig_->modelScale;
}

/** @brief 本体と持ち物の色とリムライトの色を決める（演出用の分身を黒く見せる時など。UpdateVisualState を通さない時だけ残る） */
void Player::SetAppearanceTint(const Vector4& color, const Vector3& rimColor) {
    rig_->object->SetColor(color);
    rig_->object->SetRimColor(rimColor);
    weaponVisuals_.SetMeleeRimColor(rimColor);
}

} // namespace engine::game

/**
 * @file PlayerCloneBody.cpp
 * @brief 分身の体（PlayerCloneBody）の実装
 */
#include "PlayerCloneBody.h"
#include "AllyBoss.h"
#include "CharacterVisuals.h"
#include "GameConstants.h"
#include "ModelCommon.h"
#include "ModelManager.h"
#include "Skeleton.h"
#include <string>

namespace engine::game {
using engine::graphics::Animation;
using engine::graphics::ModelManager;
using engine::graphics::SkinnedObject3d;

/** @brief モデル・アニメ・手に持つ武器を用意する */
void PlayerCloneBody::Initialize(engine::graphics::ModelCommon* modelCommon, const Vector3& position, WeaponType weapon) {
    // 見た目はふだんのプレイヤーと同じ定義を使う（覚醒の姿にはしない）
    const RigVisualDef& rig = kNormalRigVisual;
    position_ = position;
    skinCommon_ = SkinnedObject3d::CreateSharedCommon(modelCommon);
    object_ = std::make_unique<SkinnedObject3d>();
    object_->Initialize(skinCommon_.get());
    const std::string modelPath = std::string(rig.dir) + "/" + rig.file;
    object_->SetModel(ModelManager::GetInstance()->GetOrLoadSkinned(modelCommon->GetDxCommon(), modelPath, rig.texture));
    object_->SetSkeleton(engine::graphics::Skeleton::Create(engine::graphics::LoadNodeHierarchyFromFile(rig.dir, rig.file)));
    const float scale = rig.scale * AllyTune().cloneScale;
    object_->SetScale({ scale, scale, scale });
    object_->SetEnableLighting(true);

    // 武器を構える種類は、構えた待機と走りのアニメにする（プレイヤーと同じ）
    const bool holdPose = UsesHoldPose(weapon);
    idleAnimation_ = engine::graphics::LoadAnimationFile(rig.dir, rig.file, holdPose ? rig.idleHold : rig.idle);
    runAnimation_ = engine::graphics::LoadAnimationFile(rig.dir, rig.file, holdPose ? rig.runHold : rig.run);
    attackAnimation_ = engine::graphics::LoadAnimationFile(rig.dir, rig.file, rig.slash);
    // 体の高さはこちらが決めるので、どのアニメも体の上下の動きを固定する（走りと攻撃で体が下がって床に埋まらないように）
    LockClipVerticalRootMotion(idleAnimation_);
    LockClipVerticalRootMotion(runAnimation_);
    LockClipVerticalRootMotion(attackAnimation_);
    object_->SetAnimation(idleAnimation_);
    pose_ = Pose::kIdle;

    // 手に持つ武器（プレイヤーと同じ握り方で右手の骨に付ける）
    for (const HeldWeaponVisual& visual : kHeldWeaponVisuals) {
        if (visual.type != weapon) {
            continue;
        }
        weaponVisual_ = &visual;
        weaponObject_ = std::make_unique<engine::graphics::Object3d>();
        weaponObject_->Initialize(modelCommon);
        weaponObject_->SetModel(ModelManager::GetInstance()->GetOrLoad(modelCommon, visual.modelPath, visual.texturePath));
        weaponObject_->SetEnableLighting(true);
        break;
    }
    // 作った直後に一度行列を作っておく（作らないと画面の右上に白い四角が映る）
    ApplyTransforms();
}

/** @brief 体と武器に色を掛ける（分身は青く染める） */
void PlayerCloneBody::SetColor(const Vector4& color) {
    object_->SetColor(color);
    if (weaponObject_) {
        weaponObject_->SetColor(color);
    }
}

/** @brief 体と武器のふちを光らせる */
void PlayerCloneBody::SetRimGlow(const Vector3& color, float power, float intensity) {
    const bool enabled = intensity > 0.0f;
    // 体と手の武器を同じ光にする
    auto apply = [&](auto& object) {
        object.SetRimColor(color);
        object.SetRimPower(power);
        object.SetRimIntensity(intensity);
        object.SetEnableRim(enabled);
    };
    apply(*object_);
    if (weaponObject_) {
        apply(*weaponObject_);
    }
}

/** @brief 動いているかで、待機か走りのアニメにする（攻撃のアニメの途中でも切り替える） */
void PlayerCloneBody::PlayHeldAnimation(bool moving) {
    const Pose desired = moving ? Pose::kRun : Pose::kIdle;
    if (desired == pose_) {
        return;
    }
    object_->SetAnimation(moving ? runAnimation_ : idleAnimation_);
    pose_ = desired;
}

/** @brief 攻撃のアニメを頭から出す */
void PlayerCloneBody::PlayScriptedAttackAnimation() {
    object_->SetAnimation(attackAnimation_);
    pose_ = Pose::kAttack;
}

/** @brief 体の行列を作り直し、武器を手の骨に付け直す */
void PlayerCloneBody::ApplyTransforms() {
    // 分身の位置は床の上面から footOffset だけ上なので、モデルの足元はその分だけ下に置く
    const float facingYaw = facing_ >= 0.0f ? gameConstants::kHalfPi : -gameConstants::kHalfPi;
    object_->SetRotation({ 0.0f, facingYaw, 0.0f });
    object_->SetPosition({ position_.x, position_.y - AllyTune().footOffset, position_.z });
    object_->Update();
    if (weaponObject_ && weaponVisual_ != nullptr) {
        AttachToBone(weaponObject_.get(), object_->GetSkeleton(), object_->GetWorldMatrix(), kNormalRigVisual.meleeBone,
            weaponVisual_->gripScale, weaponVisual_->gripRotate, weaponVisual_->gripTranslate);
    }
}

/** @brief アニメを1フレーム進め、位置と向きを見た目へ反映する */
void PlayerCloneBody::Update() {
    ApplyTransforms();
}

/** @brief アニメを進めずに、位置と向きだけを見た目へ反映する */
void PlayerCloneBody::RefreshVisualTransforms() {
    // アニメを止めたまま行列だけを作り直す（カメラが動いても画面に張り付かないように）
    const float animSpeed = object_->GetAnimSpeed();
    object_->SetAnimSpeed(0.0f);
    ApplyTransforms();
    object_->SetAnimSpeed(animSpeed);
}

/** @brief 体と武器を描く（見せない間は描かない） */
void PlayerCloneBody::Draw() {
    if (!visible_) {
        return;
    }
    object_->Draw();
    if (weaponObject_) {
        weaponObject_->Draw();
    }
}

} // namespace engine::game

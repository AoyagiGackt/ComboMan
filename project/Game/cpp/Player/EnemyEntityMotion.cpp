/**
 * @file EnemyEntityMotion.cpp
 * @brief ボスの技の動き（BossMotionFrame）を敵の体と手元の武器へ掛ける
 */
#include "EnemyEntity.h"
#include "GameConstants.h"
#include "Matrix4x4.h"
#include "Skeleton.h"
#include <map>

namespace engine::game {
namespace {
constexpr const char* kKnightDirectory = "Resources/Knight/glTF";
constexpr const char* kKnightFile = "KnightCharacter.gltf";
/** @brief 騎士の模型に入っているアニメの名前（ほかの名前は読み込まない。読み込みで先頭のアニメに化けないように） */
constexpr const char* kKnightAnimationNames[] = {
    "Death", "Idle", "Idle_swordLeft", "Idle_swordRight", "Jump", "Roll", "Roll_sword",
    "Run", "Run_swordAttack", "Run_swordRight", "swordAttackJump", "Walking",
};
constexpr int kTranslationRow = 3; ///< 行列の平行移動が入っている行
} // namespace

/** @brief 今のフレームに技の動きを掛けられるか（倒れた・打ち上げられた・モンスターの見た目・ふだんの近接の振りの最中は掛けない） */
bool EnemyEntity::CanApplyMotion() const {
    const bool ownSwing = !scripted_ && attackState_ != nullptr && !attackState_->IsIdle();
    return motionActive_ && !defeated_ && !isLaunched_ && homeRunSpinTimer_ <= 0.0f && !monsterObject_ && !ownSwing;
}

/** @brief 動きの側が下のアニメを決めているか */
bool EnemyEntity::MotionOwnsAnimation() const {
    return CanApplyMotion() && !motionFrame_.anim.empty();
}

/** @brief 騎士の模型のアニメを名前で探す */
const engine::graphics::Animation* EnemyEntity::FindKnightAnimation(const std::string& name) {
    static std::map<std::string, engine::graphics::Animation> sAnimations;
    const auto cached = sAnimations.find(name);
    if (cached != sAnimations.end()) {
        return &cached->second;
    }
    for (const char* known : kKnightAnimationNames) {
        if (name == known) {
            const auto inserted = sAnimations.emplace(name, engine::graphics::LoadAnimationFile(kKnightDirectory, kKnightFile, name));
            return &inserted.first->second;
        }
    }
    return nullptr;
}

/** @brief 技の動きで使う騎士のアニメを全部読み込んでおく（戦闘の途中で初めて読むと、素材の記録に載らず pak だけの配布版で読めない） */
void EnemyEntity::PreloadKnightAnimations() {
    for (const char* name : kKnightAnimationNames) {
        FindKnightAnimation(name);
    }
}

/** @brief 技の動きを掛けるのをやめた最初のフレームに、骨の曲げとアニメの速さを戻す */
void EnemyEntity::EndMotion() {
    if (!motionApplied_) {
        return;
    }
    motionApplied_ = false;
    // 膝をつく姿勢を掛け始めていたら、そちらのレイヤーと速さを残す
    if (!kneelPoseApplied_) {
        object_->SetPoseLayer(nullptr);
    }
    if (!defeated_) {
        object_->SetAnimSpeed(1.0f);
    }
    // 差し替えていたアニメは、次のふだんの更新で掛け直してもらう
    if (!motionAnimName_.empty()) {
        motionAnimName_.clear();
        motionAnimOverride_ = true;
    }
}

/** @brief 技の動きの体の部分を掛ける */
bool EnemyEntity::ApplyMotionBody() {
    if (!CanApplyMotion()) {
        EndMotion();
        return false;
    }
    const BossPose& pose = motionFrame_.pose;
    // 体の向き・傾き: ふだんの傾き（のけぞり等）に重ねる。前へ倒すのは向いている側へ倒すこと
    const float facingYaw = facingSign_ >= 0.0f ? gameConstants::kHalfPi : -gameConstants::kHalfPi;
    const Vector3 rotation = object_->GetRotation();
    object_->SetRotation({ rotation.x, facingYaw + facingSign_ * pose.spin, rotation.z - facingSign_ * pose.lean });
    Vector3 visualPos = VisualPosition();
    visualPos.y += pose.lift;
    object_->SetPosition(visualPos);
    // 骨の向き
    std::vector<ReachPoseLayer::Reach> reaches;
    reaches.reserve(pose.reaches.size());
    for (const BossMotionReach& reach : pose.reaches) {
        reaches.push_back({ reach.bone, reach.direction, reach.weight });
    }
    motionLayer_.SetReaches(reaches);
    object_->SetPoseLayer(&motionLayer_);
    // 下に流すアニメ（名前が変わった時だけ掛け直す。毎フレーム掛けると最初の姿勢で止まる）
    if (!motionFrame_.anim.empty()) {
        if (motionFrame_.anim != motionAnimName_) {
            const engine::graphics::Animation* animation = FindKnightAnimation(motionFrame_.anim);
            if (animation != nullptr) {
                object_->SetAnimation(*animation);
            }
            motionAnimName_ = motionFrame_.anim;
            motionAnimOverride_ = true;
        }
        object_->SetAnimSpeed(motionFrame_.animSpeed);
    } else if (!motionAnimName_.empty()) {
        // 動きの側がアニメを決めなくなったら、ふだんのアニメへ戻してもらう
        motionAnimName_.clear();
        object_->SetAnimSpeed(1.0f);
    }
    motionApplied_ = true;
    return true;
}

/** @brief 技の動きで武器を手元へ付ける */
void EnemyEntity::PlaceMotionWeapon() {
    if (!motionApplied_ || weaponDropped_) {
        return;
    }
    const BossPose& pose = motionFrame_.pose;
    Vector3 position = {};
    const auto hand = object_->GetSkeleton().FindJointIndex(motionFrame_.handBone);
    if (motionFrame_.weaponOnHand && hand.has_value()) {
        // 手の骨のワールドの位置（骨の行列 × 体の行列 の平行移動）
        const Matrix4x4 world = Multiply(object_->GetSkeleton().GetJoint(*hand).skeletonSpaceMatrix, object_->GetWorldMatrix());
        position = { world.m[kTranslationRow][0], world.m[kTranslationRow][1], world.m[kTranslationRow][2] };
    } else {
        const Vector3 body = VisualPosition();
        position = { body.x + facingSign_ * kWeaponOffsetX, body.y + kWeaponOffsetY + pose.lift, body.z + kWeaponOffsetZ };
    }
    // 手元からのずれは、前を向いている側を +x として足す
    position.x += facingSign_ * pose.weaponOffset.x;
    position.y += pose.weaponOffset.y;
    position.z += pose.weaponOffset.z;
    const float facingYaw = facingSign_ >= 0.0f ? gameConstants::kHalfPi : -gameConstants::kHalfPi;
    weaponObject_->SetPosition(position);
    weaponObject_->SetRotation({ 0.0f, facingYaw + facingSign_ * pose.spin, -facingSign_ * pose.weaponAngle });
}

} // namespace engine::game

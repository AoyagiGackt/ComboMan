/**
 * @file PlayerCloneBody.h
 * @brief 分身の体（プレイヤーと同じモデルを青く染め、ボスの武器を手に持たせる。動きは AllyBoss が台本で決める）
 */
#pragma once
#include "Animation.h"
#include "Object3d.h"
#include "SkinCommon.h"
#include "SkinnedObject3d.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Weapon.h"
#include <memory>

namespace engine::graphics {
class ModelCommon;
}

namespace engine::game {
struct HeldWeaponVisual;

/**
 * @brief 分身の体（プレイヤーの見た目の写し）
 * @note 見た目の定義（モデル・アニメの名前・武器の握り方）は CharacterVisuals.h のプレイヤーと同じものを使う。
 *       AI も物理も持たず、置く・向き・アニメの切り替え・描画だけを受け持つ
 */
class PlayerCloneBody {
public:
    /**
     * @brief モデル・アニメ・手に持つ武器を用意する
     * @param modelCommon モデルの共通設定
     * @param position 最初の位置（分身の位置。足元は footOffset だけ下）
     * @param weapon 手に持たせる武器の種類
     */
    void Initialize(engine::graphics::ModelCommon* modelCommon, const Vector3& position, WeaponType weapon);

    /** @brief 見せるか */
    void SetVisible(bool visible) {
        visible_ = visible;
    }
    /** @brief 体と武器に色を掛ける（分身は青く染める） */
    void SetColor(const Vector4& color);
    /**
     * @brief 体と武器のふちを光らせる
     * @param color 光の色
     * @param power 鋭さ（大きいほど細いふち）
     * @param intensity 強さ（0 以下なら光らせない）
     */
    void SetRimGlow(const Vector3& color, float power, float intensity);
    /** @brief 位置を決める（分身の位置。足元は footOffset だけ下） */
    void SetPosition(const Vector3& position) {
        position_ = position;
    }
    /** @brief 位置を返す */
    const Vector3& GetPosition() const {
        return position_;
    }
    /** @brief 向きを決める（右=+1、左=-1） */
    void SetScriptedFacing(float sign) {
        facing_ = sign >= 0.0f ? 1.0f : -1.0f;
    }
    /** @brief 動いているかで、待機か走りのアニメにする（攻撃のアニメの途中でも切り替える） */
    void PlayHeldAnimation(bool moving);
    /** @brief 攻撃のアニメを頭から出す */
    void PlayScriptedAttackAnimation();
    /** @brief アニメを1フレーム進め、位置と向きを見た目へ反映する */
    void Update();
    /** @brief アニメを進めずに、位置と向きだけを見た目へ反映する */
    void RefreshVisualTransforms();
    /** @brief 体と武器を描く（見せない間は描かない） */
    void Draw();

private:
    /** @brief 今のアニメの種類 */
    enum class Pose {
        kIdle, ///< 待機
        kRun, ///< 走り
        kAttack, ///< 攻撃
    };
    /** @brief 体の行列を作り直し、武器を手の骨に付け直す */
    void ApplyTransforms();

    std::unique_ptr<engine::graphics::SkinCommon> skinCommon_;
    std::unique_ptr<engine::graphics::SkinnedObject3d> object_; ///< 体
    std::unique_ptr<engine::graphics::Object3d> weaponObject_; ///< 手に持つ武器（その武器の見た目が無ければ持たない）
    const HeldWeaponVisual* weaponVisual_ = nullptr; ///< 手に持つ武器の握り方
    engine::graphics::Animation idleAnimation_;
    engine::graphics::Animation runAnimation_;
    engine::graphics::Animation attackAnimation_;
    Pose pose_ = Pose::kIdle;
    Vector3 position_ {};
    float facing_ = 1.0f; ///< 向き（右=+1、左=-1）
    bool visible_ = false;
};

} // namespace engine::game

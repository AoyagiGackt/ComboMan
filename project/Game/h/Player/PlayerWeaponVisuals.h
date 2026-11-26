/**
 * @file PlayerWeaponVisuals.h
 * @brief プレイヤーが手に持つ近接武器・合体の素材・銃の見た目（モデルの用意・手の骨への付け方・描画）を定義するファイル
 */
#pragma once
#include "Matrix4x4.h"
#include "Object3d.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Weapon.h"
#include <functional>
#include <memory>
#include <vector>

namespace engine::graphics {
class Model;
class ModelCommon;
class OutlineEffect;
class SkinnedObject3d;
}

namespace engine::game {
namespace playerTypes { struct WeaponThrow; }

/**
 * @brief 1フレームぶんの武器の付け方（何を持たせ、どの骨に付けるか）
 * @note 何を持たせるかはプレイヤーの状態で決まるので、プレイヤーが毎フレーム組み立てて渡す
 */
struct WeaponAttachRequest {
    const engine::graphics::SkinnedObject3d* body = nullptr; ///< 武器を付ける体（骨とワールド行列を使う）
    const char* meleeBoneName = ""; ///< 近接武器を付ける骨
    const char* gunBoneName = ""; ///< 銃を付ける骨
    bool hasMelee = false; ///< 近接武器を持たせるか
    WeaponType meleeType {}; ///< 持たせる近接武器
    bool meleeDisarmed = false; ///< 持たせる近接武器が手元を離れているか（投げている間など）
    bool exploring = false; ///< 戦わずに歩くだけの状態か（武器も銃も持たない）
    bool dropped = false; ///< やられ演出で武器を手放したか
    Vector3 droppedPosition {}; ///< 手放した近接武器の位置
    float droppedSpin = 0.0f; ///< 手放した近接武器のZ軸回りの回転
    const WeaponData* fusionPartner = nullptr; ///< 合体の素材の武器（付けないなら nullptr）
    Vector3 gripOffset {}; ///< 近接武器の握りの回転に足す角度
    GunType gunType {}; ///< 持たせる銃
    Vector3 gunPoseOffset {}; ///< 射撃の構えで銃の握りの回転に足す角度
    /** @brief その種類の武器が手元を離れて飛んでいれば、位置とZ軸回りの回転を書いて true を返す */
    std::function<bool(WeaponType, Vector3&, float&)> thrownPose;
    const playerTypes::WeaponThrow* discardThrow = nullptr; ///< 投げて手放した1本
};

/**
 * @brief プレイヤーの手に持つ武器の見た目
 * @note 武器の種類が多いので、種類ごとのモデルを表（CharacterVisuals.h の kHeldWeaponVisuals / kGunVisuals）から全部作っておき、
 *       今持っている1つだけを手の骨へ付けて描く。合体の素材は土台と同じ種類でも描けるよう別に持つ。
 */
class PlayerWeaponVisuals {
public:
    /**
     * @brief 表のすべての武器と銃のモデルを用意する（ゲーム中に作ったり消したりしない）
     * @param modelCommon モデル共通処理
     * @param rimColor リムライトの色
     * @param rimPower リムライトの鋭さ
     * @param rimIntensity リムライトの強さ
     */
    void Initialize(engine::graphics::ModelCommon* modelCommon, const Vector3& rimColor, float rimPower, float rimIntensity);
    /**
     * @brief 持たせる武器と銃を選び、手の骨へ付ける
     * @param request このフレームの付け方
     */
    void Attach(const WeaponAttachRequest& request);
    /** @brief 近接武器と銃の行列を更新する（Attach の後に呼ぶ） */
    void UpdateObjects();

    /** @brief 近接武器と銃の色を決める */
    void SetColor(const Vector4& color);
    /** @brief 近接武器と銃のリムライトの強さを決める */
    void SetRimIntensity(float intensity);
    /** @brief 近接武器のリムライトの色を決める */
    void SetMeleeRimColor(const Vector3& rimColor);
    /**
     * @brief 手に付けた近接武器のワールド行列に掛ける補正を決める（次の Attach から効く）
     * @param correction 右から掛ける補正の行列
     */
    void SetCorrection(const Matrix4x4& correction);
    /** @brief 手に持つ近接武器の補正をなくす（次の Attach から効く） */
    void ClearCorrection() {
        hasCorrection_ = false;
    }

    /**
     * @brief 黒縁を描く（本体より先に呼ぶ）
     * @param outline 黒縁の描画
     * @param drawGun 銃も描くか
     */
    void DrawOutline(engine::graphics::OutlineEffect* outline, bool drawGun);
    /**
     * @brief 持っている武器を描く
     * @param drawGun 銃も描くか
     */
    void Draw(bool drawGun);

    /** @brief 付ける骨が見つかり、持たせる銃が決まっているか */
    bool HasVisibleGun() const {
        return gunVisible_ && activeGunIndex_ >= 0;
    }
    /**
     * @brief 今持っている近接武器の手元のワールド座標
     * @param outPosition 手元の座標（持っている時だけ書く）
     * @return 近接武器を持っていれば true
     */
    bool GetActiveMeleeWorldPosition(Vector3& outPosition) const;
    /**
     * @brief 手に持っている近接武器の種類と、武器のモデル空間からワールドへの行列
     * @return 手の骨に付いている時だけ true（投げている間は false）
     */
    bool GetHeldTransform(WeaponType& outType, Matrix4x4& outMatrix) const;

private:
    /** @brief 手に持たせる武器1種ぶんのモデルと、握らせる時のローカルの大きさ・回転・位置 */
    template <class Type>
    struct Slot {
        Type type {};
        engine::graphics::Model* model = nullptr; ///< 実体は ModelManager が所有・共有する
        std::unique_ptr<engine::graphics::Object3d> object;
        Vector3 gripScale {};
        Vector3 gripRotate {};
        Vector3 gripTranslate {};
    };
    using MeleeSlot = Slot<WeaponType>;
    using GunSlot = Slot<GunType>;

    /** @brief 表の1行ずつからスロットを作る */
    template <class SlotType, class Table>
    void InitializeSlots(std::vector<SlotType>& slots, const Table& table, engine::graphics::ModelCommon* modelCommon,
        const Vector3& rimColor, float rimPower, float rimIntensity);
    /** @brief 骨にオブジェクトを付ける（握りのローカル行列は呼び出し側で決める） */
    void AttachToHand(engine::graphics::Object3d* object, const WeaponAttachRequest& request, const char* boneName,
        const Vector3& gripScale, const Vector3& gripRotate, const Vector3& gripTranslate) const;
    /** @brief 手元を離れて飛んでいる武器を、手の骨から外して位置・回転を直接動かす */
    void PlaceThrown(MeleeSlot& slot, const WeaponAttachRequest& request, const Vector3& position, float spinZ, float scaleMult = 1.0f) const;
    /** @brief 合体していれば、素材の武器を土台の柄頭から逆向きに付ける */
    void AttachFusedPartner(const WeaponAttachRequest& request);
    /** @brief 手に付けた近接武器へ、決めてある補正を掛ける（補正が無ければ何もしない） */
    void ApplyCorrection(engine::graphics::Object3d& object) const;
    /** @brief 近接武器の実体すべて（合体の素材の分も含む）に fn を呼ぶ */
    template <class Fn>
    void ForEachMeleeObject(Fn&& fn) {
        for (auto& slot : melee_) {
            fn(*slot.object);
        }
        for (auto& slot : fusedPartners_) {
            fn(*slot.object);
        }
        if (discardSlot_.object) {
            fn(*discardSlot_.object);
        }
    }
    /** @brief 今描く近接武器（持っている・合体の素材・飛んでいる）に fn を呼ぶ */
    template <class Fn>
    void ForEachDrawnMelee(Fn&& fn) {
        if (activeMeleeIndex_ >= 0) {
            fn(*melee_[activeMeleeIndex_].object);
        }
        if (fusedPartnerIndex_ >= 0) {
            fn(*fusedPartners_[fusedPartnerIndex_].object);
        }
        if (thrownIndex_ >= 0 && thrownIndex_ != activeMeleeIndex_) {
            fn(*melee_[thrownIndex_].object);
        }
        if (discardVisible_) {
            fn(*discardSlot_.object);
        }
    }

    std::vector<MeleeSlot> melee_; ///< 右手に持たせる近接武器（種類ごとに1つ）
    MeleeSlot discardSlot_; ///< 手元に同種の武器が残っても独立して描く投擲用の実体
    bool discardVisible_ = false;
    std::vector<MeleeSlot> fusedPartners_; ///< 合体の素材の武器（土台と同じ種類でも描けるよう別に持つ）
    std::vector<GunSlot> guns_; ///< 左手に持たせる銃（種類ごとに1つ）
    int activeMeleeIndex_ = -1; ///< 表示中の melee_ の番号（-1=持っていない）
    int thrownIndex_ = -1; ///< 手元を離れて飛んでいる melee_ の番号（持ち替えた後も描き続ける。無ければ-1）
    int fusedPartnerIndex_ = -1; ///< 表示中の fusedPartners_ の番号（-1=合体していない）
    int activeGunIndex_ = -1; ///< 表示中の guns_ の番号（-1=持っていない）
    bool gunVisible_ = true; ///< 銃を付ける骨が見つかったか
    bool hasCorrection_ = false; ///< 手に持つ近接武器に補正を掛けるか
    Matrix4x4 correction_ {}; ///< 手に持つ近接武器のワールド行列に掛ける補正
};

} // namespace engine::game

/**
 * @file PlayerWeaponVisuals.cpp
 * @brief プレイヤーが手に持つ近接武器・合体の素材・銃の見た目を実装するファイル
 */
#include "PlayerWeaponVisuals.h"
#include "EngineContext.h"
#include "PlayerTypes.h"
#include "CharacterVisuals.h"
#include "GameConstants.h"
#include "ModelManager.h"
#include "OutlineEffect.h"
#include "SceneVisualTuning.h"
#include "SkinnedObject3d.h"

namespace engine::game {

using namespace engine::graphics;

/** @brief 表の1行ずつから、握りの姿勢と描画の実体を持つスロットを作る */
template <class SlotType, class Table>
void PlayerWeaponVisuals::InitializeSlots(std::vector<SlotType>& slots, const Table& table, ModelCommon* modelCommon,
    const Vector3& rimColor, float rimPower, float rimIntensity) {
    slots.clear();
    for (const auto& asset : table) {
        SlotType slot;
        slot.type = asset.type;
        slot.gripScale = asset.gripScale;
        slot.gripRotate = asset.gripRotate;
        slot.gripTranslate = asset.gripTranslate;
        // モデルを読み込み、本体と同じリムライトを付ける
        slot.model = engine::Services().models->GetOrLoad(modelCommon, asset.modelPath, asset.texturePath);
        slot.object = std::make_unique<Object3d>();
        slot.object->Initialize(modelCommon);
        slot.object->SetModel(slot.model);
        slot.object->SetEnableLighting(true);
        slot.object->SetRimColor(rimColor);
        slot.object->SetRimPower(rimPower);
        slot.object->SetRimIntensity(rimIntensity);
        slot.object->SetEnableRim(true);
        slots.push_back(std::move(slot));
    }
}

/** @brief 右手の近接武器・合体の素材・左手の銃を同じ手順で用意する（どれを出すかは Attach が決める） */
void PlayerWeaponVisuals::Initialize(ModelCommon* modelCommon, const Vector3& rimColor, float rimPower, float rimIntensity) {
    InitializeSlots(melee_, kHeldWeaponVisuals, modelCommon, rimColor, rimPower, rimIntensity);
    InitializeSlots(fusedPartners_, kHeldWeaponVisuals, modelCommon, rimColor, rimPower, rimIntensity);
    InitializeSlots(guns_, kGunVisuals, modelCommon, rimColor, rimPower, rimIntensity);
    discardSlot_.object = std::make_unique<Object3d>();
    discardSlot_.object->Initialize(modelCommon);
    discardSlot_.object->SetEnableLighting(true);
    discardSlot_.object->SetEnableRim(true);
    discardSlot_.object->SetRimColor(rimColor);
    discardSlot_.object->SetRimPower(rimPower);
    discardSlot_.object->SetRimIntensity(rimIntensity);
}

/** @brief 持たせる武器と銃を選び、手の骨へ付ける */
void PlayerWeaponVisuals::Attach(const WeaponAttachRequest& request) {
    discardVisible_ = false;
    if (request.body == nullptr) {
        return;
    }
    // ── 持たせる近接武器を選ぶ ──
    // 刃の向きは段ごとの加算モーションが手の骨を回して付けるので、ここでは握りの回転を足さない
    activeMeleeIndex_ = -1;
    fusedPartnerIndex_ = -1;
    if (request.hasMelee) {
        for (int i = 0; i < static_cast<int>(melee_.size()); ++i) {
            if (melee_[i].type == request.meleeType) {
                activeMeleeIndex_ = i;
                break;
            }
        }
    }
    // 歩くだけの状態では、近接武器も銃も持たない
    if (request.exploring) {
        activeMeleeIndex_ = -1;
        thrownIndex_ = -1;
        gunVisible_ = false;
        return;
    }
    // やられ演出で手放した後は、近接武器を演出が決めた位置へ置き、銃は隠す
    if (request.dropped) {
        if (activeMeleeIndex_ >= 0) {
            PlaceThrown(melee_[activeMeleeIndex_], request, request.droppedPosition, request.droppedSpin);
        }
        thrownIndex_ = -1;
        gunVisible_ = false;
        return;
    }
    // 手元にあれば右手の骨へ付け、合体していれば素材も付ける
    if (const auto* flight = request.discardThrow) {
        constexpr float kDiscardScale = 1.25f;
        for (const auto& slot : melee_) {
            if (slot.type == flight->type) {
                discardSlot_.object->SetModel(slot.model);
                discardSlot_.gripScale = slot.gripScale;
                discardSlot_.gripRotate = slot.gripRotate;
                PlaceThrown(discardSlot_, request, flight->position, flight->spin, kDiscardScale);
                discardVisible_ = true;
                break;
            }
        }
    }
    if (activeMeleeIndex_ >= 0 && !request.meleeDisarmed) {
        MeleeSlot& slot = melee_[activeMeleeIndex_];
        AttachToHand(slot.object.get(), request, request.meleeBoneName, slot.gripScale, slot.gripRotate + request.gripOffset, slot.gripTranslate);
        ApplyCorrection(*slot.object);
        AttachFusedPartner(request);
    }

    // 投げた武器は、途中で他の武器に持ち替えても持っている武器とは別に
    // 飛行や渦の位置へ動かし続ける（持ち替え直後に消えて見えないようにする）
    thrownIndex_ = -1;
    if (request.thrownPose) {
        for (int i = 0; i < static_cast<int>(melee_.size()); ++i) {
            Vector3 thrownPos;
            float thrownSpinZ = 0.0f;
            if (request.thrownPose(melee_[i].type, thrownPos, thrownSpinZ)) {
                thrownIndex_ = i;
                PlaceThrown(melee_[i], request, thrownPos, thrownSpinZ);
                // 今の武器を投げている間は手元に何も無いので、素材の武器も出さない
                if (i == activeMeleeIndex_) {
                    fusedPartnerIndex_ = -1;
                }
                break;
            }
        }
    }

    // ── 選んでいる銃を左手の骨へ付ける ──
    // 射撃コンボ中は段ごとの構えと反動の回転を握りの回転へ足し、同じ骨でも銃ごとに違う撃ち方に見せる
    activeGunIndex_ = -1;
    for (int i = 0; i < static_cast<int>(guns_.size()); ++i) {
        if (guns_[i].type == request.gunType) {
            activeGunIndex_ = i;
            break;
        }
    }
    gunVisible_ = request.body->GetSkeleton().HasJoint(request.gunBoneName);
    if (gunVisible_ && activeGunIndex_ >= 0) {
        GunSlot& slot = guns_[activeGunIndex_];
        AttachToHand(slot.object.get(), request, request.gunBoneName, slot.gripScale, slot.gripRotate + request.gunPoseOffset, slot.gripTranslate);
    }
}

/** @brief 近接武器と銃の行列を更新する */
void PlayerWeaponVisuals::UpdateObjects() {
    ForEachMeleeObject([](Object3d& object) {
        object.Update();
    });
    for (auto& slot : guns_) {
        slot.object->Update();
    }
}

/** @brief 近接武器と銃の色を決める */
void PlayerWeaponVisuals::SetColor(const Vector4& color) {
    ForEachMeleeObject([&color](Object3d& object) {
        object.SetColor(color);
    });
    for (auto& slot : guns_) {
        slot.object->SetColor(color);
    }
}

/** @brief 近接武器と銃のリムライトの強さを決める */
void PlayerWeaponVisuals::SetRimIntensity(float intensity) {
    ForEachMeleeObject([intensity](Object3d& object) {
        object.SetRimIntensity(intensity);
    });
    for (auto& slot : guns_) {
        slot.object->SetRimIntensity(intensity);
    }
}

/** @brief 近接武器のリムライトの色を決める */
void PlayerWeaponVisuals::SetMeleeRimColor(const Vector3& rimColor) {
    ForEachMeleeObject([&rimColor](Object3d& object) {
        object.SetRimColor(rimColor);
    });
}

/** @brief 手に付けた近接武器のワールド行列に掛ける補正を決める */
void PlayerWeaponVisuals::SetCorrection(const Matrix4x4& correction) {
    correction_ = correction;
    hasCorrection_ = true;
}

/** @brief 描く近接武器と銃の黒縁を描く */
void PlayerWeaponVisuals::DrawOutline(OutlineEffect* outline, bool drawGun) {
    ForEachDrawnMelee([outline](Object3d& object) {
        object.DrawOutline(outline);
    });
    if (drawGun && HasVisibleGun()) {
        guns_[activeGunIndex_].object->DrawOutline(outline);
    }
}

/** @brief 描く近接武器と銃を描く */
void PlayerWeaponVisuals::Draw(bool drawGun) {
    ForEachDrawnMelee([](Object3d& object) {
        object.Draw();
    });
    if (drawGun && HasVisibleGun()) {
        guns_[activeGunIndex_].object->Draw();
    }
}

/** @brief 今持っている近接武器の手元のワールド座標 */
bool PlayerWeaponVisuals::GetActiveMeleeWorldPosition(Vector3& outPosition) const {
    if (activeMeleeIndex_ < 0) {
        return false;
    }
    outPosition = melee_[activeMeleeIndex_].object->GetWorldPosition();
    return true;
}

/** @brief 手に持っている近接武器の種類と、武器のモデル空間からワールドへの行列 */
bool PlayerWeaponVisuals::GetHeldTransform(WeaponType& outType, Matrix4x4& outMatrix) const {
    if (activeMeleeIndex_ < 0 || activeMeleeIndex_ == thrownIndex_) {
        return false;
    }
    const MeleeSlot& slot = melee_[activeMeleeIndex_];
    // 手の骨に付いている間は SetLocalMatrix() の行列がそのままワールド行列になっている
    if (!slot.object->IsUsingLocalMatrix()) {
        return false;
    }
    outType = slot.type;
    outMatrix = slot.object->GetLocalMatrix();
    return true;
}

/** @brief 骨にオブジェクトを付ける */
void PlayerWeaponVisuals::AttachToHand(Object3d* object, const WeaponAttachRequest& request, const char* boneName,
    const Vector3& gripScale, const Vector3& gripRotate, const Vector3& gripTranslate) const {
    AttachToBone(object, request.body->GetSkeleton(), request.body->GetWorldMatrix(), boneName, gripScale, gripRotate, gripTranslate);
}

/** @brief 手元を離れて飛んでいる武器を、手の骨から外して位置・回転を直接動かす */
void PlayerWeaponVisuals::PlaceThrown(MeleeSlot& slot, const WeaponAttachRequest& request, const Vector3& position, float spinZ, float scaleMult) const {
    slot.object->ClearLocalMatrix(); // 手の骨への追従（SetLocalMatrix）を外し、直接の位置・回転に戻す
    slot.object->SetPosition(position);
    slot.object->SetRotation({ slot.gripRotate.x, slot.gripRotate.y, spinZ });
    // 握りの大きさは手の骨とキャラの縮尺に掛ける前提の値なので、手を離した時も同じ縮尺を掛けて持っていた時の大きさにそろえる
    const float handScale = scaleMult * BoneWorldScale(request.body->GetSkeleton(), request.body->GetWorldMatrix(), request.meleeBoneName);
    slot.object->SetScale({ slot.gripScale.x * handScale, slot.gripScale.y * handScale, slot.gripScale.z * handScale });
    slot.object->Update();
}

/** @brief 合体していれば、素材の武器を土台の柄頭から逆向きに付ける */
void PlayerWeaponVisuals::AttachFusedPartner(const WeaponAttachRequest& request) {
    const WeaponData* partner = request.fusionPartner;
    if (partner == nullptr) {
        return;
    }
    for (int i = 0; i < static_cast<int>(fusedPartners_.size()); ++i) {
        MeleeSlot& slot = fusedPartners_[i];
        if (slot.type != partner->type) {
            continue;
        }
        // X軸で半回転して刃を手の下側へ向ける。握りのずれも同じ向きに裏返し、土台の柄頭より先へずらす
        const PlayerVisualSettings& tune = SceneVisualTuning::Player();
        const float scale = tune.fusionPartnerScale;
        const Vector3 gripScale = { slot.gripScale.x * scale, slot.gripScale.y * scale, slot.gripScale.z * scale };
        const Vector3 gripRotate = { slot.gripRotate.x + gameConstants::kPi, slot.gripRotate.y, slot.gripRotate.z };
        const Vector3 gripTranslate = { slot.gripTranslate.x * scale, -slot.gripTranslate.y * scale - tune.fusionPartnerGap, -slot.gripTranslate.z * scale };
        AttachToHand(slot.object.get(), request, request.meleeBoneName, gripScale, gripRotate + request.gripOffset, gripTranslate);
        ApplyCorrection(*slot.object);
        fusedPartnerIndex_ = i;
        return;
    }
}

/** @brief 手に付けた近接武器へ、決めてある補正を掛ける */
void PlayerWeaponVisuals::ApplyCorrection(Object3d& object) const {
    if (!hasCorrection_) {
        return;
    }
    // 行ベクトルの行列なので、手に付けた後のワールド行列の右から掛けるとワールド座標での補正になる
    object.SetLocalMatrix(Multiply(object.GetLocalMatrix(), correction_));
    object.Update();
}

} // namespace engine::game

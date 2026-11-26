/**
 * @file WeaponFatigueTracker.cpp
 * @brief 武器ごとの疲労の加算・回復・ダメージ倍率を実装するファイル
 */
#include "WeaponFatigueTracker.h"
#include "CombatTuning.h"
#include "GameConstants.h"
#include "HitEffectLibrary.h"
#include "WeaponManager.h"
#include <algorithm>

namespace engine::game {

namespace {
constexpr float kMinFatigueRange = 0.01f; // 無料区間を1以上に設定されても0除算しないための下限
} // namespace

/** @brief 銃の添字（近接武器の後ろ） */
int WeaponFatigueTracker::GunIndex() {
    return static_cast<int>(WeaponManager::GetInstance()->GetList().size());
}

/** @brief ボスの枠の武器の添字（銃の次） */
int WeaponFatigueTracker::BossIndex() {
    return GunIndex() + 1;
}

/** @brief 手に持っている近接武器の添字（素手なら -1） */
int WeaponFatigueTracker::HeldMeleeIndex() {
    const auto* wm = WeaponManager::GetInstance();
    // ボスの枠の武器は、ふつうの枠の同じ種類の武器とは別に数える
    if (wm->IsBossSlotSelected()) {
        return BossIndex();
    }
    return wm->HasEquippedWeapon() ? wm->GetIndex() : -1;
}

/** @brief 指定した武器の疲労から、今のダメージ倍率を返す（1.0=万全〜weaponFatigueMinDamageMult） */
float WeaponFatigueTracker::DamageMult(int index) const {
    if (index < 0 || index >= Count()) {
        return 1.0f;
    }
    // 無料区間を超えた分だけ、最低の倍率へ向けて下げる
    const float freeRatio = CombatTune().weaponFatigueFreeRatio;
    const float t = std::clamp((fatigue_[index] - freeRatio) / (std::max)(1.0f - freeRatio, kMinFatigueRange),
        0.0f, 1.0f);
    return 1.0f + (CombatTune().weaponFatigueMinDamageMult - 1.0f) * t;
}

/** @brief このフレームに当てた武器を記録する（疲労の加算は Update() でまとめて1回だけ行う） */
void WeaponFatigueTracker::RegisterHit(int index, float amount) {
    if (index < 0) {
        return;
    }
    pendingIndex_ = index;
    // ボスの枠の武器は強い代わりに、当て続けると効きが早く落ちる（持ち替えると戻る）
    const float scaled = index == BossIndex() ? amount * CombatTune().bossWeaponFatigueMult : amount;
    pendingAmount_ = (std::max)(pendingAmount_, scaled);
}

/** @brief 当てた武器へ疲労を足し、それ以外の武器の疲労を抜く（ヒット判定の後に毎フレーム呼ぶ） */
void WeaponFatigueTracker::Update() {
    // 武器が増えても添字が足りるように広げる
    fatigue_.resize(static_cast<size_t>(BossIndex()) + 1, 0.0f);
    // このフレームに当てた武器へ疲労を足す
    if (pendingIndex_ >= 0 && pendingIndex_ < Count()) {
        float& fatigue = fatigue_[pendingIndex_];
        fatigue = (std::min)(fatigue + pendingAmount_, 1.0f);
        lastIndex_ = pendingIndex_;
        idleTimer_ = 0.0f;
    } else {
        idleTimer_ += gameConstants::kFrameDeltaTime;
    }
    pendingIndex_ = -1;
    pendingAmount_ = 0.0f;

    // 最後に当てた武器以外は休ませている扱いで回復する（近接と銃を交互に使うのも持ち替えになる）
    const float recover = CombatTune().weaponFatigueRecoverPerSecond * gameConstants::kFrameDeltaTime;
    const bool lastWeaponResting = idleTimer_ >= CombatTune().weaponFatigueRestDelay;
    for (int i = 0; i < Count(); ++i) {
        if (i != lastIndex_ || lastWeaponResting) {
            fatigue_[i] = (std::max)(fatigue_[i] - recover, 0.0f);
        }
    }
}

/** @brief 疲労で効きが落ちた命中の弾けを、威力に合わせて小さく灰色に寄せる */
void WeaponFatigueTracker::ApplyToHitEffect(float damageMult, Vector4& color, float& strength) {
    const float minMult = CombatTune().weaponFatigueMinDamageMult;
    if (damageMult >= 1.0f || minMult >= 1.0f) {
        return;
    }
    // 効きが落ちるほど鈍い灰色へ寄せ、弾けも小さくする
    const Vector4& dullColor = HitEffectLibrary::GetInstance()->Color("fatigue_dull");
    const float dull = std::clamp((1.0f - damageMult) / (1.0f - minMult), 0.0f, 1.0f);
    color = { color.x + (dullColor.x - color.x) * dull, color.y + (dullColor.y - color.y) * dull,
        color.z + (dullColor.z - color.z) * dull, color.w };
    strength *= 1.0f + (CombatTune().fatigueDullStrengthMin - 1.0f) * dull;
}

} // namespace engine::game

/**
 * @file CombatDepth.h
 * @brief 当たり判定の奥行きの幅を1か所で切り替える（横スクロールでは今までどおり、奥行きにも動く型では位置の前後だけ）
 * @note 横スクロールの当たりの箱は奥行きを決まった薄い幅で作る。奥行きにも動く型の時だけ SetBand で幅を決め、
 *       箱を作った所で Apply を呼ぶと、その物の奥行きの前後に箱を合わせる
 */
#pragma once
#include "MathCollision.h"
#include <cmath>

namespace engine::game::combatDepth {

/** @brief 当たりの奥行きの半分の幅（0 なら切り替えない＝横スクロールのまま） */
inline float sBandHalfDepth = 0.0f;
/** @brief 半分の幅から、2つの物の奥行きが当たる差（両側の分）を出す倍率 */
inline constexpr float kBothSides = 2.0f;

/** @brief 当たりの奥行きの半分の幅を決める（0 で切り替えをやめる。毎フレーム型から決め直す） */
inline void SetBand(float halfDepth) {
    sBandHalfDepth = halfDepth > 0.0f ? halfDepth : 0.0f;
}

/** @brief 奥行きを当たりに入れているか */
inline bool IsActive() {
    return sBandHalfDepth > 0.0f;
}

/** @brief 奥行きを当たりに入れている時だけ、箱の奥行きを centerZ の前後に合わせる */
inline void Apply(AABB& box, float centerZ) {
    if (!IsActive()) {
        return;
    }
    box.min.z = centerZ - sBandHalfDepth;
    box.max.z = centerZ + sBandHalfDepth;
}

/** @brief 奥行きを当たりに入れている時は、2つの奥行きが当たる近さか（入れていなければいつも true） */
inline bool IsNear(float aZ, float bZ) {
    return !IsActive() || std::abs(aZ - bZ) <= sBandHalfDepth * kBothSides;
}

} // namespace engine::game::combatDepth

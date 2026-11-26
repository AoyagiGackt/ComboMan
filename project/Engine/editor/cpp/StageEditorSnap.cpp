/**
 * @file StageEditorSnap.cpp
 * @brief ステージエディタの配置位置のスナップ計算を実装するファイル
 */
#include "StageEditorSnap.h"
#include <cmath>

namespace engine::editor {

/** @brief 有効な時に、値を間隔の倍数へ揃える */
float StageEditorSnap::Snap(float value, bool temporaryEnabled) const {
    // 切っていて一時的にも揃えない時と、間隔が正でない時は元の値をそのまま使う
    if ((!enabled_ && !temporaryEnabled) || step_ <= 0.0f) {
        return value;
    }
    return std::round(value / step_) * step_;
}

} // namespace engine::editor

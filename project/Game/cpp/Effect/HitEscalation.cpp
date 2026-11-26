/**
 * @file HitEscalation.cpp
 * @brief 連続ヒットの数え役を実装するファイル
 */
#include "HitEscalation.h"
#include "CombatTuning.h"
#include <algorithm>

namespace engine::game {

/** @brief 1回当てたことを数え、途切れるまでの時間を戻す */
void HitEscalation::RegisterHit() {
    ++count_;
    timer_ = CombatTune().hitEscalationKeepSeconds;
}

/** @brief 途切れるまでの時間を進め、切れたら数を戻す */
void HitEscalation::Update(float dt) {
    timer_ = (std::max)(timer_ - dt, 0.0f);
    if (timer_ <= 0.0f) {
        count_ = 0;
    }
}

/** @brief 今の盛り度合い（初撃は0、決めた連続ヒット数で1） */
float HitEscalation::Ratio() const {
    const int hits = (std::max)(count_ - 1, 0);
    return (std::min)(static_cast<float>(hits) / static_cast<float>((std::max)(CombatTune().hitEscalationMaxHits, 1)), 1.0f);
}

} // namespace engine::game

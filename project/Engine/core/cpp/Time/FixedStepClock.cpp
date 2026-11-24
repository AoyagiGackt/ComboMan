/**
 * @file FixedStepClock.cpp
 * @brief 固定の1回分の時間のまま、実際に過ぎた時間に追いつくよう更新の回数を決める時計の実装
 */
#include "FixedStepClock.h"
#include <algorithm>
#include <cmath>

namespace engine {
namespace {
/** @brief これより長く止まっていたら、遅れとして取り戻さず1回分として扱う秒数（読み込みやブレークポイントで止まった時） */
constexpr double kStallSeconds = 0.25;
/** @brief 1回分との差がこの割合より小さい間隔は、ちょうど1回分とみなす（画面の更新の細かい揺れで、まれに2回進むのを防ぐ） */
constexpr double kSnapRatio = 0.1;
} // namespace

/** @brief 1回の更新で進める時間を決める */
FixedStepClock::FixedStepClock(float stepSeconds)
    : stepSeconds_(static_cast<double>(stepSeconds)) {
}

/** @brief 前回から実際に過ぎた時間を足し、今回の描画までに回す更新の回数を返す */
int FixedStepClock::UpdatesThisFrame() {
    const Clock::time_point now = Clock::now();
    if (!started_) {
        started_ = true;
        last_ = now;
        accumulatedSeconds_ = 0.0;
        return 1;
    }
    const double elapsed = std::chrono::duration<double>(now - last_).count();
    last_ = now;

    // 長く止まっていた時は、その間の分を一気に進めるとゲームが飛ぶので、ふつうの1回分として扱う
    if (elapsed >= kStallSeconds) {
        accumulatedSeconds_ = 0.0;
        return 1;
    }

    // 1回分とほぼ同じ間隔なら、ちょうど1回分として足す（細かい揺れが積もって、たまに2回進むとカクつくため）
    const double step = std::abs(elapsed - stepSeconds_) < stepSeconds_ * kSnapRatio ? stepSeconds_ : elapsed;
    accumulatedSeconds_ += step;
    // 描画1回につき最低1回は進める（速すぎる時に止まって見えないように）。そのぶん遅れは負へ振れるが、0 で止める
    int updates = static_cast<int>(std::floor(accumulatedSeconds_ / stepSeconds_));
    updates = std::clamp(updates, 1, kMaxUpdatesPerFrame);
    accumulatedSeconds_ = (std::max)(accumulatedSeconds_ - stepSeconds_ * updates, 0.0);
    // 上限まで回しても追いつかないほど遅れている時は、残りを捨てて遅れが積み上がり続けないようにする
    if (updates == kMaxUpdatesPerFrame) {
        accumulatedSeconds_ = 0.0;
    }
    return updates;
}

/** @brief たまった遅れを捨て、次に呼んだ時を起点にし直す */
void FixedStepClock::Reset() {
    started_ = false;
    accumulatedSeconds_ = 0.0;
}

} // namespace engine

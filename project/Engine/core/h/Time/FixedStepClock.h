/**
 * @file FixedStepClock.h
 * @brief 固定の1回分の時間のまま、実際に過ぎた時間に追いつくよう更新の回数を決める時計
 */
#pragma once
#include <chrono>

namespace engine {

/**
 * @brief 1回の更新で進める時間は固定のまま、描画が遅れた分だけ更新を複数回回すための時計
 * @note 描画1回ごとに UpdatesThisFrame を1回呼び、返った回数だけゲームを更新する。
 *       処理が間に合っている時は毎回1回。重くて描画の間隔が延びた時だけ、遅れた分を数回の更新で取り戻す
 */
class FixedStepClock {
public:
    /**
     * @brief 1回の更新で進める時間を決める
     * @param stepSeconds 1回の更新で進める秒数
     */
    explicit FixedStepClock(float stepSeconds);

    /**
     * @brief 前回呼んだ時から実際に過ぎた時間を足し、今回の描画までに回す更新の回数を返す
     * @return 1 以上 kMaxUpdatesPerFrame 以下の回数
     */
    int UpdatesThisFrame();

    /** @brief たまった遅れを捨て、次に呼んだ時を起点にし直す（読み込みで止まった直後など） */
    void Reset();

    /** @brief 1回の描画で回す更新の上限（これより遅れた分は取り戻さずに捨てる） */
    static constexpr int kMaxUpdatesPerFrame = 4;

private:
    using Clock = std::chrono::steady_clock;

    /** @brief 1回の更新で進める秒数 */
    double stepSeconds_;
    /** @brief まだ更新で消化していない、実際に過ぎた時間（秒） */
    double accumulatedSeconds_ = 0.0;
    /** @brief 前回 UpdatesThisFrame を呼んだ時刻 */
    Clock::time_point last_ { };
    /** @brief last_ が有効か（最初の1回と Reset 直後は false） */
    bool started_ = false;
};

} // namespace engine

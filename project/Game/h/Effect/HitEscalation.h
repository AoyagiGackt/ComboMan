/**
 * @file HitEscalation.h
 * @brief 途切れずに当て続けたヒット数から、命中演出の盛り度合いを決める部品を定義するファイル
 */
#pragma once

namespace engine::game {

/**
 * @brief 連続ヒットの数え役
 * @note 雑魚・ボス・銃・近接を問わず、当てるたびに数を足す。
 *       決めた秒数（combat_tuning.json の hitEscalationKeepSeconds）当てないと初撃に戻る。
 */
class HitEscalation {
public:
    /** @brief 1回当てたことを数え、途切れるまでの時間を戻す */
    void RegisterHit();
    /**
     * @brief 途切れるまでの時間を進め、切れたら数を戻す
     * @param dt 経過秒数
     */
    void Update(float dt);
    /** @brief 今の盛り度合い（0.0=初撃〜1.0=上限） */
    float Ratio() const;

private:
    int count_ = 0; ///< 途切れずに当て続けたヒット数
    float timer_ = 0.0f; ///< 連続ヒットが途切れるまでの残り秒数
};

} // namespace engine::game

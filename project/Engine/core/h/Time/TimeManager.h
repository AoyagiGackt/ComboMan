/**
 * @file TimeManager.h
 * @brief 時間倍率とヒットストップを管理するファイル
 */
#pragma once
#include "EngineConstants.h"
#include <array>
namespace engine {

/**
 * @brief 止める・遅くするの残りを持つ時間の流れ1本（ゲーム全体と、時間の通り道ごとに1本ずつ持つ）
 * @note 止めたフレームを数え切ってから遅さを消化する（止め→ゆっくり→元の速さの順になる）
 */
class TimeFlow {
public:
    /** @brief 1フレーム進める（止めの残りを減らし、遅さの間はこのフレームを進めるかを決める） */
    void Step(float realDeltaSeconds);
    /** @brief 指定フレーム数止める（今の残りより長い時だけ延ばす） */
    void RequestStop(int frames);
    /** @brief 止めが明けた後に、指定した実時間だけ scale の割合で進める */
    void RequestSlow(float seconds, float scale);
    /** @brief このフレームは止まっているか（止めの間と、遅さで間引いたフレーム） */
    bool IsStopped() const {
        return stopFrames_ > 0 || skipFrame_;
    }
    /** @brief 遅さの残りがあるか */
    bool IsSlow() const {
        return slowSeconds_ > 0.0f;
    }

private:
    int stopFrames_ = 0; ///< 止めの残りフレーム
    float slowSeconds_ = 0.0f; ///< 遅さの残りの実時間
    float slowScale_ = 1.0f; ///< 遅さの間に進める割合
    float slowAccumulator_ = 0.0f; ///< 進めたフレームの端数（1を超えたらそのフレームを進める）
    bool skipFrame_ = false; ///< このフレームは遅さで間引かれたか
};

/**
 * @brief ゲーム全体の時間倍率とヒットストップ残量を一元管理する
 * @note ゲーム全体の流れのほかに、番号で分けた時間の通り道を持てる。通り道が何を表すか（敵だけ・演出だけなど）はゲームが決め、
 *       その物の更新で IsChannelStopped を見る。通り道の止めと遅さは、全体のヒットストップが明けてから進む
 */
class TimeManager {
public:
    /** @brief 時間の通り道の数 */
    static constexpr int kMaxChannels = 8;

    /** @brief 共有時間管理を返す @return 時間管理のポインタ */
    static TimeManager* GetInstance();

    /** @brief ヒットストップの残りフレームを更新する */
    void Update();

    /** @brief 時間倍率と停止状態を反映したフレーム間隔を返す @return ゲーム更新用の秒数 */
    float GetDeltaTime() const;
    /** @brief 現在の時間倍率を返す @return 時間倍率 */
    float GetTimeScale() const {
        return timeScale_;
    }
    /**
     * @brief ヒットストップ中か返す @return 停止中の場合はtrue
     * @note スロー中に間引かれたフレームも停止扱いになる（固定dtで動く処理をまとめて遅くするため）
     */
    bool IsHitStopped() const {
        return global_.IsStopped();
    }

    /** @brief ゲーム更新へ適用する時間倍率を設定する @param scale 0で停止、1で通常速度となる倍率 */
    void SetTimeScale(float scale);

    /** @brief 指定フレーム数のヒットストップを要求する @param frames 停止するフレーム数 */
    void RequestHitStop(int frames);

    /**
     * @brief ヒットストップ明けに続くスローを要求する
     * @param seconds スローを続ける実時間（秒、ヒットストップ中は減らない）
     * @param scale   ゲームを進める割合（0.5なら2フレームに1回だけ進める）
     * @note 各処理が固定dtで動くため、フレームを間引くことで全体を遅くする
     */
    void RequestSlowMotion(float seconds, float scale);
    /** @brief スロー中か返す @return スローの残り時間がある場合はtrue */
    bool IsSlowMotion() const {
        return global_.IsSlow();
    }

    /**
     * @brief 時間の通り道を指定フレーム数止める（全体のヒットストップが明けてから数える）
     * @param channel 通り道の番号（0〜kMaxChannels-1。範囲の外は何もしない）
     */
    void RequestChannelStop(int channel, int frames);
    /** @brief 時間の通り道を、止めが明けた後に指定した実時間だけ遅くする（scale はゲームを進める割合） */
    void RequestChannelSlow(int channel, float seconds, float scale);
    /** @brief その通り道の物をこのフレーム止めるか（全体が止まっている時も true） */
    bool IsChannelStopped(int channel) const;

    static constexpr float kBaseDeltaTime = engineConstants::kFrameDeltaTime;

private:
    /** @brief 時間倍率と停止要求を既定状態にする */
    TimeManager() = default;
    /** @brief 番号が通り道の範囲の中か */
    static bool IsValidChannel(int channel) {
        return channel >= 0 && channel < kMaxChannels;
    }

    float timeScale_ = 1.0f;
    TimeFlow global_; ///< ゲーム全体の流れ
    std::array<TimeFlow, kMaxChannels> channels_ { }; ///< 番号で分けた時間の通り道
};

} // namespace engine

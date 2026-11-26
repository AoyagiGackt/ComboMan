/**
 * @file TimeManager.cpp
 * @brief 時間倍率とヒットストップの残フレーム管理（TimeManager）と、時間の流れ1本（TimeFlow）の実装
 */
#include "TimeManager.h"
#include <algorithm>

namespace engine {
namespace {
constexpr float kMinSlowScale = 0.05f; // 完全に止まるとヒットストップと区別が付かないため下限を設ける
constexpr float kMaxTimeScale = 10.0f;
} // namespace

/** @brief 1フレーム進める（止めの残りを減らし、遅さの間はこのフレームを進めるかを決める） */
void TimeFlow::Step(float realDeltaSeconds) {
    skipFrame_ = false;
    if (stopFrames_ > 0) {
        --stopFrames_;
        return;
    }
    // 止めが明けてから遅さを消化する（止め→ゆっくり→元の速さの順になる）
    if (slowSeconds_ > 0.0f) {
        slowSeconds_ = (std::max)(slowSeconds_ - realDeltaSeconds, 0.0f);
        slowAccumulator_ += slowScale_;
        if (slowAccumulator_ >= 1.0f) {
            slowAccumulator_ -= 1.0f;
        } else {
            skipFrame_ = true;
        }
    }
}

/** @brief 指定フレーム数止める（今の残りより長い時だけ延ばす） */
void TimeFlow::RequestStop(int frames) {
    stopFrames_ = (std::max)(stopFrames_, frames);
}

/** @brief 止めが明けた後に、指定した実時間だけ scale の割合で進める */
void TimeFlow::RequestSlow(float seconds, float scale) {
    slowSeconds_ = (std::max)(slowSeconds_, seconds);
    slowScale_ = std::clamp(scale, kMinSlowScale, 1.0f);
    slowAccumulator_ = 0.0f;
}

/** @brief 共有時間管理を返す */
TimeManager* TimeManager::GetInstance() {
    static TimeManager sInstance;
    return &sInstance;
}

/** @brief ヒットストップの残りフレームを更新する */
void TimeManager::Update() {
    global_.Step(kBaseDeltaTime);
    // 通り道は全体のヒットストップが明けてから進める（全体の止めに重ねて、その物だけもう少し止める使い方をするため）
    if (global_.IsStopped()) {
        return;
    }
    for (TimeFlow& channel : channels_) {
        channel.Step(kBaseDeltaTime);
    }
}

/** @brief 時間倍率と停止状態を反映したフレーム間隔を返す */
float TimeManager::GetDeltaTime() const {
    if (IsHitStopped()) {
        return 0.0f;
    }
    return kBaseDeltaTime * timeScale_;
}

/** @brief ゲーム更新へ適用する時間倍率を設定する */
void TimeManager::SetTimeScale(float scale) {
    timeScale_ = std::clamp(scale, 0.0f, kMaxTimeScale);
}

/** @brief 指定フレーム数のヒットストップを要求する */
void TimeManager::RequestHitStop(int frames) {
    global_.RequestStop(frames);
}

/** @brief ヒットストップ明けに続くスローを要求する */
void TimeManager::RequestSlowMotion(float seconds, float scale) {
    global_.RequestSlow(seconds, scale);
}

/** @brief 時間の通り道を指定フレーム数止める */
void TimeManager::RequestChannelStop(int channel, int frames) {
    if (IsValidChannel(channel)) {
        channels_[static_cast<size_t>(channel)].RequestStop(frames);
    }
}

/** @brief 時間の通り道を、止めが明けた後に指定した実時間だけ遅くする */
void TimeManager::RequestChannelSlow(int channel, float seconds, float scale) {
    if (IsValidChannel(channel)) {
        channels_[static_cast<size_t>(channel)].RequestSlow(seconds, scale);
    }
}

/** @brief その通り道の物をこのフレーム止めるか（全体が止まっている時も true） */
bool TimeManager::IsChannelStopped(int channel) const {
    if (IsHitStopped()) {
        return true;
    }
    return IsValidChannel(channel) && channels_[static_cast<size_t>(channel)].IsStopped();
}

} // namespace engine

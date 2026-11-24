/**
 * @file GamepadRumble.cpp
 * @brief ゲームパッドの振動を制御する
 */
#include "GamepadRumble.h"
#include "Input.h"
#include <algorithm>
#include <chrono>
#include <cmath>
namespace engine {
namespace {
constexpr DWORD kUserIndex = 0; // 震わせるパッド（1台目）
constexpr float kMotorMaximum = 65535.0f; // XInput のモーターの強さの最大値
}
/** @brief インスタンスを取得する */
GamepadRumble* GamepadRumble::GetInstance() {
    static GamepadRumble sInstance;
    return &sInstance;
}
/** @brief 前回からの実時間を秒で取得する */
float GamepadRumble::RealDeltaSeconds() {
    const auto now = std::chrono::steady_clock::now();
    static auto sPrevious = now;
    const float elapsed = std::chrono::duration<float>(now - sPrevious).count();
    sPrevious = now;
    return elapsed;
}
/** @brief 各モーターの強さと秒数を指定して振動を開始する */
void GamepadRumble::Request(float low, float high, float seconds) {
    if (!enabled_ || !std::isfinite(low) || !std::isfinite(high) || !std::isfinite(seconds) || seconds <= 0.0f) {
        return;
    }
    // 減衰後の強さと新しい強さを比較する
    const float ratio = durationSeconds_ > 0.0f ? remainingSeconds_ / durationSeconds_ : 0.0f;
    low_ = (std::max)(low_ * ratio, std::clamp(low, 0.0f, 1.0f));
    high_ = (std::max)(high_ * ratio, std::clamp(high, 0.0f, 1.0f));
    durationSeconds_ = (std::max)(remainingSeconds_, seconds);
    remainingSeconds_ = durationSeconds_;
    Send(low_, high_);
}
/** @brief 実時間に合わせて振動を更新する */
void GamepadRumble::Update(float realDeltaSeconds) {
    if (remainingSeconds_ <= 0.0f || !std::isfinite(realDeltaSeconds) || realDeltaSeconds <= 0.0f) {
        return;
    }
    remainingSeconds_ = (std::max)(0.0f, remainingSeconds_ - realDeltaSeconds);
    const float ratio = remainingSeconds_ / durationSeconds_;
    Send(low_ * ratio, high_ * ratio);
}
/** @brief 振動を停止する */
void GamepadRumble::Stop() {
    low_ = 0.0f;
    high_ = 0.0f;
    remainingSeconds_ = 0.0f;
    durationSeconds_ = 0.0f;
    Send(0.0f, 0.0f);
}
/** @brief 振動の有効状態を設定する */
void GamepadRumble::SetEnabled(bool enabled) {
    enabled_ = enabled;
    if (!enabled_) {
        Stop();
    }
}
/** @brief 振動の有効状態を取得する */
bool GamepadRumble::IsEnabled() const {
    return enabled_;
}
/** @brief 変更された強さを送信する */
void GamepadRumble::Send(float low, float high) {
    const auto lowValue = static_cast<uint16_t>(low * kMotorMaximum);
    const auto highValue = static_cast<uint16_t>(high * kMotorMaximum);
    if (lowValue == sentLow_ && highValue == sentHigh_) {
        return;
    }
    XINPUT_VIBRATION vibration {};
    vibration.wLeftMotorSpeed = lowValue;
    vibration.wRightMotorSpeed = highValue;
    XInputSetState(kUserIndex, &vibration);
    sentLow_ = lowValue;
    sentHigh_ = highValue;
}
} // namespace engine

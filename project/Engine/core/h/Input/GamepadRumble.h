/**
 * @file GamepadRumble.h
 * @brief ゲームパッドの振動を管理する
 */
#pragma once
#include <cstdint>
namespace engine {
/** @brief ゲームパッドの振動を管理する */
class GamepadRumble {
public:
    /** @brief インスタンスを取得する */
    static GamepadRumble* GetInstance();
    /** @brief 前回からの実時間を秒で取得する */
    static float RealDeltaSeconds();
    /** @brief 各モーターの強さと秒数を指定して振動を開始する */
    void Request(float low, float high, float seconds);
    /** @brief 実時間に合わせて振動を更新する */
    void Update(float realDeltaSeconds);
    /** @brief 振動を停止する */
    void Stop();
    /** @brief 振動の有効状態を設定する */
    void SetEnabled(bool enabled);
    /** @brief 振動の有効状態を取得する */
    bool IsEnabled() const;
private:
    /** @brief 振動管理を初期化する */
    GamepadRumble() = default;
    /** @brief 変更された強さを送信する */
    void Send(float low, float high);
    float low_ = 0.0f; ///< 低い揺れのモーターの強さ（揺れ始めの値。0〜1）
    float high_ = 0.0f; ///< 高い揺れのモーターの強さ（揺れ始めの値。0〜1）
    float remainingSeconds_ = 0.0f; ///< 揺れの残りの秒数（実時間）
    float durationSeconds_ = 0.0f; ///< 今の揺れ全体の長さ（残りとの比で弱めていく）
    uint16_t sentLow_ = 0; ///< 最後にパッドへ送った低い揺れの値（同じ値は送り直さない）
    uint16_t sentHigh_ = 0; ///< 最後にパッドへ送った高い揺れの値
    bool enabled_ = true; ///< 振動を使うか（設定の VIBRATION）
};
} // namespace engine

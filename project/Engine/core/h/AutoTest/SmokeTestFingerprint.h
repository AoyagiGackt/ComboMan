/**
 * @file SmokeTestFingerprint.h
 * @brief 再生の照合に使う、ゲームの状態の指紋を作る部品
 * @note SmokeTestGame::Capture の中で、画面の名前・体力・位置などを順に混ぜて使う。
 *       同じ順で同じ値を混ぜれば同じ指紋になるので、再生した時に動きがずれたフレームを見つけられる
 */
#pragma once
#include "Vector3.h"
#include <cstddef>
#include <cstdint>
#include <string>

namespace engine {

/** @brief 値を順に混ぜて、状態の指紋を作る */
class SmokeTestFingerprint {
public:
    /** @brief バイト列を混ぜる */
    void MixBytes(const void* data, size_t size);
    /** @brief 整数を混ぜる */
    void MixInt(int64_t value);
    /** @brief 文字を混ぜる */
    void MixText(const std::string& text);
    /** @brief 位置を混ぜる（とても小さな差で別の指紋にならないよう丸める） */
    void MixPosition(const Vector3& position);
    /** @brief ここまで混ぜた指紋を返す */
    uint64_t Value() const {
        return hash_;
    }

private:
    static constexpr uint64_t kFnvOffset = 1469598103934665603ull; ///< 指紋を作る計算の始まりの値
    uint64_t hash_ = kFnvOffset; ///< ここまで混ぜた値
};

} // namespace engine

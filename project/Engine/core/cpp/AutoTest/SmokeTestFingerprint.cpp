/**
 * @file SmokeTestFingerprint.cpp
 * @brief 再生の照合に使う、ゲームの状態の指紋を作る部品の実装
 */
#include "SmokeTestFingerprint.h"
#include <cmath>

namespace engine {
namespace {
constexpr uint64_t kFnvPrime = 1099511628211ull; // 指紋を作る計算の掛け数
constexpr float kPositionQuantize = 1000.0f; // 位置を指紋に入れる時に、この分の1の細かさで丸める
} // namespace

/** @brief バイト列を1バイトずつ混ぜる */
void SmokeTestFingerprint::MixBytes(const void* data, size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash_ ^= bytes[i];
        hash_ *= kFnvPrime;
    }
}

/** @brief 整数を混ぜる */
void SmokeTestFingerprint::MixInt(int64_t value) {
    MixBytes(&value, sizeof(value));
}

/** @brief 文字を混ぜる */
void SmokeTestFingerprint::MixText(const std::string& text) {
    MixBytes(text.data(), text.size());
}

/** @brief 位置を丸めてから、x・y・z の順に混ぜる */
void SmokeTestFingerprint::MixPosition(const Vector3& position) {
    MixInt(std::llround(position.x * kPositionQuantize));
    MixInt(std::llround(position.y * kPositionQuantize));
    MixInt(std::llround(position.z * kPositionQuantize));
}

} // namespace engine

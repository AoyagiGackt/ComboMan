/**
 * @file RandomSeed.cpp
 * @brief 乱数の種をまとめて配る仕組みを実装するファイル
 */
#include "RandomSeed.h"
#include <atomic>
#include <random>

namespace engine::randomSeed {

namespace {
constexpr uint64_t kGoldenGamma = 0x9E3779B97F4A7C15ull; // 番号ごとに種をばらけさせる足し幅（黄金比から作った奇数）
constexpr uint64_t kMixMultiplier1 = 0xBF58476D1CE4E5B9ull; // 混ぜる時の掛け数1
constexpr uint64_t kMixMultiplier2 = 0x94D049BB133111EBull; // 混ぜる時の掛け数2
constexpr int kMixShift1 = 30; // 混ぜる時のずらし幅1
constexpr int kMixShift2 = 27; // 混ぜる時のずらし幅2
constexpr int kMixShift3 = 31; // 混ぜる時のずらし幅3

std::atomic<uint32_t> sBase { 0 }; // 元の種（0 なら決まっていない）
std::atomic<uint64_t> sCounter { 0 }; // 配った種の数（決まった元の種と合わせて次の種を決める）

/** @brief 64ビットの値をよく混ぜる（近い番号から、似ていない種を作る） */
uint64_t Mix(uint64_t value) {
    value = (value ^ (value >> kMixShift1)) * kMixMultiplier1;
    value = (value ^ (value >> kMixShift2)) * kMixMultiplier2;
    return value ^ (value >> kMixShift3);
}
} // namespace

/** @brief 元の種を決め、配った数を数え直す */
void SetBase(uint32_t base) {
    sBase = base;
    sCounter = 0;
}

/** @brief 元の種が決まっているか */
bool IsFixed() {
    return sBase != 0;
}

/** @brief 決めた元の種 */
uint32_t GetBase() {
    return sBase;
}

/** @brief 乱数1つぶんの種を返す */
uint32_t Next() {
    const uint32_t base = sBase;
    if (base == 0) {
        // 決まっていなければ、ふだんどおり毎回ちがう種にする
        return std::random_device {}();
    }
    // 元の種と呼ばれた順番から決める（同じ順に作られた乱数は毎回同じ種になる）
    const uint64_t index = sCounter.fetch_add(1);
    return static_cast<uint32_t>(Mix(static_cast<uint64_t>(base) + (index + 1) * kGoldenGamma));
}

} // namespace engine::randomSeed

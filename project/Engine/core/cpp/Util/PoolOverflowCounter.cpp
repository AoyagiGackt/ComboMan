/**
 * @file PoolOverflowCounter.cpp
 * @brief プールが足りずに頼みを捨てた回数を数える仕組みを実装するファイル
 */
#include "PoolOverflowCounter.h"
#include <map>
#include <mutex>

namespace engine::poolOverflowCounter {

namespace {
/** @brief 数えた回数の置き場所（別のスレッドから数えても壊れないように鍵を掛ける） */
struct Counters {
    std::mutex mutex; ///< 数を書き換える時の鍵
    std::map<std::string, uint64_t> counts; ///< プールの名前 → 捨てた回数
};

/** @brief 数えた回数の置き場所（初めて使う時に作る） */
Counters& GetCounters() {
    static Counters sCounters;
    return sCounters;
}
} // namespace

/** @brief プールが足りずに頼みを1回捨てたことを数える */
void Count(const char* poolName) {
    Counters& counters = GetCounters();
    const std::lock_guard<std::mutex> lock(counters.mutex);
    ++counters.counts[poolName != nullptr ? poolName : "unknown"];
}

/** @brief 数えた回数をプールごとに fn へ渡す */
void ForEach(const std::function<void(const std::string& poolName, uint64_t count)>& fn) {
    Counters& counters = GetCounters();
    // 鍵を掛けている間に fn を呼ばないよう、写しを取ってから渡す
    std::map<std::string, uint64_t> snapshot;
    {
        const std::lock_guard<std::mutex> lock(counters.mutex);
        snapshot = counters.counts;
    }
    for (const auto& [name, count] : snapshot) {
        fn(name, count);
    }
}

} // namespace engine::poolOverflowCounter

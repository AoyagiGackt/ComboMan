/**
 * @file TestCoverage.cpp
 * @brief 自動テストプレイで、何を試したかを数える仕組みを実装するファイル
 */
#include "TestCoverage.h"
#include "SmokeTestRunner.h"
#include <map>

namespace engine::testCoverage {

namespace {
/** @brief 種類1つぶんの数え */
struct CategoryRecord {
    std::set<std::string> declared; ///< 試せる物
    std::set<std::string> covered; ///< 実際に起きた物
};

/** @brief 種類ごとの数え（初めて使う時に作る） */
std::map<std::string, CategoryRecord>& Records() {
    static std::map<std::string, CategoryRecord> sRecords;
    return sRecords;
}
} // namespace

/** @brief 自動テストプレイの時だけ数える */
bool IsEnabled() {
    return SmokeTestRunner::IsActive();
}

/** @brief 試せる物を1つ知らせる */
void Declare(const std::string& category, const std::string& name) {
    if (!IsEnabled()) {
        return;
    }
    Records()[category].declared.insert(name);
}

/** @brief 実際に起きた物を1つ知らせる */
void Cover(const std::string& category, const std::string& name) {
    if (!IsEnabled()) {
        return;
    }
    Records()[category].covered.insert(name);
}

/** @brief 種類ごとに、試せる物と試した物を fn へ渡す */
void ForEach(const std::function<void(const std::string& category, const std::set<std::string>& declared,
    const std::set<std::string>& covered)>& fn) {
    for (const auto& [category, record] : Records()) {
        fn(category, record.declared, record.covered);
    }
}

} // namespace engine::testCoverage

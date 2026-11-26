/**
 * @file PoolOverflowCounter.h
 * @brief 決まった数だけ用意した入れ物（プール）が足りずに、頼みを捨てた回数を数える仕組みを定義するファイル
 * @note 捨てても落ちはしないが、演出が出ない・弾が出ないといった見えにくい不具合になるので、自動テストが数を見て知らせる
 */
#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace engine::poolOverflowCounter {

/**
 * @brief プールが足りずに頼みを1回捨てたことを数える
 * @param poolName プールの名前（報告に出す。同じ名前はまとめて数える）
 */
void Count(const char* poolName);
/**
 * @brief 数えた回数をプールごとに fn へ渡す
 * @param fn プールの名前と、捨てた回数を受け取る
 */
void ForEach(const std::function<void(const std::string& poolName, uint64_t count)>& fn);

} // namespace engine::poolOverflowCounter

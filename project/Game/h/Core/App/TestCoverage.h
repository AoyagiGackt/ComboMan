/**
 * @file TestCoverage.h
 * @brief 自動テストプレイで、何を試したか（武器・技・ボスの技・部屋など）を数える仕組みを定義するファイル
 * @note ゲーム側は、試せる物を Declare で、実際に起きた物を Cover で知らせるだけでよい。
 *       自動テストプレイでない時は何もしない（ふだんの遊びには影響しない）。
 *       結果は自動テストの報告に、種類ごとの 試した数 / 全部の数 と、まだ試していない物の一覧で出る
 */
#pragma once
#include <cstdint>
#include <functional>
#include <set>
#include <string>

namespace engine::game::testCoverage {

/**
 * @brief 試せる物を1つ知らせる（全部の数と、まだ試していない物を出すため）
 * @param category 種類（例: weapon・melee_step・boss_move・room）
 * @param name その種類の中の名前
 */
void Declare(const std::string& category, const std::string& name);
/**
 * @brief 実際に起きた物を1つ知らせる（Declare していない物でも数える）
 * @param category 種類
 * @param name その種類の中の名前
 */
void Cover(const std::string& category, const std::string& name);
/** @brief 数えるか（自動テストプレイの時だけ true。呼ぶ側が名前を作る手間を省く時に使う） */
bool IsEnabled();
/**
 * @brief 種類ごとに、試せる物と試した物を fn へ渡す（報告を書く時に使う）
 * @param fn 種類・試せる物・試した物を受け取る
 */
void ForEach(const std::function<void(const std::string& category, const std::set<std::string>& declared,
    const std::set<std::string>& covered)>& fn);

} // namespace engine::game::testCoverage

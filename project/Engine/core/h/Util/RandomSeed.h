/**
 * @file RandomSeed.h
 * @brief 乱数の種をまとめて配る仕組みを定義するファイル
 * @note ふだんは毎回ちがう種を配る。自動テストの時に元の種を決めると、同じ順に作られた乱数は毎回同じ種を受け取り、
 *       同じ操作をすれば同じ動きになる（見つけた不具合をもう一度起こせる）
 */
#pragma once
#include <cstdint>

namespace engine::randomSeed {

/**
 * @brief 元の種を決める（これ以降に配る種がすべて決まる。0 なら毎回ちがう種に戻す）
 * @param base 元の種
 * @note 乱数を作る物より先に呼ぶ（起動の最初）
 */
void SetBase(uint32_t base);
/** @brief 元の種が決まっているか */
bool IsFixed();
/** @brief 決めた元の種（決まっていなければ 0） */
uint32_t GetBase();
/**
 * @brief 乱数1つぶんの種を返す
 * @return 元の種が決まっていれば、元の種と呼ばれた順番から決まる種。決まっていなければ毎回ちがう種
 */
uint32_t Next();

} // namespace engine::randomSeed

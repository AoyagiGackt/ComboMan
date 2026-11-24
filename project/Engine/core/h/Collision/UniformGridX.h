/** @file UniformGridX.h
 * @brief 横方向の格子で当たり判定の候補を検索する
 */
#pragma once
#include "CollisionConfig.h"
#include <cstdint>
#include <vector>

namespace engine {
/**
 * @brief 横方向の格子に矩形の番号を登録して検索する
 * @note 横スクロールなので X だけで区切る。調べたい範囲に掛かるセルの矩形だけを候補として返し、総当たりを避ける
 */
class UniformGridX {
public:
    /** @brief 矩形の一覧から格子を構築する */
    void Build(const std::vector<AABB>& boxes, float cellWidth = kDefaultCellWidth);
    /** @brief 範囲に掛かるセルの矩形番号を重複なく返す */
    void Query(float minX, float maxX, std::vector<uint32_t>& out) const;
private:
    /** @brief セルの座標と矩形番号を保持する */
    struct Entry {
        int64_t cell; ///< セルの番号（x をセルの幅で割って切り捨てた値）
        uint32_t index; ///< 登録した矩形の番号
    };
    /** @brief セルの幅の既定値（ステージのブロック数個ぶん） */
    static constexpr float kDefaultCellWidth = 4.0f;
    float cellWidth_ = kDefaultCellWidth; ///< セルの幅
    std::vector<Entry> entries_; ///< セルの番号の順に並べた登録
    mutable std::vector<uint32_t> lastSeen_; ///< 矩形ごとに、最後に結果へ入れた問い合わせの番号（重複を除くため）
    mutable uint32_t queryId_ = 0; ///< 問い合わせの番号（問い合わせごとに増やす）
};
} // namespace engine

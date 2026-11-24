/** @file UniformGridX.cpp
 * @brief 横方向の格子を構築して矩形の候補を検索する
 */
#include "UniformGridX.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace engine {
/** @brief 矩形の一覧から格子を構築する */
void UniformGridX::Build(const std::vector<AABB>& boxes, float cellWidth) {
    cellWidth_ = cellWidth > 0.0f ? cellWidth : kDefaultCellWidth;
    entries_.clear();
    lastSeen_.assign(boxes.size(), 0);
    queryId_ = 0;
    for (size_t i = 0; i < boxes.size(); ++i) {
        const int64_t first = static_cast<int64_t>(std::floor(boxes[i].min.x / cellWidth_));
        const int64_t last = static_cast<int64_t>(std::floor(boxes[i].max.x / cellWidth_));
        for (int64_t cell = first; cell <= last; ++cell) {
            entries_.push_back({ cell, static_cast<uint32_t>(i) });
        }
    }
    std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
        return a.cell < b.cell;
    });
}

/** @brief 範囲に掛かるセルの矩形番号を重複なく返す */
void UniformGridX::Query(float minX, float maxX, std::vector<uint32_t>& out) const {
    out.clear();
    if (minX > maxX) {
        return;
    }
    if (queryId_ == (std::numeric_limits<uint32_t>::max)()) {
        std::fill(lastSeen_.begin(), lastSeen_.end(), 0);
        queryId_ = 0;
    }
    ++queryId_;
    const int64_t first = static_cast<int64_t>(std::floor(minX / cellWidth_));
    const int64_t last = static_cast<int64_t>(std::floor(maxX / cellWidth_));
    auto it = std::lower_bound(entries_.begin(), entries_.end(), first,
        [](const Entry& entry, int64_t cell) { return entry.cell < cell; });
    for (; it != entries_.end() && it->cell <= last; ++it) {
        if (lastSeen_[it->index] != queryId_) {
            lastSeen_[it->index] = queryId_;
            out.push_back(it->index);
        }
    }
    std::sort(out.begin(), out.end());
}
} // namespace engine

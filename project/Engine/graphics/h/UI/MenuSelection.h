/**
 * @file MenuSelection.h
 * @brief メニューの項目の並びと、有効な項目の間で選択を動かす仕組みを定義するファイル
 */
#pragma once
#include "UIButton.h"
#include <cstddef>
#include <vector>

namespace engine::graphics {

/** @brief 項目を選んだ後の操作結果 */
enum class MenuDecision { kNone, kConfirm, kBack };

/** @brief 表示順に項目を保持し、有効な項目の間で選択を動かす */
class MenuSelection {
public:
    /** @brief 項目を入れ替え、最初の有効な項目を選ぶ */
    void SetItems(const std::vector<UIButton>& items);
    /** @brief 上下の入力を順に反映し、選択位置が変わったか返す */
    bool Move(bool up, bool down);
    /** @brief 決定と戻るの入力から選択結果を返す */
    MenuDecision Decide(bool confirm, bool back) const;
    /** @brief 選択中の項目の位置を返す */
    int GetSelectedIndex() const {
        return cursor_;
    }
    /** @brief 項目数を返す */
    std::size_t Count() const {
        return items_.size();
    }
    /** @brief 指定位置の項目を選べるか返す */
    bool IsEnabled(std::size_t index) const {
        return items_[index].enabled;
    }

private:
    /** @brief 指定方向で次に選べる項目を探す */
    void Step(int direction);
    std::vector<UIButton> items_;
    int cursor_ = 0;
};

} // namespace engine::graphics

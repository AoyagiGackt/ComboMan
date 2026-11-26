/**
 * @file MenuSelection.cpp
 * @brief メニューの選択の移動と決定の判定を実装するファイル
 */
#include "MenuSelection.h"

namespace engine::graphics {

/** @brief 項目の並びを保存し、先頭から有効な項目を探す */
void MenuSelection::SetItems(const std::vector<UIButton>& items) {
    items_ = items;
    cursor_ = 0;
    if (!items_.empty() && !IsEnabled(0)) {
        Step(1);
    }
}

/** @brief 端を越えずに無効な項目を飛ばして移動する */
void MenuSelection::Step(int direction) {
    for (int i = cursor_ + direction; i >= 0 && i < static_cast<int>(Count()); i += direction) {
        if (IsEnabled(i)) {
            cursor_ = i;
            break;
        }
    }
}

/** @brief 同時入力のときは上へ移動した後に下へ移動する */
bool MenuSelection::Move(bool up, bool down) {
    const int previous = cursor_;
    if (up) {
        Step(-1);
    }
    if (down) {
        Step(1);
    }
    return cursor_ != previous;
}

/** @brief 戻る操作を受け付け、選択可能な項目に対する決定を判定する */
MenuDecision MenuSelection::Decide(bool confirm, bool back) const {
    if (back) {
        return MenuDecision::kBack;
    }
    if (confirm && !items_.empty() && IsEnabled(cursor_)) {
        return MenuDecision::kConfirm;
    }
    return MenuDecision::kNone;
}

} // namespace engine::graphics

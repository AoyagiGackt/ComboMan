/**
 * @file StageEditorSnap.h
 * @brief ステージエディタの配置位置を決まった間隔へ揃える設定と計算を定義するファイル
 */
#pragma once

namespace engine::editor {

/**
 * @brief 配置位置のスナップ
 * @note ドラッグ移動・新規配置・複製に効く。間隔と有効かどうかはエディタの画面から ImGui で直接書き換えるので、参照を返す
 */
class StageEditorSnap {
public:
    /** @brief スナップを使うか（ImGui のチェックボックスから書き換える） */
    bool& Enabled() {
        return enabled_;
    }
    /** @brief スナップを使うか */
    bool Enabled() const {
        return enabled_;
    }
    /** @brief スナップの間隔（ImGui のドラッグ入力から書き換える） */
    float& Step() {
        return step_;
    }
    /** @brief スナップの間隔 */
    float Step() const {
        return step_;
    }
    /**
     * @brief 有効な時に、値を間隔の倍数へ揃える
     * @param value 揃える値
     * @param temporaryEnabled スナップを切っていても、この時だけ揃えるか（Ctrl を押している間など）
     * @return 揃えた値（揃えない時はそのまま）
     */
    float Snap(float value, bool temporaryEnabled) const;

private:
    bool enabled_ = false; ///< スナップを使うか
    float step_ = 1.0f; ///< スナップの間隔
};

} // namespace engine::editor

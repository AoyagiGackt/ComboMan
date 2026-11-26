/**
 * @file FieldMemory.h
 * @brief 値をばらばらの変数で直すパネルに、項目の説明と、開いた時の値へ戻す右クリックメニューを付ける部品
 */
#pragma once
#ifdef USE_IMGUI
#include "EditorUI.h"
#include <any>
#include <imgui.h>
#include <unordered_map>

namespace engine::editor {

/**
 * @brief 編集する変数ごとに、最初に見た時の値を覚えておく
 * @note 変数の場所で見分けるので、毎フレーム同じ変数を直接編集する部品に使う（一時変数の写しには使えない）。
 *       使い方: if (memory.Track(ImGui::DragFloat(...), value, "説明")) { 反映 }
 */
class FieldMemory {
public:
    /**
     * @brief 直前の入力部品に説明と戻すメニューを付ける
     * @param changed 直前の部品が値を変えたか
     * @param value   直前の部品が編集した変数
     * @param help    マウスを乗せた時の説明（nullptr なら出さない）
     * @return 部品で変えたか、メニューで戻したら true（呼び出し側で反映する）
     */
    template <class T>
    bool Track(bool changed, T& value, const char* help) {
        // 初めて見た変数は、その時の値を戻す先として覚える
        const void* key = &value;
        if (!originals_.contains(key)) {
            originals_[key] = value;
        }
        // 説明
        if (help != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            graphics::editorUI::TooltipWrapped("%s\n右クリックで開いた時の値に戻せます", help);
        }
        // 右クリックで開いた時の値に戻す（変数の場所でメニューを見分ける）
        bool reverted = false;
        ImGui::PushID(key);
        if (ImGui::BeginPopupContextItem(kMenuId)) {
            if (ImGui::MenuItem("開いた時の値に戻す")) {
                if (const T* original = std::any_cast<T>(&originals_[key])) {
                    value = *original;
                    reverted = true;
                }
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
        return changed || reverted;
    }

    /** @brief 覚えた値を忘れる（パネルを開き直した時に呼び、その時の値を新しい戻す先にする） */
    void Forget() {
        originals_.clear();
    }

private:
    /** @brief 戻すメニューの名前（変数の場所と組み合わせて見分ける） */
    static constexpr const char* kMenuId = "##fieldMemoryMenu";

    std::unordered_map<const void*, std::any> originals_; ///< 変数の場所と、最初に見た時の値
};

} // namespace engine::editor
#endif // USE_IMGUI

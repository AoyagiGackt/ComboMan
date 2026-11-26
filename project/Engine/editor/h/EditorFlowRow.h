/**
 * @file EditorFlowRow.h
 * @brief ボタンを入りきる分だけ横に並べ、入らなければ次の行へ回す部品（パネルが狭くてもはみ出さない）
 */
#pragma once
#ifdef USE_IMGUI
#include <imgui.h>

namespace engine::editor {

/**
 * @brief 1つの並びの右端を覚えておき、次の部品が入るかで同じ行に置くかを決める
 * @note 並べ始める所で作り、部品の前に Next(幅) を呼ぶ。ボタンなら Button を使えば幅も測る
 */
class FlowRow {
public:
    /** @brief 今の場所から右端までを並べる幅として覚える */
    FlowRow()
        : right_(ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x) {
    }

    /** @brief 幅 width の部品を置く前に呼ぶ（入るなら前の部品と同じ行にする） */
    void Next(float width) {
        if (!first_) {
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            if (ImGui::GetItemRectMax().x + spacing + width <= right_) {
                ImGui::SameLine();
            }
        }
        first_ = false;
    }

    /** @brief 文字の幅に合わせたボタンを、入る所に置く */
    bool Button(const char* label) {
        Next(ButtonWidth(label));
        return ImGui::Button(label);
    }

    /** @brief 文字から、ボタンの幅を測る（## 以降は数えない） */
    static float ButtonWidth(const char* label) {
        return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * kBothSides;
    }

private:
    static constexpr float kBothSides = 2.0f; ///< 左右の余白の2つ分
    float right_; ///< 並べられる右端（画面の座標）
    bool first_ = true; ///< まだ1つも置いていないか
};

} // namespace engine::editor
#endif // USE_IMGUI

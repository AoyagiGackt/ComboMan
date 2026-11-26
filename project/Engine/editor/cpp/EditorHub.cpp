/**
 * @file EditorHub.cpp
 * @brief エディタの一覧と開閉の頼みの実装
 */
#ifdef USE_IMGUI
#include "EditorHub.h"
#include "EditorUI.h"
#include <imgui.h>
#include <string>
#include <utility>

namespace engine::editor {
namespace {
/** @brief 開いているエディタの名前の色 */
constexpr ImVec4 kOpenColor = { 0.4f, 1.0f, 0.5f, 1.0f };
/** @brief 開閉のボタンの幅 */
constexpr float kToggleButtonWidth = 64.0f;
} // namespace

/** @brief 唯一のインスタンスを返す */
EditorHub* EditorHub::GetInstance() {
    static EditorHub sInstance;
    return &sInstance;
}

/** @brief このフレームの一覧に載せ、前のフレームで押された開閉の頼みがあれば受け取る */
bool EditorHub::Offer(const char* id, const char* title, const char* key, const char* help, bool open) {
    entries_.push_back({ id, title, key != nullptr ? key : "", help != nullptr ? help : "", open });
    // 頼みは1回受け取ったら消す
    return pendingToggles_.erase(id) > 0;
}

/** @brief 一覧を、開閉のボタン・名前とキー・説明の順で1行ずつ描く */
void EditorHub::DrawList() {
    if (entries_.empty()) {
        ImGui::TextDisabled("このシーンで使えるエディタはありません");
        return;
    }
    for (const Entry& entry : entries_) {
        ImGui::PushID(entry.id.c_str());
        // 開閉のボタンは次のフレームの Offer で受け取ってもらう
        if (ImGui::Button(entry.open ? "閉じる" : "開く", ImVec2(kToggleButtonWidth, 0.0f))) {
            nextToggles_.insert(entry.id);
        }
        // 名前（キーがあれば後ろに添える）は開いている時だけ色を変え、狭い時は折り返す
        ImGui::SameLine();
        const std::string title = entry.key.empty() ? entry.title : entry.title + "  (" + entry.key + ")";
        ImGui::PushStyleColor(ImGuiCol_Text, entry.open ? kOpenColor : ImGui::GetStyleColorVec4(ImGuiCol_Text));
        ImGui::TextWrapped("%s", title.c_str());
        ImGui::PopStyleColor();
        if (!entry.help.empty() && ImGui::IsItemHovered()) {
            graphics::editorUI::TooltipWrapped("%s", entry.help.c_str());
        }
        // 説明は名前の下に薄く折り返して出す
        if (!entry.help.empty()) {
            graphics::editorUI::TextDisabledWrapped("%s", entry.help.c_str());
        }
        ImGui::PopID();
    }
}

/** @brief 一覧を空にし、このフレームに押された頼みを次のフレームへ渡す（受け取られなかった古い頼みは捨てる） */
void EditorHub::EndFrame() {
    entries_.clear();
    pendingToggles_ = std::move(nextToggles_);
    nextToggles_.clear();
}

} // namespace engine::editor
#endif // USE_IMGUI

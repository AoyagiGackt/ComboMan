/**
 * @file EditorHub.cpp
 * @brief エディタの一覧と開閉の頼みの実装
 */
#ifdef USE_IMGUI
#include "EditorHub.h"
#include "EditorUI.h"
#include <imgui.h>
#include <algorithm>
#include <iterator>
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
    // どの場面で使えたかを覚えておく（ほかの場面にいる時に、どこへ行けば使えるかを出すため）
    auto known = std::find_if(known_.begin(), known_.end(), [id](const KnownEntry& entry) {
        return entry.id == id;
    });
    if (known == known_.end()) {
        known_.push_back({ id, title, {} });
        known = std::prev(known_.end());
    }
    if (!placeLabel_.empty() && std::find(known->places.begin(), known->places.end(), placeLabel_) == known->places.end()) {
        known->places.push_back(placeLabel_);
    }
    // 頼みは1回受け取ったら消す
    return pendingToggles_.erase(id) > 0;
}

/** @brief 一覧を、開閉のボタン・名前とキー・説明の順で1行ずつ描く */
void EditorHub::DrawList() {
    if (entries_.empty()) {
        ImGui::TextDisabled("このシーンで使えるエディタはありません");
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
    DrawUnavailableList();
}

/** @brief 前に知らせがあったのに今の場面に無いエディタを、使える場面の名前と一緒に薄く出す */
void EditorHub::DrawUnavailableList() const {
    bool headerShown = false;
    for (const KnownEntry& known : known_) {
        const bool offered = std::any_of(entries_.begin(), entries_.end(), [&known](const Entry& entry) {
            return entry.id == known.id;
        });
        if (offered) {
            continue;
        }
        if (!headerShown) {
            graphics::editorUI::TextDisabledWrapped("ほかの場面で使えるエディタ（この起動で入った場面の物だけ出ます）");
            headerShown = true;
        }
        // 使える場面の名前を「、」でつなぐ
        std::string places;
        for (const std::string& place : known.places) {
            places += (places.empty() ? "" : "、") + place;
        }
        graphics::editorUI::TextDisabledWrapped("・%s（%sで使えます）", known.title.c_str(), places.empty() ? "ほかの場面" : places.c_str());
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

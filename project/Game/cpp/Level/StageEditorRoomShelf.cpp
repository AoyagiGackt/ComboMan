/**
 * @file StageEditorRoomShelf.cpp
 * @brief 部品の棚の部屋のひな形（選んだ物をまとめて保存し、ほかのステージでも押すだけで置ける）
 * @note クラスは StageEditor のまま。保存と置き方は StageEditorPrefabService.cpp の SaveSelectedPrefab / InstantiatePrefab を使う
 */
#include "StageEditor.h"
#ifdef USE_IMGUI
#include "EditorFlowRow.h"
#include "EditorUI.h"
#include "StageEditorPrefabService.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <imgui.h>
#include <string>
#include <vector>

namespace engine::game {
namespace {
constexpr const char* kRoomDirectory = "Resources/Prefabs"; ///< 部屋のひな形を置くフォルダ
constexpr size_t kRoomLabelCapacity = 96; ///< ボタンの文字の入れ物の大きさ

/** @brief 棚に並べる部屋のひな形の1つ */
struct RoomEntry {
    std::string name; ///< ひな形の名前（ファイル名から .json を除いた物）
    int partCount = 0; ///< 中の配置物の数
};

/** @brief 部屋のひな形の一覧（保存した時と、更新を押した時に読み直す） */
std::vector<RoomEntry>& Rooms() {
    static std::vector<RoomEntry> sRooms;
    return sRooms;
}

/** @brief 一覧を読み直したことがあるか（初めて棚を開いた時に読む） */
bool& RoomsLoaded() {
    static bool sLoaded = false;
    return sLoaded;
}

/** @brief フォルダの部屋のひな形を名前の順に読み直す */
void ReloadRooms() {
    std::vector<RoomEntry>& rooms = Rooms();
    rooms.clear();
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(kRoomDirectory, error)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }
        const std::string name = entry.path().stem().string();
        rooms.push_back({ name, static_cast<int>(StageEditorPrefabService::Load(name).size()) });
    }
    std::sort(rooms.begin(), rooms.end(), [](const RoomEntry& a, const RoomEntry& b) {
        return a.name < b.name;
    });
    RoomsLoaded() = true;
}
} // namespace

/** @brief 部品の棚に、部屋のひな形の保存と置く操作を出す */
void StageEditor::RenderRoomShelf() {
    if (!ImGui::CollapsingHeader("部屋のひな形")) {
        return;
    }
    if (!RoomsLoaded()) {
        ReloadRooms();
    }
    graphics::editorUI::TextDisabledWrapped("敵の部屋や足場の並びを複数選んで保存すると、ほかのステージでも押すだけで同じ並びを置けます（画面の中央に、床の上へ乗せて置きます）。");

    // 選んだ物をまとめて保存する
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##roomName", "部屋の名前（半角英数字）", prefabName_, sizeof(prefabName_));
    const bool hasSelection = selKind_ == SelKind::kObject && selIndex_ >= 0;
    ImGui::BeginDisabled(!hasSelection || prefabName_[0] == '\0' || playTestMode_);
    if (ImGui::Button("選んだ物を部屋として保存", ImVec2(-1.0f, 0.0f))) {
        SaveSelectedPrefab();
        ReloadRooms();
    }
    ImGui::EndDisabled();
    if (!hasSelection) {
        graphics::editorUI::TextDisabledWrapped("Ctrl を押しながら配置物を選ぶと、まとめて保存できます。");
    }

    // 保存してある部屋を並べ、押すと画面の中央に置く（入りきる分だけ横に並べる）
    ImGui::SeparatorText("置く");
    if (Rooms().empty()) {
        ImGui::TextDisabled("まだ保存した部屋がありません");
    }
    ImGui::BeginDisabled(playTestMode_);
    engine::editor::FlowRow row;
    for (size_t index = 0; index < Rooms().size(); ++index) {
        const RoomEntry& room = Rooms()[index];
        char label[kRoomLabelCapacity];
        std::snprintf(label, sizeof(label), "%s（%d 個）", room.name.c_str(), room.partCount);
        ImGui::PushID(static_cast<int>(index));
        if (row.Button(label)) {
            strncpy_s(prefabName_, room.name.c_str(), _TRUNCATE);
            InstantiatePrefab();
        }
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    if (ImGui::SmallButton("一覧を更新")) {
        ReloadRooms();
    }
}

} // namespace engine::game
#endif

/**
 * @file StageEditorLevelPanel.cpp
 * @brief ステージエディタの このレベル タブ（説明書き・空・常駐グラフ・プレイヤーの出る位置・中身の数）と、グラフの選び方の共通部品
 */
#include "StageEditor.h"
#ifdef USE_IMGUI
#include "GraphEditor.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <imgui.h>
#endif

#ifdef USE_IMGUI

namespace engine::game {
namespace {
constexpr const char* kSkyFolder = "Resources/SkyDome/"; // 空の画像を置く場所
constexpr const char* kSkyImageExtension = ".png"; // 空の画像の拡張子
constexpr const char* kDefaultSkyFile = "skySphere.png"; // 既定の空（sky が空の時に使う画像。一覧には出さない）
constexpr const char* kGraphFolder = "Resources/Graphs/"; // グラフを置く場所
constexpr const char* kGraphExtension = ".json"; // グラフの拡張子
constexpr const char* kResidentGraphSuffix = "main"; // 常駐グラフを新しく作る時の名前の後ろ
constexpr const char* kNewLevelStem = "level"; // レベルがまだ保存されていない時の名前の頭
constexpr int kFirstDuplicateNumber = 2; // 同じ名前のグラフがある時に後ろへ付け始める番号
constexpr float kCommentBoxLines = 4.0f; // 説明書きの欄の高さ（行の数）
constexpr float kSpawnDragStep = 0.1f; // 出る位置をドラッグで動かす細かさ

/** @brief フォルダの中から指定の拡張子のファイルの場所を名前順に集める（skipFile と同じ名前は除く） */
std::vector<std::string> CollectFiles(const char* folder, const char* extension, const char* skipFile) {
    std::vector<std::string> files;
    std::error_code error;
    for (const auto& file : std::filesystem::directory_iterator(folder, error)) {
        const std::filesystem::path& path = file.path();
        if (!file.is_regular_file(error) || path.extension() != extension) {
            continue;
        }
        if (skipFile != nullptr && path.filename() == skipFile) {
            continue;
        }
        files.push_back(std::string(folder) + path.filename().string());
    }
    std::sort(files.begin(), files.end());
    return files;
}

/** @brief ファイルの場所から名前だけを取り出す */
std::string FileNameOf(const std::string& path) {
    return std::filesystem::path(path).filename().string();
}
} // namespace

/** @brief レベル全体の設定（説明書き・空・常駐グラフ・プレイヤーの出る位置・中身の数）をまとめて編集する */
void StageEditor::RenderLevelPanel() {
    if (!ImGui::BeginTabItem("このレベル", &showLevelPanel_)) {
        return;
    }
    ImGui::TextWrapped("%s", levelPath_.empty() ? "まだ保存していないレベル" : levelPath_.c_str());

    // 説明書き: レベルの JSON の _comment に保存される（ゲームでは使わない）
    ImGui::SeparatorText("説明書き");
    const bool commentChanged = ImGui::InputTextMultiline("##levelComment", commentBuffer_, sizeof(commentBuffer_),
        ImVec2(-1.0f, ImGui::GetTextLineHeight() * kCommentBoxLines));
    if (CaptureItemUndo(commentChanged)) {
        comment_ = commentBuffer_;
    }

    RenderSkyPicker();
    RenderGameModePanel();

    // 常駐グラフはここでも選べる（フラグ起動グラフはグラフのタブで編集）
    ImGui::SeparatorText("常駐グラフ");
    RenderResidentGraphPicker();
    ImGui::TextDisabled("フラグ起動グラフ %d 件", static_cast<int>(flagGraphs_.size()));
    ImGui::SameLine();
    if (ImGui::SmallButton("グラフのタブを開く")) {
        showGraphPanel_ = true;
    }

    // プレイヤーの出る位置: 数値で直すか、今見ている所へ置く
    ImGui::SeparatorText("プレイヤーの出る位置");
    ImGui::SetNextItemWidth(-1.0f);
    CaptureItemUndo(ImGui::DragFloat3("##playerSpawn", &playerSpawn_.x, kSpawnDragStep));
    if (ImGui::Button("ここへカメラ")) {
        FocusCameraOn(playerSpawn_);
    }
    ImGui::SameLine();
    if (ImGui::Button("画面の中央にする")) {
        RecordUndoSnapshotNow();
        const Vector3 center = ViewCenterOnGround();
        playerSpawn_.x = SnapValue(center.x);
        playerSpawn_.y = SnapValue(center.y);
        statusMessage_ = "プレイヤーの出る位置を画面の中央にしました";
        statusTimer_ = kStatusShortSeconds;
    }

    // 中身の数（敵の数え漏れや置きすぎに気づけるように）
    ImGui::SeparatorText("中身");
    const int enemyCount = static_cast<int>(std::count_if(objects_.begin(), objects_.end(), [](const ObjectEntry& entry) {
        return entry.EnemyBody() != nullptr;
    }));
    ImGui::Text("配置物 %d / 敵 %d / トリガー %d / チェックポイント %d", static_cast<int>(objects_.size()), enemyCount,
        static_cast<int>(triggers_.size()), static_cast<int>(checkpoints_.size()));
    ImGui::EndTabItem();
}

/** @brief 空の画像を一覧から選ぶ欄（選ぶとすぐ画面の空に映る） */
void StageEditor::RenderSkyPicker() {
    ImGui::SeparatorText("空");
    // 選んだ画像へ変える（元に戻せるよう、変える前に履歴を取る）
    auto choose = [this](const std::string& sky) {
        if (sky == sky_) {
            return;
        }
        RecordUndoSnapshotNow();
        SetSky(sky);
        statusMessage_ = sky.empty() ? "空を既定に戻しました" : "空を " + sky + " にしました";
        statusTimer_ = kStatusShortSeconds;
    };
    const std::string current = sky_.empty() ? std::string("既定の空") : FileNameOf(sky_);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##sky", current.c_str())) {
        if (ImGui::Selectable("既定の空", sky_.empty())) {
            choose(std::string());
        }
        for (const std::string& image : CollectFiles(kSkyFolder, kSkyImageExtension, kDefaultSkyFile)) {
            if (ImGui::Selectable(FileNameOf(image).c_str(), image == sky_)) {
                choose(image);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::TextDisabled("空の画像は repos/tools/gen_sky.js で作る");
}

/** @brief グラフを一覧から選ぶ欄（allowNone なら なし も選べる）。選び直したら true */
bool StageEditor::RenderGraphPathPicker(const char* id, std::string& path, bool allowNone) {
    bool changed = false;
    const std::string current = path.empty() ? std::string("なし") : FileNameOf(path);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo(id, current.c_str())) {
        if (allowNone && ImGui::Selectable("なし", path.empty())) {
            changed = !path.empty();
            path.clear();
        }
        for (const std::string& graph : CollectFiles(kGraphFolder, kGraphExtension, nullptr)) {
            if (ImGui::Selectable(FileNameOf(graph).c_str(), graph == path) && graph != path) {
                path = graph;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

/** @brief 常駐グラフを選ぶ欄と、開く・新しく作るのボタン（このレベルとグラフのタブで共通） */
void StageEditor::RenderResidentGraphPicker() {
    std::string picked = graphPath_;
    if (RenderGraphPathPicker("##residentGraph", picked, true)) {
        RecordUndoSnapshotNow();
        graphPath_ = picked;
        strncpy_s(graphPathBuffer_, graphPath_.c_str(), _TRUNCATE);
    }
    // 選んだグラフをノードエディタで開く（無ければ押せない）
    ImGui::BeginDisabled(graphPath_.empty());
    if (ImGui::Button("ノードエディタで開く##resident")) {
        GraphEditor::GetInstance()->OpenAndShow(graphPath_);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    // レベルの名前から空いている名前を決めて、空のグラフをノードエディタで開く（保存はノードエディタで行う）
    if (ImGui::Button("新しく作る##resident")) {
        RecordUndoSnapshotNow();
        graphPath_ = MakeNewGraphPath(kResidentGraphSuffix);
        strncpy_s(graphPathBuffer_, graphPath_.c_str(), _TRUNCATE);
        GraphEditor::GetInstance()->OpenAndShow(graphPath_);
        statusMessage_ = "新しいグラフ " + graphPath_ + " を開きました。ノードエディタで保存してください";
        statusTimer_ = kStatusLongSeconds;
    }
}

/** @brief レベルの名前と suffix から、まだ使われていないグラフの場所を作る（level01_main.json のように） */
std::string StageEditor::MakeNewGraphPath(const std::string& suffix) const {
    const std::string stem = levelPath_.empty() ? std::string(kNewLevelStem) : std::filesystem::path(levelPath_).stem().string();
    const std::string base = std::string(kGraphFolder) + stem + "_" + suffix;
    std::string path = base + kGraphExtension;
    std::error_code error;
    for (int number = kFirstDuplicateNumber; std::filesystem::exists(path, error); ++number) {
        path = base + "_" + std::to_string(number) + kGraphExtension;
    }
    return path;
}

} // namespace engine::game

#endif

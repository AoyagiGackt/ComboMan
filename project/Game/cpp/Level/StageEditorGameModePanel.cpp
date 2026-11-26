/**
 * @file StageEditorGameModePanel.cpp
 * @brief ステージエディタのゲームの型を選ぶ欄・道の編集・型に合わせた点検と部品の棚・中央ビューの道の表示
 * @note クラスは StageEditor のまま、型と道に関わる定義だけをここに置く
 */
#include "StageEditor.h"
#ifdef USE_IMGUI
#include "DiagnosticsDraw.h"
#include "EditorFlowRow.h"
#include "EditorUI.h"
#include "EditorUiStyle.h"
#include "GameModeLibrary.h"
#include "Input.h"
#include "PathTrack.h"
#include "StringUtility.h"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <imgui.h>
#include <string>

namespace engine::game {
namespace {
constexpr float kPathExtendStep = 8.0f; ///< 最後の点の先に足す時に離す距離（+X 方向）
constexpr float kPathDrawStep = 0.5f; ///< 道の線を描く時に区切る道のり
constexpr float kPathPointCrossSize = 0.6f; ///< 道の点に描く印の大きさ（ワールド単位）
constexpr float kPathSpeedDragStep = 0.05f; ///< 速さの入力のドラッグ幅
constexpr float kPathPositionDragStep = 0.1f; ///< 位置の入力のドラッグ幅
constexpr float kMaxPathSpeed = 40.0f; ///< 速さの上限（ワールド単位/秒）
constexpr float kPathLabelOffsetX = 6.0f; ///< 点の番号を印からずらす量（ピクセル）
constexpr float kPathLabelOffsetY = -16.0f; ///< 点の番号を印からずらす量（ピクセル）
constexpr ImU32 kPathLineColor = IM_COL32(120, 200, 255, 255); ///< 道の線の色
constexpr ImU32 kPathPointColor = IM_COL32(255, 255, 255, 255); ///< 道の点の色
constexpr ImU32 kPathSelectedColor = IM_COL32(255, 200, 60, 255); ///< 選んでいる点の色
constexpr float kMidpointRatio = 0.5f; ///< 2つの点のちょうど間
constexpr size_t kPathLabelCapacity = 48; ///< 点の一覧の1行の文字の入れ物の大きさ
constexpr float kPathPickRadiusPx = 12.0f; ///< 道の点を掴める、マウスとの距離（ピクセル）
constexpr const char* kLevelTemplateDirectory = "Resources/Levels/Templates"; ///< お手本のステージを置くフォルダ
constexpr const char* kLevelsDirectory = "Resources/Levels"; ///< レベルを置くフォルダ
constexpr const char* kGraphsDirectory = "Resources/Graphs"; ///< グラフを置くフォルダ
constexpr float kHalfExtentRatio = 0.5f; ///< 大きさから半分の幅を出す割合

/** @brief お手本のステージの一覧の1行 */
struct LevelTemplate {
    std::string path; ///< お手本のファイル
    std::string label; ///< ボタンに出す名前（ファイル名から sample_ を除いた物）
    std::string comment; ///< マウスを乗せた時に出す説明（_comment）
};

/** @brief お手本のステージの一覧（一覧を開いた時にフォルダを読み直す） */
std::vector<LevelTemplate>& LevelTemplates() {
    static std::vector<LevelTemplate> sTemplates;
    return sTemplates;
}

/** @brief フォルダのお手本のステージを読み直す（ファイル名の順） */
void ReloadLevelTemplates() {
    std::vector<LevelTemplate>& templates = LevelTemplates();
    templates.clear();
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(kLevelTemplateDirectory, error)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }
        const std::string path = entry.path().generic_string();
        const LevelData data = levelLoader::Load(path);
        const std::string mode = GameModeLibrary::GetInstance()->Find(data.gameMode).name;
        templates.push_back({ path, mode, data.comment });
    }
    std::sort(templates.begin(), templates.end(), [](const LevelTemplate& a, const LevelTemplate& b) {
        return a.path < b.path;
    });
}

/** @brief レベルの名前に使える文字だけか（半角英数字と _、空は不可） */
bool IsValidLevelName(const std::string& name) {
    return !name.empty() && std::all_of(name.begin(), name.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
    });
}

/** @brief 型の表から、この id の型が本当にあるか */
bool ModeExists(const std::string& id) {
    const auto& modes = GameModeLibrary::GetInstance()->All();
    return std::any_of(modes.begin(), modes.end(), [&id](const GameModeDesc& mode) {
        return mode.id == id;
    });
}

/** @brief レベルの道の点から、なめらかな道を作る */
PathTrack BuildTrack(const std::vector<PathPointDesc>& points) {
    std::vector<PathTrack::Point> trackPoints;
    trackPoints.reserve(points.size());
    for (const PathPointDesc& point : points) {
        trackPoints.push_back({ point.position, point.speed });
    }
    PathTrack track;
    track.Build(trackPoints);
    return track;
}
} // namespace

/** @brief ゲームの型を選ぶ欄と、道の点の一覧 */
void StageEditor::RenderGameModePanel() {
    ImGui::SeparatorText("ゲームの型");
    GameModeLibrary* library = GameModeLibrary::GetInstance();
    // 下の型を作る欄で保存すると型の一覧が読み直されるので、参照ではなく写しを持つ
    const GameModeDesc mode = library->Find(gameMode_);
    // 型を変える（元に戻せるよう、変える前に履歴を取る。既定の型は書かずに空にしておく）
    auto choose = [this](const std::string& id) {
        const std::string stored = id == GameModeLibrary::kDefaultId ? std::string() : id;
        if (stored == gameMode_) {
            return;
        }
        RecordUndoSnapshotNow();
        gameMode_ = stored;
        statusMessage_ = "ゲームの型を変えました: " + GameModeLibrary::GetInstance()->Find(gameMode_).name;
        statusTimer_ = kStatusShortSeconds;
    };
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##gameMode", mode.name.c_str())) {
        for (const GameModeDesc& candidate : library->All()) {
            if (ImGui::Selectable(candidate.name.c_str(), &candidate == &mode)) {
                choose(candidate.id);
            }
            if (ImGui::IsItemHovered() && !candidate.description.empty()) {
                graphics::editorUI::TooltipWrapped("%s", candidate.description.c_str());
            }
        }
        ImGui::EndCombo();
    }
    // 型の説明と作り方のこつ
    if (!mode.description.empty()) {
        graphics::editorUI::TextDisabledWrapped("%s", mode.description.c_str());
    }
    for (const std::string& tip : mode.tips) {
        graphics::editorUI::BulletWrapped("%s", tip.c_str());
    }
    // この型の操作（{操作名} を今のキーの名前に置き換えて出す）
    if (!mode.controls.empty()) {
        ImGui::SeparatorText("この型の操作");
        for (const GameModeControl& control : mode.controls) {
            const std::string keys = input_ != nullptr
                ? StringUtility::ConvertString(input_->ExpandPrompts(StringUtility::ConvertString(control.keys)))
                : control.keys;
            graphics::editorUI::BulletWrapped("%s  %s", keys.c_str(), control.label.c_str());
        }
        graphics::editorUI::TextDisabledWrapped("本編では、この型を初めて遊ぶ時に操作説明として出ます。");
    }
    RenderGameModeChecks();
    RenderLevelTemplates();
    RenderGameModeMaker();

    // 道を使わない型では、残っている道を消せるようにだけしておく
    if (!mode.path.follow) {
        if (!path_.empty()) {
            ImGui::TextDisabled("この型は道を使いません（道の点 %d 個は保存されたままです）", static_cast<int>(path_.size()));
            if (graphics::editorUI::ConfirmButton("道を消す", "このレベルの道の点をすべて消しますか？（元に戻せます）")) {
                RecordUndoSnapshotNow();
                path_.clear();
                selectedPathPoint_ = -1;
            }
        }
        return;
    }

    ImGui::SeparatorText("道");
    const PathTrack track = BuildTrack(path_);
    ImGui::Text("点 %d 個  全長 %.1f", static_cast<int>(path_.size()), track.TotalLength());
    graphics::editorUI::TextDisabledWrapped("プレイヤーとカメラは、この道を点の速さで自動に進みます。終わりに着くと %s のフラグが立ちます。",
        mode.path.endFlag.c_str());

    graphics::editorUI::TextDisabledWrapped("点は中央のビューでドラッグしても動かせます。");

    // 点を足す・挟む・消す（どれも元に戻せる。ボタンは入りきる分だけ横に並べる）
    ImGui::BeginDisabled(playTestMode_);
    engine::editor::FlowRow pathRow;
    if (pathRow.Button("画面中央に点を足す")) {
        RecordUndoSnapshotNow();
        path_.push_back({ ViewCenterOnGround(), path_.empty() ? PathPointDesc { }.speed : path_.back().speed });
        selectedPathPoint_ = static_cast<int>(path_.size()) - 1;
    }
    ImGui::BeginDisabled(path_.empty());
    if (pathRow.Button("最後の点の先に足す")) {
        RecordUndoSnapshotNow();
        PathPointDesc next = path_.back();
        next.position.x += kPathExtendStep;
        path_.push_back(next);
        selectedPathPoint_ = static_cast<int>(path_.size()) - 1;
    }
    ImGui::EndDisabled();
    const bool hasSelection = selectedPathPoint_ >= 0 && selectedPathPoint_ < static_cast<int>(path_.size());
    ImGui::BeginDisabled(!hasSelection);
    if (pathRow.Button("選んだ点の後ろに挟む")) {
        RecordUndoSnapshotNow();
        const size_t index = static_cast<size_t>(selectedPathPoint_);
        // 次の点があればその間に、無ければ先へ離して置く
        PathPointDesc inserted = path_[index];
        if (index + 1 < path_.size()) {
            inserted.position = Lerp(path_[index].position, path_[index + 1].position, kMidpointRatio);
        } else {
            inserted.position.x += kPathExtendStep;
        }
        path_.insert(path_.begin() + static_cast<std::ptrdiff_t>(index + 1), inserted);
        selectedPathPoint_ = static_cast<int>(index + 1);
    }
    if (pathRow.Button("選んだ点を消す")) {
        RecordUndoSnapshotNow();
        path_.erase(path_.begin() + selectedPathPoint_);
        selectedPathPoint_ = path_.empty() ? -1 : (std::min)(selectedPathPoint_, static_cast<int>(path_.size()) - 1);
    }
    if (pathRow.Button("この点へ移動") && hasSelection) {
        FocusCameraOn(path_[static_cast<size_t>(selectedPathPoint_)].position);
    }
    ImGui::EndDisabled();

    // 点の一覧。選んだ点だけ位置と速さを直せる
    for (int index = 0; index < static_cast<int>(path_.size()); ++index) {
        ImGui::PushID(index);
        PathPointDesc& point = path_[static_cast<size_t>(index)];
        char label[kPathLabelCapacity];
        std::snprintf(label, sizeof(label), "点 %d  速さ %.2f", index + 1, point.speed);
        if (ImGui::Selectable(label, index == selectedPathPoint_)) {
            selectedPathPoint_ = index;
        }
        if (index == selectedPathPoint_) {
            ImGui::Indent();
            CaptureItemUndo(ImGui::DragFloat3("位置", &point.position.x, kPathPositionDragStep));
            CaptureItemUndo(ImGui::DragFloat("速さ", &point.speed, kPathSpeedDragStep, 0.0f, kMaxPathSpeed, "%.2f /秒"));
            if (ImGui::IsItemHovered()) {
                graphics::editorUI::TooltipWrapped("この点を通る時の進む速さです。前後の点の間はなめらかに変わります");
            }
            ImGui::Unindent();
        }
        ImGui::PopID();
    }
    ImGui::EndDisabled();
}

/** @brief 選んでいる型でよく使う部品を、押すと画面中央に置けるボタンで並べる */
void StageEditor::RenderRecommendedParts() {
    const GameModeDesc& mode = GameModeLibrary::GetInstance()->Find(gameMode_);
    if (mode.recommendedKinds.empty()) {
        return;
    }
    ImGui::SeparatorText(("この型でよく使う部品（" + mode.name + "）").c_str());
    ImGui::BeginDisabled(playTestMode_);
    // 入りきる分だけ横に並べ、入らなければ次の行へ回す
    engine::editor::FlowRow row;
    for (size_t index = 0; index < mode.recommendedKinds.size(); ++index) {
        const GameModeKind& part = mode.recommendedKinds[index];
        ImGui::PushID(static_cast<int>(index));
        if (row.Button(part.label.c_str())) {
            AddObjectAt(part.kind, ViewCenterOnGround());
        }
        ImGui::PopID();
    }
    ImGui::EndDisabled();
}

/** @brief 中央ビューに道の線と点を描く（道を使わない型でも、点が残っていれば薄く知らせるため描く） */
void StageEditor::DrawPathOverlay() const {
    if (path_.empty()) {
        return;
    }
    // なめらかにした線を細かく区切って描く
    const PathTrack track = BuildTrack(path_);
    if (track.IsValid()) {
        Vector3 previous = track.PositionAt(0.0f);
        for (float distance = kPathDrawStep; distance < track.TotalLength() + kPathDrawStep; distance += kPathDrawStep) {
            const Vector3 current = track.PositionAt(distance);
            diagnosticsDraw::DrawLine(previous, current, kPathLineColor);
            previous = current;
        }
    }
    // 点の印と番号
    ImDrawList* draw = diagnosticsDraw::GetDrawList();
    for (int index = 0; index < static_cast<int>(path_.size()); ++index) {
        const Vector3& position = path_[static_cast<size_t>(index)].position;
        const ImU32 color = index == selectedPathPoint_ ? kPathSelectedColor : kPathPointColor;
        diagnosticsDraw::DrawCross(position, kPathPointCrossSize, color);
        ImVec2 screen;
        if (draw != nullptr && diagnosticsDraw::WorldToScreen(position, screen)) {
            const std::string number = std::to_string(index + 1);
            draw->AddText({ screen.x + kPathLabelOffsetX, screen.y + kPathLabelOffsetY }, color, number.c_str());
        }
    }
}

/** @brief ゲームの型に合っていない所を普通の言葉で返す（ValidateLevel 用に、文だけを取り出す） */
std::vector<std::string> StageEditor::CollectGameModeIssues() const {
    std::vector<std::string> issues;
    for (GameModeCheck& check : CollectGameModeChecks()) {
        issues.push_back(std::move(check.text));
    }
    return issues;
}

/** @brief ゲームの型に合っていない所を、直し方と一緒に返す */
std::vector<GameModeCheck> StageEditor::CollectGameModeChecks() const {
    std::vector<GameModeCheck> checks;
    // 書いてある型が無い時は、横スクロールとして動くことを知らせる
    if (!gameMode_.empty() && !ModeExists(gameMode_)) {
        checks.push_back({ "ゲームの型が見つかりません: " + gameMode_ + "（横スクロールとして動きます）", GameModeFix::kResetMode });
    }
    const GameModeDesc& mode = GameModeLibrary::GetInstance()->Find(gameMode_);
    // 型に必ず要る部品が置かれているか
    for (const GameModeKind& required : mode.requiredKinds) {
        const bool placed = std::any_of(objects_.begin(), objects_.end(), [&required](const ObjectEntry& entry) {
            return entry.desc.kind == required.kind;
        });
        if (!placed) {
            checks.push_back({ mode.name + " には " + required.label + " が必要ですが、まだ置かれていません", GameModeFix::kPlaceKind, required.kind });
        }
    }
    // 奥行きにも動く型では、動ける幅より奥行きの狭い床しか無いと、奥へ動いた時に宙に浮いて見える
    if (mode.player.depthMovement) {
        const bool wideFloor = std::any_of(objects_.begin(), objects_.end(), [&mode](const ObjectEntry& entry) {
            const float halfDepth = entry.desc.scale.z * kHalfExtentRatio;
            return entry.desc.solid && entry.desc.position.z - halfDepth <= mode.player.depthMin
                && entry.desc.position.z + halfDepth >= mode.player.depthMax;
        });
        if (!wideFloor) {
            checks.push_back({ mode.name + " は奥行きにも動けます。床の奥行きが " + std::to_string(static_cast<int>(mode.player.depthMax - mode.player.depthMin))
                + " より狭いので、奥へ動いた時に宙に浮いて見えます", GameModeFix::kWidenFloor });
        }
    }
    // 道に沿って進む型の道
    if (mode.path.follow) {
        if (path_.size() < 2) {
            checks.push_back({ mode.name + " は道に沿って進みます。道の点を2つ以上置いてください", GameModeFix::kAddPathPoints });
        }
        for (size_t index = 0; index < path_.size(); ++index) {
            if (path_[index].speed <= 0.0f) {
                checks.push_back({ "道の点 " + std::to_string(index + 1) + " の速さが0です（そこで止まって先へ進めません）",
                    GameModeFix::kFixPathSpeed, std::string(), static_cast<int>(index) });
            }
        }
    }
    return checks;
}

/** @brief 型の点検の一覧を、直すボタン付きで描く（問題が無ければ1行だけ） */
void StageEditor::RenderGameModeChecks() {
    ImGui::SeparatorText("この型の点検");
    const std::vector<GameModeCheck> checks = CollectGameModeChecks();
    if (checks.empty()) {
        ImGui::TextColored(engine::editor::uiStyle::kOkColor, "この型に合っています");
        return;
    }
    // 直し方の説明（ボタンにマウスを乗せた時に出す）
    const auto fixHelp = [](GameModeFix fix) {
        switch (fix) {
        case GameModeFix::kResetMode: return "型を横スクロールに戻します";
        case GameModeFix::kPlaceKind: return "足りない物を画面の中央に置きます";
        case GameModeFix::kWidenFloor: return "当たり判定のある物の奥行きを、動ける幅まで広げます";
        case GameModeFix::kAddPathPoints: return "画面の中央から右へ、道の点を足します";
        case GameModeFix::kFixPathSpeed: return "この点の速さを既定の速さにします";
        default: return "";
        }
    };
    ImGui::BeginDisabled(playTestMode_);
    for (size_t index = 0; index < checks.size(); ++index) {
        const GameModeCheck& check = checks[index];
        ImGui::PushID(static_cast<int>(index));
        // 直せる物は先にボタンを置き、文はその横から折り返す
        if (check.fix != GameModeFix::kNone) {
            const bool pressed = ImGui::SmallButton("直す");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                graphics::editorUI::TooltipWrapped("%s（元に戻せます）", fixHelp(check.fix));
            }
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, engine::editor::uiStyle::kWarningColor);
            ImGui::TextWrapped("%s", check.text.c_str());
            ImGui::PopStyleColor();
            if (pressed) {
                ApplyGameModeFix(check);
                ImGui::PopID();
                break;
            }
        } else {
            graphics::editorUI::BulletWrapped("%s", check.text.c_str());
        }
        ImGui::PopID();
    }
    ImGui::EndDisabled();
}

/** @brief 点検の1件を直す（元に戻せるよう、直す前に履歴を取る） */
void StageEditor::ApplyGameModeFix(const GameModeCheck& check) {
    const GameModeDesc& mode = GameModeLibrary::GetInstance()->Find(gameMode_);
    switch (check.fix) {
    case GameModeFix::kResetMode:
        RecordUndoSnapshotNow();
        gameMode_.clear();
        break;
    case GameModeFix::kPlaceKind:
        // 置く操作は自分で履歴を取る
        AddObjectAt(check.kind, ViewCenterOnGround());
        break;
    case GameModeFix::kWidenFloor: {
        // 当たり判定のある物を、動ける奥行きの真ん中に置いて、その幅まで広げる
        RecordUndoSnapshotNow();
        const float depth = mode.player.depthMax - mode.player.depthMin;
        const float center = (mode.player.depthMax + mode.player.depthMin) * kHalfExtentRatio;
        for (ObjectEntry& entry : objects_) {
            if (entry.desc.solid && entry.desc.scale.z < depth) {
                entry.desc.scale.z = depth;
                entry.desc.position.z = center;
                RegenerateInstances(entry);
            }
        }
        break;
    }
    case GameModeFix::kAddPathPoints:
        RecordUndoSnapshotNow();
        if (path_.empty()) {
            path_.push_back({ ViewCenterOnGround(), PathPointDesc { }.speed });
        }
        while (path_.size() < 2) {
            PathPointDesc next = path_.back();
            next.position.x += kPathExtendStep;
            path_.push_back(next);
        }
        break;
    case GameModeFix::kFixPathSpeed:
        if (check.index >= 0 && check.index < static_cast<int>(path_.size())) {
            RecordUndoSnapshotNow();
            path_[static_cast<size_t>(check.index)].speed = PathPointDesc { }.speed;
        }
        break;
    default:
        return;
    }
    statusMessage_ = "直しました（Ctrl+Z で元に戻せます）";
    statusTimer_ = kStatusShortSeconds;
}

/** @brief お手本のステージを名前を付けて写し、新しいレベルとして開く欄 */
void StageEditor::RenderLevelTemplates() {
    // 開いた時にフォルダを読み直す（閉じている間は何もしない）
    const bool open = ImGui::CollapsingHeader("お手本のステージから新しいレベルを作る");
    if (ImGui::IsItemToggledOpen() || (open && LevelTemplates().empty())) {
        ReloadLevelTemplates();
    }
    if (!open) {
        return;
    }
    graphics::editorUI::TextDisabledWrapped("型ごとのお手本を写して、新しいレベルとして開きます。クリアのグラフも一緒に写すので、すぐに遊んで確かめられます。");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##templateLevelName", "新しいレベルの名前（半角英数字と _）", templateLevelName_, sizeof(templateLevelName_));
    const std::string name = templateLevelName_;
    const bool validName = IsValidLevelName(name);
    if (!validName) {
        ImGui::TextColored(engine::editor::uiStyle::kWarningColor, "名前は半角英数字と _ だけにしてください");
    }
    if (LevelTemplates().empty()) {
        ImGui::TextDisabled("%s にお手本がありません", kLevelTemplateDirectory);
        return;
    }
    // お手本ごとのボタン（入りきる分だけ横に並べる）
    ImGui::BeginDisabled(!validName || playTestMode_);
    engine::editor::FlowRow row;
    for (size_t index = 0; index < LevelTemplates().size(); ++index) {
        const LevelTemplate& entry = LevelTemplates()[index];
        ImGui::PushID(static_cast<int>(index));
        if (row.Button(entry.label.c_str())) {
            CreateLevelFromTemplate(entry.path, name);
        }
        if (!entry.comment.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            graphics::editorUI::TooltipWrapped("%s", entry.comment.c_str());
        }
        ImGui::PopID();
    }
    ImGui::EndDisabled();
}

/** @brief お手本のステージから新しいレベルを作って開く（クリアのグラフも名前を変えて写す） */
bool StageEditor::CreateLevelFromTemplate(const std::string& templatePath, const std::string& name) {
    // 失敗した理由をステータスに出して false を返す
    const auto fail = [this](const std::string& message) {
        statusMessage_ = message;
        statusTimer_ = kStatusLongSeconds;
        return false;
    };
    if (dirty_) {
        return fail("保存していない変更があります。先に保存してから作ってください");
    }
    if (!IsValidLevelName(name)) {
        return fail("名前は半角英数字と _ だけにしてください");
    }
    // 同じ名前のレベルやグラフがあれば上書きしない
    const std::string levelPath = std::string(kLevelsDirectory) + "/" + name + ".json";
    const std::string graphPath = std::string(kGraphsDirectory) + "/" + name + "_main.json";
    std::error_code error;
    if (std::filesystem::exists(levelPath, error)) {
        return fail("同じ名前のレベルがあります: " + levelPath);
    }
    LevelData data = levelLoader::Load(templatePath);
    if (data.objects.empty()) {
        return fail("お手本を読み込めませんでした: " + templatePath);
    }
    // クリアのグラフは新しいレベル用に写し、お手本のグラフを書き換えないようにする
    if (!data.graphPath.empty()) {
        if (std::filesystem::exists(graphPath, error)) {
            return fail("同じ名前のグラフがあります: " + graphPath);
        }
        std::filesystem::copy_file(data.graphPath, graphPath, error);
        if (error) {
            return fail("グラフを写せませんでした: " + data.graphPath);
        }
        data.graphPath = graphPath;
    }
    if (!levelLoader::Save(levelPath, data)) {
        return fail("レベルを保存できませんでした: " + levelPath);
    }
    Open(levelPath, modelCommon_, camera_);
    statusMessage_ = "お手本から新しいレベルを作りました: " + levelPath;
    statusTimer_ = kStatusLongSeconds;
    return true;
}

/** @brief クリックした所が道の点の近くなら、その点を掴む */
bool StageEditor::BeginPathPointDrag(float mouseX, float mouseY) {
    // 道を使う型で、点がある時だけ
    if (path_.empty() || !GameModeLibrary::GetInstance()->Find(gameMode_).path.follow) {
        return false;
    }
    int nearest = -1;
    float nearestSquared = kPathPickRadiusPx * kPathPickRadiusPx;
    const ImVec2 mouse = ImGui::GetMousePos();
    for (int index = 0; index < static_cast<int>(path_.size()); ++index) {
        ImVec2 screen;
        if (!diagnosticsDraw::WorldToScreen(path_[static_cast<size_t>(index)].position, screen)) {
            continue;
        }
        const float dx = screen.x - mouse.x;
        const float dy = screen.y - mouse.y;
        if (dx * dx + dy * dy <= nearestSquared) {
            nearestSquared = dx * dx + dy * dy;
            nearest = index;
        }
    }
    if (nearest < 0) {
        return false;
    }
    // 掴んだ所が飛ばないよう、点とマウスの下の地面との差を覚えてから動かし始める
    Vector3 ground;
    if (!MouseToGround(mouseX, mouseY, ground)) {
        return false;
    }
    const Vector3& point = path_[static_cast<size_t>(nearest)].position;
    pathDragOffset_ = { point.x - ground.x, point.y - ground.y, 0.0f };
    selectedPathPoint_ = nearest;
    draggingPathPoint_ = nearest;
    BeginUndoCapture();
    return true;
}

/** @brief 掴んでいる道の点をマウスに付いて動かし、離したら確定する */
bool StageEditor::UpdatePathPointDrag() {
    if (draggingPathPoint_ < 0) {
        return false;
    }
    // 元に戻すで点が消えていたら、掴むのをやめる
    if (draggingPathPoint_ >= static_cast<int>(path_.size())) {
        draggingPathPoint_ = -1;
        CommitUndoCapture();
        return false;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        draggingPathPoint_ = -1;
        CommitUndoCapture();
        return true;
    }
    // 横と高さだけを動かし、奥行きはそのまま（吸着が入っていれば格子にそろえる）
    const ImVec2 mouse = ImGui::GetMousePos();
    Vector3 ground;
    if (MouseToGround(mouse.x, mouse.y, ground)) {
        Vector3& point = path_[static_cast<size_t>(draggingPathPoint_)].position;
        const float x = SnapValue(ground.x + pathDragOffset_.x);
        const float y = SnapValue(ground.y + pathDragOffset_.y);
        if (x != point.x || y != point.y) {
            point.x = x;
            point.y = y;
            MarkUndoDirty();
        }
    }
    return true;
}

} // namespace engine::game
#endif

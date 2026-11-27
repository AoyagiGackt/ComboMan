/**
 * @file GraphEditorLevelLink.cpp
 * @brief ノードエディタの、グラフと今のレベルのつなぎ方・保存して遊ぶまでの手順・グラフの流れを普通の文で読む欄
 * @note クラスは GraphEditor のまま。レベルの中身は StageEditor の公開の入口（DescribeGraphUse など）だけで触る
 */
#ifdef USE_IMGUI
#include "GraphEditor.h"
#include "EngineContext.h"
#include "EditorFlowRow.h"
#include "EditorUI.h"
#include "EditorUiStyle.h"
#include "GraphEditorServices.h"
#include "GraphRuntime.h"
#include "NodeRegistry.h"
#include "StageEditor.h"
#include "TimeManager.h"
#include <cstdio>
#include <filesystem>
#include <imgui.h>
#include <map>
#include <string>
#include <type_traits>
#include <variant>

namespace engine::game {
namespace {
constexpr size_t kMaxSummaryLines = 60; ///< 流れの文を出す最大の行数（大きなグラフで重くならないように）
constexpr float kSummaryIndent = 16.0f; ///< 分かれた先の行を下げる幅
constexpr size_t kNumberCapacity = 32; ///< 数を文字にする時の入れ物の大きさ
constexpr ImVec4 kCurrentStepColor = { 1.0f, 0.85f, 0.3f, 1.0f }; ///< 実行中の手順の色
constexpr const char* kDoneMark = "[済]"; ///< 終わった手順の印
constexpr const char* kTodoMark = "[  ]"; ///< まだの手順の印

/** @brief ノードの値を、流れの文に入れる文字にする（ON/OFF、余計な 0 を付けない数） */
std::string ValueText(const GraphValue& value) {
    return std::visit([](const auto& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, bool>) {
            return v ? "ON" : "OFF";
        } else if constexpr (std::is_same_v<T, float>) {
            char text[kNumberCapacity];
            std::snprintf(text, sizeof(text), "%g", v);
            return text;
        } else {
            return v.empty() ? std::string("（空）") : v;
        }
    }, value);
}

/** @brief 分かれた深さの分だけ行を下げる（深さ 0 では下げない。ImGui::Indent(0) は既定の幅だけ下げてしまうため） */
void IndentBy(int depth) {
    if (depth > 0) {
        ImGui::Indent(kSummaryIndent * static_cast<float>(depth));
    }
}

/** @brief IndentBy で下げた分を戻す */
void UnindentBy(int depth) {
    if (depth > 0) {
        ImGui::Unindent(kSummaryIndent * static_cast<float>(depth));
    }
}

/** @brief ノード1つを普通の文にする（node_labels.json の _summary の型、無ければ名前と値の並び） */
std::string Sentence(const GraphNode& node) {
    const std::string pattern = GraphNodeLabels::SummaryTemplate(node.type);
    // 値の書き方（線でつないだ値は、どこから来るかだけを言う）
    const auto valueOf = [&node](const std::string& key) -> std::string {
        if (node.paramLinks.contains(key)) {
            return "つないだ値";
        }
        const auto found = node.params.find(key);
        return found != node.params.end() ? ValueText(found->second) : std::string("?");
    };
    if (pattern.empty()) {
        std::string text = GraphNodeLabels::TypeName(node.type);
        std::string list;
        for (const auto& [key, value] : node.params) {
            list += (list.empty() ? "" : "、") + GraphNodeLabels::ParamName(node.type, key) + " " + valueOf(key);
        }
        return list.empty() ? text : text + "（" + list + "）";
    }
    // {名前} を値に置き換える
    std::string text;
    size_t cursor = 0;
    while (cursor < pattern.size()) {
        const size_t open = pattern.find('{', cursor);
        const size_t close = open == std::string::npos ? std::string::npos : pattern.find('}', open);
        if (close == std::string::npos) {
            text += pattern.substr(cursor);
            break;
        }
        text += pattern.substr(cursor, open - cursor) + valueOf(pattern.substr(open + 1, close - open - 1));
        cursor = close + 1;
    }
    return text;
}
} // namespace

/** @brief ノードエディタを開いた時、今のレベルが変わっていて編集中でなければ、そのレベルの常駐のグラフを開く */
void GraphEditor::AutoOpenLevelGraph() {
    const StageEditor* stage = StageEditor::GetActive();
    if (stage == nullptr || stage->GetLevelPath() == autoOpenedLevel_) {
        return;
    }
    autoOpenedLevel_ = stage->GetLevelPath();
    const std::string& resident = stage->GetResidentGraphPath();
    if (dirty_ || resident.empty() || resident == graphPath_) {
        return;
    }
    Open(resident);
    statusMessage_ = "今のレベルのグラフを開きました: " + resident;
    statusTimer_ = kStatusNormalSeconds;
}

/** @brief このグラフが今のレベルでどう使われているかと、保存して遊ぶまでの手順を出す */
void GraphEditor::DrawLevelLink() {
    if (!ImGui::CollapsingHeader("このグラフを使う", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    graphics::editorUI::TextDisabledWrapped("%s", graphPath_.c_str());
    StageEditor* stage = StageEditor::GetActive();
    if (stage == nullptr) {
        graphics::editorUI::TextDisabledWrapped("ステージを開いていないので、つなぐ先のレベルがありません。本編かトレーニングで開くと、ここからレベルにつなげます。");
        return;
    }
    const std::string use = stage->DescribeGraphUse(graphPath_);
    const std::string levelName = std::filesystem::path(stage->GetLevelPath()).stem().string();

    // 次にすることを、終わった物に印を付けて並べる
    const bool saved = !dirty_;
    const bool linked = !use.empty();
    ImGui::TextWrapped("%s 1. グラフを保存する", saved ? kDoneMark : kTodoMark);
    ImGui::TextWrapped("%s 2. レベル %s で使う", linked ? kDoneMark : kTodoMark, levelName.c_str());
    ImGui::TextWrapped("%s 3. 遊んで確かめる（下のボタンで 1〜3 をまとめてできます）", kTodoMark);
    if (linked) {
        ImGui::PushStyleColor(ImGuiCol_Text, engine::editor::uiStyle::kOkColor);
        ImGui::TextWrapped("%s", use.c_str());
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, engine::editor::uiStyle::kWarningColor);
        ImGui::TextWrapped("このグラフは、まだレベル %s で使われていません。保存しただけでは動きません。", levelName.c_str());
        ImGui::PopStyleColor();
    }

    // 保存・つなぐ・遊ぶ（入りきる分だけ横に並べる）
    engine::editor::FlowRow row;
    if (row.Button("保存して遊んで確かめる")) {
        Save();
        if (!dirty_) {
            // つないでいなければ常駐のグラフにし、つないであれば保存した中身で動かし直す
            if (linked) {
                stage->RestartLevelGraphs();
            } else {
                stage->UseAsResidentGraph(graphPath_);
            }
            // エディタを閉じてゲームの時間を戻し、ステージエディタを開いていれば出る位置からテストを始める
            visible_ = false;
            engine::Services().time->SetTimeScale(savedTimeScale_);
            stage->PlayTestFromSpawn();
        }
    }
    if (ImGui::IsItemHovered()) {
        graphics::editorUI::TooltipWrapped("グラフを保存し、このレベルにつないでから、エディタを閉じて動かします。ステージエディタを開いている時は、プレイヤーの出る位置からテストを始めます");
    }
    if (!linked && row.Button("保存して、このレベルで使う")) {
        Save();
        if (!dirty_) {
            stage->UseAsResidentGraph(graphPath_);
        }
    }
    if (linked && row.Button("保存して動かし直す")) {
        Save();
        if (!dirty_) {
            stage->RestartLevelGraphs();
            statusMessage_ = "保存した中身で、レベルのグラフを動かし直しました";
            statusTimer_ = kStatusNormalSeconds;
        }
    }
    graphics::editorUI::TextDisabledWrapped("レベルへのつなぎ方は、レベルの保存（ステージエディタの Ctrl+S）で残ります。");
}

/** @brief グラフの流れを、開始ノードから実行の順に普通の文で出す（押すとそのノードへ移る。実行中の所は色を変える） */
void GraphEditor::DrawFlowSummary() {
    if (!ImGui::CollapsingHeader("このグラフがすること", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    if (graph_.startNodeId.empty() || graph_.FindNode(graph_.startNodeId) == nullptr) {
        graphics::editorUI::TextDisabledWrapped("開始ノードが決まっていません。最初に動かすノードを選んで、開始ノードに設定してください。");
        return;
    }
    // テストの実行中で、このグラフを見ているなら、今いるノードを色で示す
    std::string currentId;
    if (testRuntime_.IsRunning()) {
        std::string leafPath;
        const GraphRuntime& leaf = testRuntime_.GetActiveLeaf(leafPath);
        if ((leafPath.empty() ? testRuntimeRootPath_ : leafPath) == graphPath_) {
            currentId = leaf.GetCurrentNodeId();
        }
    }
    std::map<std::string, int> numbers; ///< 出したノードと、その番号（戻る線を見分ける）
    size_t lines = 0;
    // 1本の流れを、分かれる所では両方の先を下げて出す
    const auto describe = [&](const auto& self, std::string id, int depth) -> void {
        while (!id.empty() && lines < kMaxSummaryLines) {
            IndentBy(depth);
            // もう出したノードへ戻る線は、番号だけを言う
            if (const auto seen = numbers.find(id); seen != numbers.end()) {
                ImGui::TextDisabled("（%d 番へ戻る）", seen->second);
                UnindentBy(depth);
                ++lines;
                return;
            }
            const GraphNode* node = graph_.FindNode(id);
            if (node == nullptr) {
                UnindentBy(depth);
                return;
            }
            const int number = static_cast<int>(numbers.size()) + 1;
            numbers[id] = number;
            const std::string line = std::to_string(number) + ". " + Sentence(*node) + (id == currentId ? "  ← 今ここ" : "");
            ImGui::PushID(id.c_str());
            if (id == currentId) {
                ImGui::PushStyleColor(ImGuiCol_Text, kCurrentStepColor);
            }
            // 長い文も折り返して出し、押すとそのノードを選んで画面の真ん中へ移す
            ImGui::TextWrapped("%s", line.c_str());
            if (ImGui::IsItemHovered()) {
                graphics::editorUI::TooltipWrapped("押すと、このノードを選んで画面の真ん中へ移します");
            }
            if (ImGui::IsItemClicked()) {
                SelectOnlyNode(id);
                FrameNodes(true);
            }
            if (id == currentId) {
                ImGui::PopStyleColor();
            }
            ImGui::PopID();
            UnindentBy(depth);
            ++lines;
            // 分かれるノードは、真と偽の先を下げて続ける
            if (NodeRegistry::GetInstance()->IsBranchType(node->type)) {
                IndentBy(depth + 1);
                ImGui::TextDisabled("当てはまる時:");
                UnindentBy(depth + 1);
                self(self, node->nextTrue, depth + 1);
                IndentBy(depth + 1);
                ImGui::TextDisabled("当てはまらない時:");
                UnindentBy(depth + 1);
                self(self, node->nextFalse, depth + 1);
                return;
            }
            id = node->next;
        }
        if (id.empty()) {
            IndentBy(depth);
            ImGui::TextDisabled("（ここで終わり）");
            UnindentBy(depth);
        }
    };
    describe(describe, graph_.startNodeId, 0);
    if (lines >= kMaxSummaryLines) {
        graphics::editorUI::TextDisabledWrapped("長いので、ここまでにしています。");
    }
    // 開始から届かないノードは流れに出ないので、点検の一覧を見るよう案内する
    if (numbers.size() < graph_.nodes.size()) {
        graphics::editorUI::TextDisabledWrapped("開始ノードから届かないノードが %d 個あります（実行されません）。上のつなぎ忘れを点検で見られます。",
            static_cast<int>(graph_.nodes.size() - numbers.size()));
    }
}

} // namespace engine::game
#endif

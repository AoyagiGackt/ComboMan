/**
 * @file PostProcessOrderEditor.cpp
 * @brief 画面全体へ掛けるエフェクトの順番と有効・無効を編集するエディタの実装
 */
#include "PostProcessOrderEditor.h"
#ifdef USE_IMGUI
#include "EditorFlowRow.h"
#include "EditorUI.h"
#include "JsonHelper.h"
#include "PostProcessStack.h"
#include <imgui.h>
#include <algorithm>
#include <cstddef>
#include <vector>

namespace engine::editor {
namespace {
using graphics::PostProcessStack;

/** @brief 初めて開いた時のウィンドウの大きさ */
constexpr ImVec2 kInitialWindowSize = { 460.0f, 420.0f };
/** @brief 編集中で保存していない時の見出しの色 */
constexpr ImVec4 kUnsavedColor = { 1.0f, 0.8f, 0.3f, 1.0f };
/** @brief JSON の文字列の配列を std::vector にする（文字列でない物は飛ばす） */
std::vector<std::string> ToNames(const nlohmann::json& value) {
    std::vector<std::string> names;
    if (!value.is_array()) {
        return names;
    }
    for (const auto& item : value) {
        if (item.is_string()) {
            names.push_back(item.get<std::string>());
        }
    }
    return names;
}

/** @brief 名前の並びに name が入っているか */
bool HasName(const std::vector<std::string>& names, const std::string& name) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

/** @brief チェックボックスの幅（四角・間・文字）を測る */
float CheckboxWidth(const char* label) {
    const ImGuiStyle& style = ImGui::GetStyle();
    // 四角は文字の高さと同じ幅で描かれる
    return ImGui::GetFrameHeight() + style.ItemInnerSpacing.x
        + ImGui::CalcTextSize(label, nullptr, true).x;
}

} // namespace

/** @brief 開いている間だけ、場面ごとの列と保存のボタンを描く */
void PostProcessOrderEditor::Draw(bool* open) {
    if (open == nullptr || !*open) {
        return;
    }
    ImGui::SetNextWindowSize(kInitialWindowSize, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("画面の効果の順番", open)) {
        ImGui::End();
        return;
    }
    if (!loaded_) {
        Reload();
    }

    // 保存・元に戻すのボタン（変えていない間は押せない）
    const bool hasChanges = edited_ != saved_;
    FlowRow toolbar;
    ImGui::BeginDisabled(!hasChanges);
    bool saveRequested = toolbar.Button("保存 (Ctrl+S)");
    if (toolbar.Button("元に戻す")) {
        Reload();
        PostProcessStack::ClearConfigOverride();
        status_ = "設定ファイルの中身に戻しました";
    }
    ImGui::EndDisabled();
    if (toolbar.Button("ファイルから読み直す")) {
        Reload();
        PostProcessStack::ClearConfigOverride();
        status_ = "設定ファイルを読み直しました";
    }
    saveRequested |= hasChanges && graphics::editorUI::CtrlShortcut(ImGuiKey_S, true);
    if (saveRequested) {
        Save();
    }
    if (hasChanges) {
        ImGui::TextColored(kUnsavedColor, "保存していない変更があります（ゲームにはもう効いています）");
    }
    if (!status_.empty()) {
        graphics::editorUI::TextDisabledWrapped("%s", status_.c_str());
    }
    graphics::editorUI::TextDisabledWrapped(
        "3D を描いた後・UI の前に、上から順に掛けます。チェックを外すと掛けません。トーンマップは明るさを丸めるので、ブルームより下に置きます。");
    ImGui::Separator();

    // 場面ごとの列
    nlohmann::json& stacks = edited_["stacks"];
    if (!stacks.is_object()) {
        stacks = nlohmann::json::object();
    }
    bool changed = false;
    for (auto it = stacks.begin(); it != stacks.end(); ++it) {
        changed |= DrawStack(it.key(), it.value());
    }
    if (changed) {
        ApplyPreview();
    }
    ImGui::End();
}

/** @brief 設定ファイルを読み、編集中の中身を同じにする */
void PostProcessOrderEditor::Reload() {
    saved_ = JsonHelper::Load(PostProcessStack::kConfigPath);
    if (!saved_.is_object()) {
        saved_ = nlohmann::json::object();
    }
    edited_ = saved_;
    loaded_ = true;
}

/** @brief 1つの場面の列を、有効のチェック・上へ・下へ・外すのボタンで編集する */
bool PostProcessOrderEditor::DrawStack(const std::string& stackName, nlohmann::json& stack) {
    if (!ImGui::CollapsingHeader(stackName.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        return false;
    }
    ImGui::PushID(stackName.c_str());
    std::vector<std::string> order = ToNames(stack.value("order", nlohmann::json::array()));
    std::vector<std::string> disabled = ToNames(stack.value("disabled", nlohmann::json::array()));
    bool changed = false;

    // 1行に1つ、上から掛ける順に並べる
    size_t moveUp = order.size();
    size_t moveDown = order.size();
    size_t remove = order.size();
    for (size_t i = 0; i < order.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const std::string label = std::to_string(i + 1) + ". " + PostProcessStack::GetDisplayName(order[i]);
        bool enabled = !HasName(disabled, order[i]);
        FlowRow row;
        row.Next(CheckboxWidth(label.c_str()));
        if (ImGui::Checkbox(label.c_str(), &enabled)) {
            // チェックを外した物は disabled に入れ、付けた物は disabled から出す
            if (enabled) {
                disabled.erase(std::remove(disabled.begin(), disabled.end(), order[i]), disabled.end());
            } else {
                disabled.push_back(order[i]);
            }
            changed = true;
        }
        if (ImGui::IsItemHovered()) {
            graphics::editorUI::TooltipWrapped("設定ファイルでの名前: %s", order[i].c_str());
        }
        // 上へ・下へは端では押せない
        ImGui::BeginDisabled(i == 0);
        if (row.Button("上へ")) {
            moveUp = i;
        }
        ImGui::EndDisabled();
        ImGui::BeginDisabled(i + 1 == order.size());
        if (row.Button("下へ")) {
            moveDown = i;
        }
        ImGui::EndDisabled();
        if (row.Button("外す")) {
            remove = i;
        }
        ImGui::PopID();
    }
    // 並びを描き終えてから入れ替える（描いている途中で並びを変えない）
    if (moveUp < order.size()) {
        std::swap(order[moveUp], order[moveUp - 1]);
        changed = true;
    }
    if (moveDown < order.size()) {
        std::swap(order[moveDown], order[moveDown + 1]);
        changed = true;
    }
    if (remove < order.size()) {
        // 外した物は、登録されていれば最後に掛けられるので、止めたい時はチェックを外す
        disabled.erase(std::remove(disabled.begin(), disabled.end(), order[remove]), disabled.end());
        order.erase(order.begin() + static_cast<std::ptrdiff_t>(remove));
        changed = true;
    }

    // 登録されているのに並びに無い物を、足す候補として出す
    std::vector<std::string> candidates;
    for (const std::string& name : PostProcessStack::GetKnownNames()) {
        if (!HasName(order, name)) {
            candidates.push_back(name);
        }
    }
    if (!candidates.empty()) {
        graphics::editorUI::TextDisabledWrapped("並びに無い物（この場面で登録されていれば、いちばん最後に掛かります）");
        FlowRow addRow;
        for (const std::string& name : candidates) {
            const std::string label = "足す: " + PostProcessStack::GetDisplayName(name) + "##add" + name;
            if (addRow.Button(label.c_str())) {
                order.push_back(name);
                changed = true;
            }
        }
    }

    if (changed) {
        stack["order"] = order;
        stack["disabled"] = disabled;
    }
    ImGui::PopID();
    return changed;
}

/** @brief 編集中の中身を、保存の前からゲームの列に読み直させる */
void PostProcessOrderEditor::ApplyPreview() {
    PostProcessStack::SetConfigOverride(edited_);
    status_.clear();
}

/** @brief 編集中の中身を設定ファイルに書き、ゲームは設定ファイルから読む形に戻す */
void PostProcessOrderEditor::Save() {
    if (!JsonHelper::Save(PostProcessStack::kConfigPath, edited_)) {
        status_ = std::string("保存できませんでした: ") + PostProcessStack::kConfigPath;
        return;
    }
    saved_ = edited_;
    PostProcessStack::ClearConfigOverride();
    status_ = std::string("保存しました: ") + PostProcessStack::kConfigPath;
}

} // namespace engine::editor
#else
namespace engine::editor {
/** @brief ImGui を使わない構成では何も描かない */
void PostProcessOrderEditor::Draw(bool* open) {
    (void)open;
}
} // namespace engine::editor
#endif // USE_IMGUI

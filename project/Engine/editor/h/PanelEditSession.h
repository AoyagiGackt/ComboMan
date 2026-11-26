/**
 * @file PanelEditSession.h
 * @brief 手書きの調整パネルに、元に戻す・やり直す・保存した値や初期値へ戻す・見比べ・項目の説明をまとめて付ける部品
 */
#pragma once
#ifdef USE_IMGUI
#include "EditorFlowRow.h"
#include "EditorUI.h"
#include "SnapshotHistory.h"
#include <imgui.h>
#include <optional>
#include <utility>

namespace engine::editor {

/**
 * @brief 1つの設定の構造体を編集するパネルの履歴と戻す操作を受け持つ
 * @tparam Settings 編集する設定の型（コピーでき、既定のコンストラクタが初期値を表すこと）
 * @note 使い方: 毎フレームの最初に BeginFrame、上に DrawToolbar、各入力部品の直後に Item を呼ぶ。
 *       Item は直前の部品の操作を履歴に積み、マウスを乗せると説明、右クリックで戻すメニューを出す。
 *       見比べを使う時は、入力部品を BeginFields と EndFields で囲む（見比べ中は触れなくなる）
 */
template <class Settings>
class PanelEditSession {
public:
    /** @brief 読み込み・保存した直後に呼び、戻す先の値として覚える（未保存の印も消える） */
    void MarkSaved(const Settings& saved) {
        saved_ = saved;
        dirty_ = false;
    }

    /** @brief 保存していない変更があるかを返す */
    bool IsDirty() const {
        return dirty_;
    }

    /**
     * @brief フレームの最初に呼び、変更前の値を写しておく（パネルを描く前に呼ぶ）
     * @note 前のフレームで操作中だった部品が離されないまま閉じられた時は、ここで履歴を確定する
     */
    void BeginFrame(Settings& current) {
        if (history_.IsCapturing() && !ImGui::IsAnyItemActive()) {
            history_.Commit();
        }
        // 見比べ中で自動切り替えなら、決まった秒数ごとに保存した値と新しい値を入れ替える
        if (comparing_ && autoSwitch_ && ImGui::GetTime() - lastSwitchTime_ >= kCompareSwitchSeconds) {
            ShowCompareSide(current, !showingSaved_);
        }
        frameStart_ = current;
        itemCount_ = 0;
    }

    /** @brief 見比べ中かを返す */
    bool IsComparing() const {
        return comparing_;
    }

    /** @brief 見比べをやめ、新しい値に戻す（保存する前などに呼ぶ） */
    void StopCompare(Settings& current) {
        if (comparing_) {
            current = edited_;
            comparing_ = false;
        }
    }

    /** @brief 入力部品の前に呼ぶ（見比べ中は部品を触れなくする） */
    void BeginFields() const {
        ImGui::BeginDisabled(comparing_);
    }

    /** @brief 入力部品の後に呼ぶ（BeginFields と対にする） */
    void EndFields() const {
        ImGui::EndDisabled();
    }

    /**
     * @brief 元に戻す・やり直す・まとめて戻すボタンと未保存の印を描き、Ctrl+Z と Ctrl+Y を受け付ける
     * @param current 編集中の設定（戻す操作で書き換わる）
     */
    void DrawToolbar(Settings& current) {
        // ボタンはパネルの幅に入る分だけ横に並べ、入らなければ次の行へ回す
        FlowRow row;
        // 見比べ中は、どちらの値かと切り替えの操作だけを出す
        if (DrawCompareControls(current, row)) {
            return;
        }
        // パネルの中を操作している時だけ、ショートカットで戻す・やり直す
        const ImGuiIO& io = ImGui::GetIO();
        const bool panelFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        const bool shortcutAllowed = panelFocused && io.KeyCtrl && !io.WantTextInput && !ImGui::IsAnyItemActive();
        const bool undoShortcut = shortcutAllowed && ImGui::IsKeyPressed(ImGuiKey_Z, false);
        const bool redoShortcut = shortcutAllowed && ImGui::IsKeyPressed(ImGuiKey_Y, false);

        // 元に戻す
        ImGui::BeginDisabled(!history_.CanUndo());
        const bool undoPressed = row.Button("元に戻す");
        ImGui::EndDisabled();
        ShowHelp("1つ前の値に戻します（Ctrl+Z）");
        if ((undoPressed || undoShortcut) && history_.CanUndo()) {
            ApplyHistory(current, history_.Undo(current));
        }
        // やり直す
        ImGui::BeginDisabled(!history_.CanRedo());
        const bool redoPressed = row.Button("やり直す");
        ImGui::EndDisabled();
        ShowHelp("元に戻した操作をやり直します（Ctrl+Y）");
        if ((redoPressed || redoShortcut) && history_.CanRedo()) {
            ApplyHistory(current, history_.Redo(current));
        }

        // まとめて戻す操作は取り返しが付くよう履歴に積むが、押し間違いを防ぐため確認をはさむ
        row.Next(FlowRow::ButtonWidth("保存した値に全部戻す"));
        if (graphics::editorUI::ConfirmButton("保存した値に全部戻す", "保存していない変更をすべて捨てて、最後に保存した値に戻しますか？", "戻す")) {
            ReplaceAll(current, saved_);
        }
        row.Next(FlowRow::ButtonWidth("初期値に全部戻す"));
        if (graphics::editorUI::ConfirmButton("初期値に全部戻す", "すべての項目を最初から決まっている値に戻しますか？（保存するまでファイルは変わりません）", "戻す")) {
            ReplaceAll(current, defaults_);
        }

        // 未保存の印
        if (dirty_) {
            row.Next(ImGui::CalcTextSize(kUnsavedLabel).x);
            ImGui::TextColored(kUnsavedColor, "%s", kUnsavedLabel);
        }
        graphics::editorUI::TextDisabledWrapped("項目にマウスを乗せると説明、右クリックで値を戻せます");
    }

    /**
     * @brief 直前の入力部品に、履歴・説明・右クリックで戻すメニューを付ける（設定の直下のメンバー用）
     * @param current 編集中の設定
     * @param member  直前の部品が編集したメンバー
     * @param help    マウスを乗せた時の説明（nullptr なら戻す方法だけを出す）
     */
    template <class Field>
    void Item(Settings& current, Field Settings::* member, const char* help) {
        Item(current, help, [member](Settings& settings) -> Field& {
            return settings.*member;
        });
    }

    /**
     * @brief 直前の入力部品に、履歴・説明・右クリックで戻すメニューを付ける（配列の中など入れ子の値用）
     * @param access 設定から同じ値を取り出す関数（編集中・保存時・初期値の3つに使う）
     */
    template <class Accessor>
    void Item(Settings& current, const char* help, Accessor access) {
        TrackLastItem();
        ShowHelp(help);
        // 右クリックで、この項目だけを保存した値か初期値へ戻す
        // 色の選択のように名前を持たない部品もあるので、フレームの中の順番でメニューを見分ける
        ImGui::PushID(itemCount_++);
        if (ImGui::BeginPopupContextItem(kRevertMenuId)) {
            if (ImGui::MenuItem("保存した値に戻す")) {
                history_.Record(current);
                access(current) = access(saved_);
                dirty_ = true;
            }
            if (ImGui::MenuItem("初期値に戻す")) {
                history_.Record(current);
                access(current) = access(defaults_);
                dirty_ = true;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    /** @brief 入力部品ではないボタンなどで設定をまとめて変える直前に呼び、元に戻せるようにする */
    void RecordBeforeChange(const Settings& current) {
        history_.Record(current);
        dirty_ = true;
    }

private:
    /** @brief 項目ごとの戻すメニューの名前（番号と組み合わせて見分ける） */
    static constexpr const char* kRevertMenuId = "##revertMenu";
    /** @brief 見比べの自動切り替えの間隔（秒） */
    static constexpr double kCompareSwitchSeconds = 1.5;
    /** @brief 見比べ中に保存した値を見せている時の色 */
    static constexpr ImVec4 kSavedSideColor = { 0.6f, 0.8f, 1.0f, 1.0f };
    /** @brief 見比べ中に新しい値を見せている時の色 */
    static constexpr ImVec4 kEditedSideColor = { 1.0f, 0.75f, 0.35f, 1.0f };
    static constexpr const char* kUnsavedLabel = "未保存"; ///< 未保存の印の文字
    static constexpr const char* kAutoSwitchLabel = "自動で入れ替える"; ///< 見比べの自動切り替えの文字
    /** @brief 未保存の印の色 */
    static constexpr ImVec4 kUnsavedColor = { 1.0f, 0.85f, 0.3f, 1.0f };

    /**
     * @brief 見比べの開始ボタン、または見比べ中の切り替えの操作を描く
     * @return 見比べ中なら true（ほかのボタンは出さない）
     */
    bool DrawCompareControls(Settings& current, FlowRow& row) {
        if (!comparing_) {
            // 始める前に新しい値を取っておき、保存した値から見せる
            if (row.Button("見比べる")) {
                edited_ = current;
                comparing_ = true;
                ShowCompareSide(current, true);
            }
            ShowHelp("保存した値と今の値を入れ替えながら遊んで、どちらがよいか比べます");
            return false;
        }
        // 今どちらを見せているかを大きく色分けして出す
        ImGui::PushStyleColor(ImGuiCol_Text, showingSaved_ ? kSavedSideColor : kEditedSideColor);
        ImGui::TextWrapped("見比べ中: 今は %s", showingSaved_ ? "保存した値 (A)" : "新しい値 (B)");
        ImGui::PopStyleColor();
        FlowRow compareRow;
        if (compareRow.Button(showingSaved_ ? "新しい値 (B) にする" : "保存した値 (A) にする")) {
            ShowCompareSide(current, !showingSaved_);
        }
        compareRow.Next(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize(kAutoSwitchLabel).x);
        ImGui::Checkbox(kAutoSwitchLabel, &autoSwitch_);
        ShowHelp("1.5秒ごとに A と B を入れ替えます");
        if (compareRow.Button("見比べをやめる")) {
            StopCompare(current);
        }
        ShowHelp("新しい値 (B) に戻して、また編集できるようにします");
        return true;
    }

    /** @brief 見比べ中に、保存した値か新しい値のどちらかを当てはめる */
    void ShowCompareSide(Settings& current, bool savedSide) {
        showingSaved_ = savedSide;
        current = savedSide ? saved_ : edited_;
        lastSwitchTime_ = ImGui::GetTime();
    }

    /** @brief 直前の部品を触り始めた・変えた・離した時に、履歴の記録を始める・印を付ける・確定する */
    void TrackLastItem() {
        if (ImGui::IsItemActivated()) {
            history_.Begin(frameStart_);
        }
        if (ImGui::IsItemEdited()) {
            dirty_ = true;
            // 色の選択の小窓のように、触り始めを拾えない部品はこの1回で積む
            if (history_.IsCapturing()) {
                history_.MarkChanged();
            } else {
                history_.Record(frameStart_);
            }
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            history_.Commit();
        }
    }

    /** @brief 直前の部品にマウスが乗っていれば、説明と戻し方を折り返して出す */
    static void ShowHelp(const char* help) {
        if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) {
            return;
        }
        if (help != nullptr) {
            graphics::editorUI::TooltipWrapped("%s", help);
        }
    }

    /** @brief 元に戻す・やり直すで取り出した状態を当てはめる */
    void ApplyHistory(Settings& current, std::optional<Settings> restored) {
        if (restored) {
            current = std::move(*restored);
            dirty_ = true;
        }
    }

    /** @brief 今の状態を履歴に積んでから、まるごと別の値に置き換える */
    void ReplaceAll(Settings& current, const Settings& replacement) {
        history_.Record(current);
        current = replacement;
        dirty_ = true;
    }

    EditorHistory<Settings> history_; ///< 元に戻す・やり直すの履歴
    Settings frameStart_ { }; ///< このフレームで編集する前の値
    Settings saved_ { }; ///< 最後に読み込んだ・保存した値
    Settings defaults_ { }; ///< 既定のコンストラクタが決める初期値
    bool dirty_ = false; ///< 保存していない変更があるか
    int itemCount_ = 0; ///< このフレームで Item を呼んだ数（戻すメニューを見分ける番号）
    Settings edited_ { }; ///< 見比べを始めた時の新しい値
    bool comparing_ = false; ///< 見比べ中か
    bool showingSaved_ = false; ///< 見比べ中に保存した値を見せているか
    bool autoSwitch_ = true; ///< 見比べ中に自動で入れ替えるか
    double lastSwitchTime_ = 0.0; ///< 最後に入れ替えた時刻（ImGui の時計）
};

} // namespace engine::editor
#endif // USE_IMGUI

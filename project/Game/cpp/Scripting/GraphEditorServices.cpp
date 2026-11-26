/**
 * @file GraphEditorServices.cpp
 * @brief グラフ編集の入力処理とノード表示を個別に実行する
 */
#ifdef USE_IMGUI
#include "GraphEditorServices.h"
#include "TextBufferCapacity.h"
#include "EditorHub.h"
#include "EditorUI.h"
#include "GraphEditor.h"
#include "GraphRuntime.h"
#include "Input.h"
#include "JsonHelper.h"
#include "NodeRegistry.h"
#include "TimeManager.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <optional>
#include <vector>

namespace engine::game {
namespace {
/** @brief 入力欄の変更有無と単発操作かどうかの通知 */
struct GraphWidgetEdit {
    bool changed;
    bool discrete;
};
/** @brief 数値入力欄を表示する */
GraphWidgetEdit DrawValueWidget(const char* label, float& value) {
    return { ImGui::InputFloat(label, &value), false };
}
/** @brief 真偽値の切り替え欄を表示する */
GraphWidgetEdit DrawValueWidget(const char* label, bool& value) {
    return { ImGui::Checkbox(label, &value), true };
}
/** @brief 一時バッファで文字列を編集し、変更時に値へ反映する */
GraphWidgetEdit DrawValueWidget(const char* label, std::string& value) {
    char buffer[textBufferCapacity::kPath];
    strncpy_s(buffer, value.c_str(), _TRUNCATE);
    const bool changed = ImGui::InputText(label, buffer, sizeof(buffer));
    if (changed) {
        value = buffer;
    }
    return { changed, false };
}

/** @brief ノードの分類と見出しの色の組 */
struct CategoryColor {
    const char* name;
    ImU32 color;
};
constexpr CategoryColor kCategoryColors[] = {
    { "よく使う", IM_COL32(46, 89, 132, 255) },
    { "配置物", IM_COL32(42, 100, 78, 255) },
    { "進行", IM_COL32(107, 82, 43, 255) },
    { "演出・音声", IM_COL32(107, 64, 113, 255) },
    { "敵", IM_COL32(133, 57, 54, 255) },
    { "プレイヤー", IM_COL32(44, 99, 107, 255) },
    { "変数・フラグ", IM_COL32(86, 75, 133, 255) },
    { "数値・論理", IM_COL32(85, 96, 49, 255) },
};
constexpr ImU32 kWarningBorderColor = IM_COL32(255, 139, 56, 255);

/** @brief 未登録の分類には標準のボタン色を使う */
ImU32 TitleColor(const NodeTypeSpec* spec) {
    for (const auto& category : kCategoryColors) {
        if (spec && spec->category == category.name) {
            return category.color;
        }
    }
    return ImGui::GetColorU32(ImGuiCol_Button);
}

constexpr const char* kNodeLabelsPath = "Resources/Config/UI/node_labels.json";

/** @brief 読み込み済みの表示名の表（最初に引いた時に読む） */
nlohmann::json& NodeLabelTable() {
    static nlohmann::json sTable = JsonHelper::Load(kNodeLabelsPath);
    return sTable;
}

/** @brief ノード種別の項目を返す（無ければ null） */
const nlohmann::json* FindTypeLabels(const std::string& type) {
    const nlohmann::json& table = NodeLabelTable();
    if (!table.is_object() || !table.contains("types") || !table["types"].is_object()) {
        return nullptr;
    }
    const auto entry = table["types"].find(type);
    return entry != table["types"].end() && entry->is_object() ? &*entry : nullptr;
}
} // namespace

/** @brief ノード種別の表示名を返す（表に無ければ type 名のまま） */
std::string GraphNodeLabels::TypeName(const std::string& type) {
    const nlohmann::json* labels = FindTypeLabels(type);
    if (labels == nullptr) {
        return type;
    }
    const auto name = labels->find("name");
    return name != labels->end() && name->is_string() ? name->get<std::string>() : type;
}

/** @brief ノードのパラメータの表示名を返す（表に無ければパラメータ名のまま） */
std::string GraphNodeLabels::ParamName(const std::string& type, const std::string& param) {
    const nlohmann::json* labels = FindTypeLabels(type);
    if (labels == nullptr) {
        return param;
    }
    const auto params = labels->find("params");
    if (params == labels->end() || !params->is_object()) {
        return param;
    }
    const auto name = params->find(param);
    return name != params->end() && name->is_string() ? name->get<std::string>() : param;
}

/** @brief ノードの流れの文の型を返す（_summary。無ければ空） */
std::string GraphNodeLabels::SummaryTemplate(const std::string& type) {
    const nlohmann::json& table = NodeLabelTable();
    if (!table.is_object()) {
        return {};
    }
    const auto summary = table.find("_summary");
    if (summary == table.end() || !summary->is_object()) {
        return {};
    }
    const auto found = summary->find(type);
    return found != summary->end() && found->is_string() ? found->get<std::string>() : std::string();
}

/** @brief パラメーターの説明を返す（種類ごとの表、共通の表の順に探し、無ければ汎用の説明） */
std::string GraphNodeLabels::ParamHelp(const std::string& type, const std::string& param) {
    const nlohmann::json& table = NodeLabelTable();
    // 表の中の文字列を1つ引く（無ければ空）
    const auto lookup = [](const nlohmann::json& section, const std::string& key) -> std::string {
        if (!section.is_object()) {
            return {};
        }
        const auto found = section.find(key);
        return found != section.end() && found->is_string() ? found->get<std::string>() : std::string();
    };
    if (table.is_object()) {
        // 同じ名前でも種類によって意味が違うパラメーターは、種類ごとの表を先に見る
        if (const auto byType = table.find("_paramHelpByType"); byType != table.end() && byType->is_object()) {
            if (const auto typeTable = byType->find(type); typeTable != byType->end()) {
                if (std::string help = lookup(*typeTable, param); !help.empty()) {
                    return help;
                }
            }
        }
        if (const auto common = table.find("_paramHelp"); common != table.end()) {
            if (std::string help = lookup(*common, param); !help.empty()) {
                return help;
            }
        }
    }
    return "ノードが処理に使用する入力値";
}

/** @brief 表を読み直す */
void GraphNodeLabels::Reload() {
    NodeLabelTable() = JsonHelper::Load(kNodeLabelsPath);
}

/** @brief 型別の入力を共通の通知へまとめ、元の値を変更せず結果を返す */
GraphValueEditResult DrawGraphValueInput(const char* label, const GraphValue& value) {
    GraphValueEditResult result { value };
    const GraphWidgetEdit edit = std::visit([label](auto& current) {
        return DrawValueWidget(label, current);
    }, result.value);
    result.changed = edit.changed;
    result.discrete = edit.discrete;
    result.activated = ImGui::IsItemActivated();
    result.deactivated = ImGui::IsItemDeactivated();
    return result;
}

/** @brief 値型に対応するピン色を返す */
ImU32 GraphEditorDrawingStyle::ColorForType(GraphValueType type) {
    // GraphValueType の並び順
    static constexpr std::array<ImU32, 4> kTypeColors = {
        IM_COL32(110, 220, 110, 255), // Float
        IM_COL32(220, 100, 100, 255), // Bool
        IM_COL32(200, 120, 220, 255), // String
        IM_COL32(240, 240, 240, 255), // Any
    };
    const size_t index = static_cast<size_t>(type);
    return index < kTypeColors.size() ? kTypeColors[index] : kTypeColors.back();
}

/** @brief F1 でエディタの表示を切り替え、開いている間はゲームの時間を止める（表示中は true） */
bool GraphEditorInteraction::PrepareFrame(GraphEditor& editor, engine::Input* input, float& realDeltaTime) {

    bool wasVisible = editor.visible_;
    // F1 か、F8 のエディタ一覧のボタンで開閉する
    const bool hubToggle = engine::editor::EditorHub::GetInstance()->Offer("graph", "ノードグラフ", "F1",
        "敵を全部倒したら扉が開く、のような仕掛けを、ノードを線でつないで作ります", editor.visible_);
    if ((input && input->TriggerKey(DIK_F1)) || hubToggle) {
        editor.visible_ = !editor.visible_;
    }
    if (editor.visible_ != wasVisible) {
        if (editor.visible_) {
            // 今のレベルが変わっていたら、そのレベルのグラフを開く（毎回ファイルを選ばなくてよいように）
            editor.AutoOpenLevelGraph();
            // 開くたびに日本語の表示名を読み直す（node_labels.json の書き換えを再起動せずに見られる）
            GraphNodeLabels::Reload();
            // 開いている間は背後のゲームを止める（TimeManager のタイムスケールを流用する）
            editor.savedTimeScale_ = TimeManager::GetInstance()->GetTimeScale();
            TimeManager::GetInstance()->SetTimeScale(0.0f);
        } else {
            TimeManager::GetInstance()->SetTimeScale(editor.savedTimeScale_);
        }
    }
    if (!editor.visible_) {
        return false;
    }

    // エディタ内蔵のタイマー類は、開いている間はTimeManagerのdtが0になる（背後のゲームを止めるため）ので、
    // ImGuiが持つ実時間ベースのdt（io.DeltaTime）を使う
    realDeltaTime = ImGui::GetIO().DeltaTime;
    if (editor.statusTimer_ > 0.0f) {
        editor.statusTimer_ -= realDeltaTime;
    }

    // 保存は入力欄の編集と競合しないので、入力中でも受け付ける
    if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        editor.Save();
    }

    // Ctrl系ショートカット（テキスト入力欄にフォーカスがある間はImGui自身の入力欄内編集に譲る）
    if (!ImGui::GetIO().WantTextInput) {
        /** @brief 修飾キー付きのショートカットと実行操作の対応 */
        struct CtrlShortcut {
            ImGuiKey key;
            void (*action)(GraphEditor&);
            bool repeats; ///< 押しっぱなしで繰り返すか
        };
        static constexpr CtrlShortcut kCtrlShortcuts[] = {
            {
                ImGuiKey_Z, [](GraphEditor& e) {
                    e.Undo();
                }, true
            },
            {
                ImGuiKey_Y, [](GraphEditor& e) {
                    e.Redo();
                }, true
            },
            // editor.DrawCanvas()のノード走査ループの外なので、ここでは直接複製してよい
            {
                ImGuiKey_D, [](GraphEditor& e) {
                    e.DuplicateSelectedNodes();
                }, false
            },
            {
                ImGuiKey_A, [](GraphEditor& e) {
                    e.SelectAllNodes();
                }, false
            },
        };
        if (ImGui::GetIO().KeyCtrl) {
            for (const CtrlShortcut& shortcut : kCtrlShortcuts) {
                if (ImGui::IsKeyPressed(shortcut.key, shortcut.repeats)) {
                    shortcut.action(editor);
                    break; // 1フレームに実行するのは1つだけ
                }
            }
        }
    }
    return true;
}

/** @brief 操作の案内を出す */
void GraphEditorInteraction::DrawGuidance(GraphEditor& editor) {
    // 作った後にどうすればよいかと、つないだ結果どう動くかを一番上に出す
    editor.DrawLevelLink();
    editor.DrawFlowSummary();
    // ノードを選んでいる時は、その種類の説明とパラメーターごとの意味を出す
    if (editor.selectedNodeIds_.size() == 1) {
        // 選択中の id のノードが消えていたり、種類が未登録なら説明は出さない
        const GraphNode* selected = editor.graph_.FindNode(editor.selectedNodeId_);
        const NodeTypeSpec* selectedSpec = selected
            ? NodeRegistry::GetInstance()->FindSpec(selected->type)
            : nullptr;
        if (selected && selectedSpec && ImGui::CollapsingHeader("選択中ノードの説明", ImGuiTreeNodeFlags_DefaultOpen)) {
            // 種類名と分類、ノード全体の説明
            ImGui::TextWrapped("%s (%s)  %s", GraphNodeLabels::TypeName(selected->type).c_str(), selected->type.c_str(),
                selectedSpec->category.c_str());
            ImGui::TextWrapped("%s", selectedSpec->description.c_str());
            // パラメーター名から説明文を引く（表は node_labels.json の _paramHelp）
            auto parameterHelp = [selected](const std::string& key) {
                return GraphNodeLabels::ParamHelp(selected->type, key);
            };
            // 表に無いパラメーター名は汎用の説明を出す
            for (const NodeParamSpec& param : selectedSpec->params) {
                graphics::editorUI::BulletWrapped("%s (%s)  %s", GraphNodeLabels::ParamName(selected->type, param.key).c_str(),
                    param.key.c_str(), parameterHelp(param.key).c_str());
            }
            // 値を出力するノードかどうかで、次のノードへの渡し方の案内を変える
            graphics::editorUI::BulletWrapped("左上の入力で前のノードから実行の順番を受け取ります。開始ノードはここから動きます。");
            graphics::editorUI::BulletWrapped("%s", selectedSpec->branches
                    ? "右の緑の出口は条件を満たす時、赤の出口はそれ以外の時に進む先です。"
                    : "右上の黄色の出口は次に実行するノードにつなぎます。");
            if (selectedSpec->hasOutput) {
                graphics::editorUI::BulletWrapped("右下の色付きピンから処理結果を別のノードの入力へ渡せます。");
            }
        }
    }
    // 折りたためる使い方一覧
    if (ImGui::CollapsingHeader("使い方")) {
        // ノードの追加と探し方
        graphics::editorUI::BulletWrapped("何もない所を右クリックし、お手本から追加を選ぶと、つなぎ方の例を置けます");
        graphics::editorUI::BulletWrapped("何もない所を右クリック → ノードやコメント枠を追加（よく使う物は直接、他はジャンル別サブメニューに分類）");
        graphics::editorUI::BulletWrapped("追加メニュー上部の検索欄に入力すると、名前・ジャンル・説明で絞り込める");
        graphics::editorUI::BulletWrapped("ノードにマウスを乗せると、何ができるノードか説明がツールチップで出る");
        graphics::editorUI::BulletWrapped("Ctrl+Z: 元に戻す  Ctrl+Y: やり直す  Ctrl+S: 保存  Ctrl+D: 選択ノードを複製  Ctrl+A: 全選択");
        graphics::editorUI::BulletWrapped("F: 選択中のノード（無ければ全体）が収まるように表示");
        graphics::editorUI::BulletWrapped("ホイール: カーソル位置を中心にズーム  中ドラッグかスペース+左ドラッグ: パン");
        // 実行の線と値の線のつなぎ方
        graphics::editorUI::BulletWrapped("実行の流れ: ノード右上の黄ピンを、次のノードへドラッグして接続（ノードのどこに落としてもよい）");
        graphics::editorUI::BulletWrapped("配線中は接続できるピンに輪が付き、近づくと吸着する");
        graphics::editorUI::BulletWrapped("Ifノードは緑ピン=条件が真のとき、赤ピン=偽のときの行き先");
        graphics::editorUI::BulletWrapped("値の受け渡し: ノード右下の出力ピンを、パラメータ左の同じ色のピンへドラッグ");
        graphics::editorUI::BulletWrapped("　ピンの色 = 型: 緑=数値  赤=ON/OFF  紫=文字列  白=なんでも接続可");
        graphics::editorUI::BulletWrapped("線かピンを右クリック → 配線を解除");
        graphics::editorUI::BulletWrapped("つながったパラメータ左のピンをドラッグ → 線を抜いて別の場所へつなぎ替え（何もない所で離すと切断）");
        // ノードの移動・削除と、テスト実行・ほかの機能との連携
        graphics::editorUI::BulletWrapped("タイトルかノードの余白をドラッグ: 選択中のノードをまとめて移動（Ctrlで格子に吸着）");
        graphics::editorUI::BulletWrapped("Delete: 選択中のノードを削除");
        graphics::editorUI::BulletWrapped("何もない所をドラッグ: 範囲選択（Ctrl/Shiftで追加）  クリック: 選択解除");
        graphics::editorUI::BulletWrapped("Ctrl/Shift+クリック: ノードを選択に追加、もう一度で外す");
        graphics::editorUI::BulletWrapped("コメント枠は上の帯をドラッグで移動、右下の角をドラッグで大きさ変更");
        graphics::editorUI::BulletWrapped("実行ボタンでこのグラフをその場でテスト（黄色い枠 = 今実行中のノード）");
        graphics::editorUI::BulletWrapped("Subgraphノード: 別のグラフJSONを部品として呼び出す（カプセル化）");
        graphics::editorUI::BulletWrapped("フラグはステージエディタ(F2)のトリガーと共有（GetFlag/SetFlagで読み書き）");
    }
    // 初めて使う人向けに、イベントを1本作る手順を番号付きで出す
    if (ImGui::CollapsingHeader("はじめてのイベント作成", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextWrapped("例として、ステージのトリガーがONになったら1秒待ち、効果音を鳴らす流れを作ります。");
        graphics::editorUI::BulletWrapped("1  空白を右クリックし、フラグを待つ、待つ、効果音を鳴らすの3つを追加する");
        graphics::editorUI::BulletWrapped("2  フラグを待つのフラグ名へ、ステージエディタで設定したフラグ名を入力する");
        graphics::editorUI::BulletWrapped("3  黄色の実行ピンを、フラグを待つ、待つ、効果音を鳴らすの順につなぐ");
        graphics::editorUI::BulletWrapped("4  待つの秒数へ1、効果音を鳴らすのファイルへ音声ファイルを指定する");
        graphics::editorUI::BulletWrapped("5  フラグを待つを選択し、開始ノードに設定する");
        graphics::editorUI::BulletWrapped("6  実行で黄色い枠が順に進むことを確認して保存する");
        graphics::editorUI::TextDisabledWrapped("値ピンは計算結果を渡す時に使います。固定値だけなら入力欄へ直接入力できます。");
    }
}

/** @brief ツールバーを出す */
void GraphEditorInteraction::DrawToolbar(GraphEditor& editor) {
    // 編集中のグラフのパスと、開く・保存
    char pathBuf[textBufferCapacity::kPath];
    strncpy_s(pathBuf, editor.graphPath_.c_str(), _TRUNCATE);
    ImGui::SetNextItemWidth(GraphEditorDrawingStyle::kGraphPathFieldWidth);
    // パスは入力した時点で書き換え、開くボタンで読み込む
    if (ImGui::InputText("##path", pathBuf, sizeof(pathBuf))) {
        editor.graphPath_ = pathBuf;
    }
    ImGui::SameLine();
    if (ImGui::Button("開く")) {
        // 未保存の編集がある時は黙って破棄せず、確認モーダルを挟む
        if (editor.dirty_) {
            editor.pendingConfirmOpenPath_ = editor.graphPath_;
            editor.requestConfirmOpen_ = true;
        } else {
            editor.Open(editor.graphPath_);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("保存 (Ctrl+S)")) {
        editor.Save();
    }
    // 保存し忘れに気付けるよう、未保存の変更がある時だけ表示する
    if (editor.dirty_) {
        graphics::editorUI::DrawUnsavedMark();
    }

    // Subgraphの開くボタン等、ウィンドウスコープ外で予約された分もここでモーダルを開く
    if (editor.requestConfirmOpen_) {
        ImGui::OpenPopup("グラフを開く確認");
        editor.requestConfirmOpen_ = false;
    }
    // 確認で破棄を選んだ時だけ、予約したパスを開く
    if (graphics::editorUI::ConfirmModal("グラフを開く確認",
            "未保存の変更があります。\n変更を破棄して開き直しますか？",
            "破棄して開く")
        == graphics::editorUI::ConfirmResult::kOk) {
        editor.Open(editor.pendingConfirmOpenPath_);
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    // 選択中のノードを開始ノードにする・自動整列
    bool hasSelection = !editor.selectedNodeId_.empty() && editor.graph_.FindNode(editor.selectedNodeId_);
    ImGui::BeginDisabled(!hasSelection);
    if (ImGui::Button("開始ノードに設定")) {
        // 元に戻せるよう、変える前に履歴を取る
        editor.RecordUndoSnapshotNow();
        editor.graph_.startNodeId = editor.selectedNodeId_;
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("自動整列")) {
        editor.ArrangeNodes();
    }
    ImGui::SameLine();
    if (ImGui::Button("全体表示 (F)")) {
        editor.FrameNodes(false);
    }
    ImGui::SameLine();
    ImGui::Checkbox("説明", &editor.showHelpPanel_);

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    // 元に戻す・やり直す（戻せる履歴が無い間はボタンを押せなくする）
    ImGui::BeginDisabled(!editor.history_.CanUndo());
    if (ImGui::Button("元に戻す (Ctrl+Z)")) {
        editor.Undo();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!editor.history_.CanRedo());
    if (ImGui::Button("やり直す (Ctrl+Y)")) {
        editor.Redo();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    // このグラフをその場で試しに動かす・止める
    if (!editor.testRuntime_.IsRunning()) {
        if (ImGui::Button("実行")) {
            // 実行中のノードを光らせる時に、今見ているグラフが実行中のものか比べるためパスを覚えておく
            editor.testRuntime_.Start(&editor.graph_);
            editor.testRuntimeRootPath_ = editor.graphPath_;
        }
    } else {
        if (ImGui::Button("停止")) {
            // 実行状態を作り直して、途中の待ちや変数も捨てる
            editor.testRuntime_ = GraphRuntime { };
        }
        ImGui::SameLine();
        ImGui::TextColored(GraphEditorDrawingStyle::kRunningTextColor, "実行中...");
    }

    if (editor.statusTimer_ > 0.0f) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", editor.statusMessage_.c_str());
    }
}

/** @brief フレームの最後に、ノードの操作で予約した処理（サブグラフを開くなど）をまとめて行う */
void GraphEditorInteraction::FinishFrame(GraphEditor& editor, float realDeltaTime) {
    // Subgraphノードの開くボタン（ノード走査ループ内で押される）はここでまとめて処理する
    if (!editor.pendingOpenPath_.empty()) {
        std::string path = editor.pendingOpenPath_;
        editor.pendingOpenPath_.clear();
        if (editor.dirty_) {
            // ここはウィンドウのIDスコープ外なので、モーダルは次フレームのツールバー側で開く
            editor.pendingConfirmOpenPath_ = path;
            editor.requestConfirmOpen_ = true;
        } else {
            editor.Open(path);
        }
    }

    if (editor.testRuntime_.IsRunning()) {
        editor.testRuntime_.Update(realDeltaTime);
    }
}

/** @brief 1フレーム分の編集入力を処理する */
void GraphEditorInteraction::Update(GraphEditor& editor, engine::Input* input) {
    float realDeltaTime = 0.0f;
    if (!PrepareFrame(editor, input, realDeltaTime)) {
        return;
    }

    // 画面全体を覆う一枚のウィンドウとして開く背景を完全不透明にして後ろのゲーム画面を隠す
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::SetNextWindowBgAlpha(1.0f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;
    if (!ImGui::Begin("ノードエディタ", nullptr, flags)) {
        ImGui::End();
        return;
    }

    // 説明の開閉でキャンバスが上下に動かないよう、説明は右側のパネルへ分ける
    DrawToolbar(editor);
    editor.UpdateWarnings();
    editor.DrawInspection();
    ImGui::SameLine();
    graphics::editorUI::TextDisabledWrapped("F1: 表示/非表示    右クリック: 追加/メニュー/配線解除    ホイール: ズーム    中ドラッグ: パン    左ドラッグ: 範囲選択    F: 全体表示");
    ImGui::Separator();
    editor.DrawCanvas();
    if (editor.showHelpPanel_) {
        ImGui::SameLine();
        ImGui::BeginChild("GraphHelpPanel", ImVec2(0.0f, 0.0f), true);
        DrawGuidance(editor);
        ImGui::EndChild();
    }
    ImGui::End();
    editor.DrawVariablesPanel();
    FinishFrame(editor, realDeltaTime);
}

/** @brief ノード一件の表示と操作を処理する */
void GraphNodeRenderer::Draw(GraphEditor& editor, ImDrawList* dl, const ImVec2& origin,
    const std::string& id, GraphNode& node, void* opaqueState) {
    // ノードの大きさとピンの位置はズームに合わせる
    auto& state = *static_cast<GraphEditor::CanvasFrameState*>(opaqueState);
    const float nodeW = GraphEditorDrawingStyle::kBaseNodeWidth * editor.zoom_;
    const float pinR = GraphEditorDrawingStyle::kBasePinRadius * editor.zoom_;
    const float pinPad = GraphEditorDrawingStyle::kBasePinPad * editor.zoom_;
    const float boxPad = GraphEditorDrawingStyle::kNodeBoxPadding * editor.zoom_;

    ImGui::PushID(id.c_str());
    dl->ChannelsSetCurrent(GraphEditorDrawingStyle::kNodeFrontChannel);

    ImVec2 nodeScreenPos(origin.x + (editor.panOffsetX_ + node.editorX) * editor.zoom_, origin.y + (editor.panOffsetY_ + node.editorY) * editor.zoom_);
    ImGui::SetCursorScreenPos(nodeScreenPos);
    ImGui::BeginGroup();

    const NodeTypeSpec* spec = NodeRegistry::GetInstance()->FindSpec(node.type);

    // タイトル（このボタン自体がドラッグハンドル兼用）
    // 表示は日本語名、マウスを乗せるとグラフJSONに書く type 名と説明を出す
    const std::string title = GraphNodeLabels::TypeName(node.type) + "##title";
    ImGui::PushStyleColor(ImGuiCol_Button, TitleColor(spec));
    ImGui::Button(title.c_str(), ImVec2(nodeW, 0));
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        // 点検対象では枠の判定で説明と理由をまとめて出す
        if (!editor.warnings_.contains(id)) {
            graphics::editorUI::TooltipWrapped("%s\n%s", node.type.c_str(), spec ? spec->description.c_str() : "");
        }
    }
    // タイトルを掴んだら選択して移動の準備をする（移動は全ノードを描いた後に UpdateNodeDrag で進める）
    if (ImGui::IsItemActivated() && !editor.panning_) {
        editor.BeginNodePress(id);
        state.nodePressedThisFrame = true;
    }
    ImGui::TextDisabled("%s", id.c_str());

    // パラメータの入力欄とデータ入力ピン
    editor.DrawNodeParams(dl, id, node, nodeScreenPos, state);

    // 別グラフを参照するノードは中身をワンクリックで開けるようにする（editor.Open()はループ後に遅延実行）
    if (spec && !spec->openPathParam.empty()) {
        if (ImGui::SmallButton("サブグラフを開く")) {
            auto it = node.params.find(spec->openPathParam);
            if (it != node.params.end()) {
                editor.pendingOpenPath_ = AsString(it->second);
            }
        }
    }

    ImGui::EndGroup();
    ImVec2 nodeMin = ImGui::GetItemRectMin();
    ImVec2 nodeMax = ImGui::GetItemRectMax();
    editor.nodeRects_[id] = { nodeMin, nodeMax };

    // ノードの外枠（パラメータまで含む矩形に余白を足したもの）
    const ImVec2 interactionMin(nodeMin.x - boxPad, nodeMin.y - boxPad * GraphEditorDrawingStyle::kNodeTopPaddingRatio);
    const ImVec2 interactionMax(nodeMax.x + boxPad, nodeMax.y + boxPad);

    // 背景（枠・塗り、開始ノードは緑枠で強調）
    dl->ChannelsSetCurrent(GraphEditorDrawingStyle::kNodeBackChannel);
    ImU32 bg = editor.IsNodeSelected(id) ? GraphEditorDrawingStyle::kSelectedBackgroundColor : GraphEditorDrawingStyle::kBackgroundColor;
    dl->AddRectFilled(interactionMin, interactionMax, bg, GraphEditorDrawingStyle::kNodeCornerRadius * editor.zoom_);
    ImU32 border = (id == editor.graph_.startNodeId) ? GraphEditorDrawingStyle::kStartColor : GraphEditorDrawingStyle::kBorderColor;
    if (editor.warnings_.contains(id)) {
        border = kWarningBorderColor;
    }
    float borderThickness = GraphEditorDrawingStyle::kNodeBorderThickness * editor.zoom_;
    if (state.viewingActiveGraph && id == state.activeRunNodeId) {
        border = GraphEditorDrawingStyle::kRunningColor;
        borderThickness = GraphEditorDrawingStyle::kRunningBorderThickness * editor.zoom_; // Run中は太くして一目で分かるようにする
    }
    dl->AddRect(interactionMin, interactionMax, border, GraphEditorDrawingStyle::kNodeCornerRadius * editor.zoom_, 0, borderThickness);

    // 開始ノードは緑枠だけだと気づきにくいため、枠の上にラベルも出す
    if (id == editor.graph_.startNodeId) {
        ImVec2 labelPos(nodeMin.x - boxPad, nodeMin.y - boxPad * GraphEditorDrawingStyle::kNodeTopPaddingRatio - ImGui::GetFontSize() - GraphEditorDrawingStyle::kNodeLabelGap * editor.zoom_);
        dl->AddText(labelPos, GraphEditorDrawingStyle::kStartColor, "開始");
    }

    dl->ChannelsSetCurrent(GraphEditorDrawingStyle::kNodeFrontChannel);

    // ピンは描いて記録するだけにする（掴む・つなぐ判定は全ピンが出そろった後に近い物を選ぶ）
    // 実行入力ピン（左上。タイトル行の高さに合わせる）
    ImVec2 inPin(nodeMin.x - pinPad, nodeMin.y + GraphEditorDrawingStyle::kPinRowOffset * editor.zoom_);
    dl->AddCircleFilled(inPin, pinR, GraphEditorDrawingStyle::kInputPinColor);
    state.pins.push_back({ GraphPinKind::kExecIn, id, GraphEditorDrawingStyle::kExecInPinName, inPin, pinR, GraphValueType::kAny });

    // 実行出力ピン（右上。Ifのみtrue/falseの2つ、他は1つ）
    auto drawOutputPin = [&](const char* pinKind, ImVec2 pos, ImU32 col) {
        dl->AddCircleFilled(pos, pinR, col);
        state.pins.push_back({ GraphPinKind::kExecOut, id, pinKind, pos, pinR, GraphValueType::kAny });
    };

    if (spec && spec->branches) {
        drawOutputPin("true", ImVec2(nodeMax.x + pinPad, nodeMin.y + (nodeMax.y - nodeMin.y) * GraphEditorDrawingStyle::kTruePinHeightRatio), GraphEditorDrawingStyle::kTruePinColor);
        drawOutputPin("false", ImVec2(nodeMax.x + pinPad, nodeMin.y + (nodeMax.y - nodeMin.y) * GraphEditorDrawingStyle::kFalsePinHeightRatio), GraphEditorDrawingStyle::kFalsePinColor);
    } else {
        drawOutputPin("next", ImVec2(nodeMax.x + pinPad, nodeMin.y + GraphEditorDrawingStyle::kPinRowOffset * editor.zoom_), GraphEditorDrawingStyle::kOutputPinColor);
    }

    // データ出力ピン（右下。値を出力するノードだけ）
    if (spec && spec->hasOutput) {
        ImVec2 outPin(nodeMax.x + pinPad, nodeMax.y - GraphEditorDrawingStyle::kPinRowOffset * editor.zoom_);

        float dataPinR = pinR * GraphEditorDrawingStyle::kDataPinRadiusRatio;
        dl->AddCircleFilled(outPin, dataPinR, GraphEditorDrawingStyle::ColorForType(spec->outputType));
        state.pins.push_back({ GraphPinKind::kDataOut, id, "", outPin, dataPinR, spec->outputType });
    }

    ImGui::PopID();
}
} // namespace engine::game
#endif

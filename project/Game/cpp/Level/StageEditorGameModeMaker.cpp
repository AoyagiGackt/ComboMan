/**
 * @file StageEditorGameModeMaker.cpp
 * @brief ステージエディタで、今のゲームの型を写して新しい型を作る・直す欄
 * @note クラスは StageEditor のまま。JSON を手で書かずに、カメラ・道・奥行きの値をスライダーで決めて保存する
 */
#include "StageEditor.h"
#ifdef USE_IMGUI
#include "EditorFlowRow.h"
#include "EditorUI.h"
#include "EditorUiStyle.h"
#include "GameModeLibrary.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <imgui.h>
#include <string>

namespace engine::game {
namespace {
// スライダーの範囲（ワールド単位、速さはワールド単位/秒）
constexpr float kMinCameraDistance = 2.0f;
constexpr float kMaxCameraDistance = 40.0f;
constexpr float kMinCameraHeight = -5.0f;
constexpr float kMaxCameraHeight = 15.0f;
constexpr float kMaxLookAhead = 20.0f;
constexpr float kMinPathHalfSize = 1.0f;
constexpr float kMaxPathHalfWidth = 20.0f;
constexpr float kMaxPathHalfHeight = 15.0f;
constexpr float kMinDepthSpeed = 1.0f;
constexpr float kMaxDepthSpeed = 20.0f;
constexpr float kMaxDepthRange = 20.0f;
constexpr float kMaxEnemyDepthSpeed = 10.0f;
constexpr float kMinHitHalfDepth = 0.2f;
constexpr float kMaxHitHalfDepth = 5.0f;
constexpr float kDescriptionLines = 3.0f; ///< 説明の入力欄の高さ（行数）
constexpr const char* kCopySuffix = "_copy"; ///< 写した型の id に付ける名前
constexpr const char* kCopyNameSuffix = "（写し）"; ///< 写した型の名前に付ける文字

/** @brief カメラの部品の選び方の1行 */
struct CameraChoice {
    const char* type; ///< JSON に書く名前
    const char* label; ///< エディタに出す名前
};
constexpr CameraChoice kCameraChoices[] = {
    { "side", "横から映す" },
    { "behind", "後ろから映す" },
};

/** @brief 型の id に使える文字だけか（半角英数字と _、空は不可） */
bool IsValidModeId(const std::string& id) {
    return !id.empty() && std::all_of(id.begin(), id.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
    });
}

/** @brief 直前の部品にマウスが乗っていれば説明を出す */
void ShowHelp(const char* help) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        graphics::editorUI::TooltipWrapped("%s", help);
    }
}

/** @brief 幅いっぱいのスライダーを、上に名前を出して描く（狭いパネルでも名前が切れないように） */
bool WideSlider(const char* label, float* value, float minValue, float maxValue, const char* help) {
    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(-1.0f);
    const std::string id = std::string("##") + label;
    const bool changed = ImGui::SliderFloat(id.c_str(), value, minValue, maxValue, "%.1f");
    ShowHelp(help);
    return changed;
}
} // namespace

/** @brief 今の型を写して新しい型を作る・直す欄 */
void StageEditor::RenderGameModeMaker() {
    if (!ImGui::CollapsingHeader("新しい型を作る（今の型を写して直す）")) {
        return;
    }
    GameModeLibrary* library = GameModeLibrary::GetInstance();
    // まだ作り始めていなければ、今の型を写して始めるボタンだけを出す
    if (!modeDraftOpen_) {
        graphics::editorUI::TextDisabledWrapped("今のレベルの型を写して、カメラ・道・奥行きの値をスライダーで直し、新しい型として保存します。JSON を書く必要はありません。");
        if (ImGui::Button("今の型を写して作り始める", ImVec2(-1.0f, 0.0f))) {
            modeDraft_ = library->Find(gameMode_);
            modeDraft_.id += kCopySuffix;
            modeDraft_.name += kCopyNameSuffix;
            strncpy_s(modeDraftId_, modeDraft_.id.c_str(), _TRUNCATE);
            strncpy_s(modeDraftName_, modeDraft_.name.c_str(), _TRUNCATE);
            strncpy_s(modeDraftDescription_, modeDraft_.description.c_str(), _TRUNCATE);
            modeDraftOpen_ = true;
        }
        return;
    }

    // 名前と説明
    ImGui::TextUnformatted("型の名前（エディタに出る）");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##modeName", modeDraftName_, sizeof(modeDraftName_));
    ImGui::TextUnformatted("ファイルの名前（半角英数字と _）");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##modeId", modeDraftId_, sizeof(modeDraftId_));
    const std::string id = modeDraftId_;
    const bool validId = IsValidModeId(id);
    if (!validId) {
        ImGui::TextColored(engine::editor::uiStyle::kWarningColor, "ファイルの名前は半角英数字と _ だけにしてください");
    } else if (library->Contains(id)) {
        ImGui::TextColored(engine::editor::uiStyle::kCautionColor, "同じ名前の型があります。保存すると上書きします");
    }
    ImGui::TextUnformatted("説明");
    ImGui::InputTextMultiline("##modeDescription", modeDraftDescription_, sizeof(modeDraftDescription_),
        ImVec2(-1.0f, ImGui::GetTextLineHeight() * kDescriptionLines), ImGuiInputTextFlags_WordWrap);

    // カメラ
    ImGui::SeparatorText("カメラ");
    for (const CameraChoice& choice : kCameraChoices) {
        if (ImGui::RadioButton(choice.label, modeDraft_.cameraType == choice.type)) {
            modeDraft_.cameraType = choice.type;
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();
    if (modeDraft_.cameraType == "side") {
        ImGui::Checkbox("ステージの端でカメラを止める", &modeDraft_.useStageBoundsCamera);
        ShowHelp("今までの横スクロールと同じ映し方です。切ると下の距離や高さで映します");
    }
    const bool customCamera = !(modeDraft_.cameraType == "side" && modeDraft_.useStageBoundsCamera);
    ImGui::BeginDisabled(!customCamera);
    WideSlider("見る物からの距離", &modeDraft_.camera.distance, kMinCameraDistance, kMaxCameraDistance, "大きいほど引いて広く映します");
    WideSlider("カメラの高さ", &modeDraft_.camera.height, kMinCameraHeight, kMaxCameraHeight, "見る物よりどれだけ上から映すかです");
    WideSlider("見る点の高さ", &modeDraft_.camera.targetHeight, kMinCameraHeight, kMaxCameraHeight, "画面の真ん中に来る高さです");
    WideSlider("先を見る量", &modeDraft_.camera.lookAhead, 0.0f, kMaxLookAhead, "進む向きの少し先を真ん中にします。大きいほど前が広く見えます");
    ImGui::EndDisabled();

    // 道に沿って自動で進むか
    ImGui::SeparatorText("自動で進む");
    ImGui::Checkbox("レベルの道に沿って自動で進む", &modeDraft_.path.follow);
    ShowHelp("このレベルのタブで引いた道の上を、点の速さで画面が進みます");
    if (modeDraft_.path.follow) {
        WideSlider("横に離れてよい幅", &modeDraft_.path.playerHalfWidth, kMinPathHalfSize, kMaxPathHalfWidth, "道の上の今の位置から、プレイヤーが前後に離れてよい幅です");
        WideSlider("上に離れてよい幅", &modeDraft_.path.playerHalfHeight, kMinPathHalfSize, kMaxPathHalfHeight, "道の上の今の位置から、プレイヤーが上へ離れてよい幅です");
    }

    // 奥行きにも動くか
    ImGui::SeparatorText("奥行き");
    ImGui::Checkbox("奥行き方向にも動ける", &modeDraft_.player.depthMovement);
    ShowHelp("前後のキーで先へ進み、左右のキーで奥行き方向へ動くようになります。後ろから映す時に向いています");
    if (modeDraft_.player.depthMovement) {
        WideSlider("奥行きへ動く速さ", &modeDraft_.player.depthSpeed, kMinDepthSpeed, kMaxDepthSpeed, "左右のキーで奥行き方向へ動く速さです");
        WideSlider("手前の端", &modeDraft_.player.depthMin, -kMaxDepthRange, 0.0f, "奥行きの動ける範囲の手前の端です。床の奥行きもここまで広げます");
        WideSlider("奥の端", &modeDraft_.player.depthMax, 0.0f, kMaxDepthRange, "奥行きの動ける範囲の奥の端です。床の奥行きもここまで広げます");
        WideSlider("当たる奥行きの幅", &modeDraft_.player.hitHalfDepth, kMinHitHalfDepth, kMaxHitHalfDepth, "これより奥行きが離れていると、攻撃が当たりません");
        ImGui::Checkbox("敵も同じ奥行きへ寄ってくる", &modeDraft_.player.enemiesFollowDepth);
        if (modeDraft_.player.enemiesFollowDepth) {
            WideSlider("敵が寄ってくる速さ", &modeDraft_.player.enemyDepthSpeed, 0.0f, kMaxEnemyDepthSpeed, "大きいほど敵がすぐに同じ奥行きへ来ます");
        }
    }

    // 保存してこのレベルに使う・やめる（入りきる分だけ横に並べる）
    ImGui::Separator();
    engine::editor::FlowRow row;
    ImGui::BeginDisabled(!validId || modeDraftName_[0] == '\0');
    if (row.Button("保存してこのレベルに使う")) {
        modeDraft_.id = id;
        modeDraft_.name = modeDraftName_;
        modeDraft_.description = modeDraftDescription_;
        if (library->Save(modeDraft_)) {
            RecordUndoSnapshotNow();
            gameMode_ = id == GameModeLibrary::kDefaultId ? std::string() : id;
            statusMessage_ = "型を保存して、このレベルに使いました: " + modeDraft_.name;
            modeDraftOpen_ = false;
        } else {
            statusMessage_ = "型を保存できませんでした";
        }
        statusTimer_ = kStatusLongSeconds;
    }
    ImGui::EndDisabled();
    if (row.Button("やめる")) {
        modeDraftOpen_ = false;
    }
}

} // namespace engine::game
#endif

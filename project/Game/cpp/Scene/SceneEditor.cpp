/**
 * @file SceneEditor.cpp
 * @brief 実行中のライブパラメータ調整パネル（SceneEditor、Stateパターンによる選択種別ごとの表示切替）の実装
 */
#include "SceneEditor.h"
#include "EngineContext.h"
#include "TextBufferCapacity.h"
#include "GameConstants.h"
#include "Input.h"
#include "JsonHelper.h"
#include "ParticleManager.h"
#include "RunData.h"
#include "SceneFlow.h"
#include "SceneManager.h"
#include "ScoreManager.h"
#include "RevolvedMeshBase.h"
#include <algorithm>
#include <iterator>
#include <string>
#ifdef USE_IMGUI
#include "EditorHub.h"
#include "EditorUI.h"
#include "EditorFlowRow.h"
#include "FieldMemory.h"
#include <imgui.h>
#endif
using namespace engine::graphics;

namespace engine::game {

// 内部ヘルパー

namespace {
// ImGuiのドラッグ操作1pxあたりの変化量
constexpr float kDragStepPosition = 0.1f;
constexpr float kDragStepFinePosition = 0.05f;
constexpr float kDragStepRotation = 0.01f;
constexpr float kDragStepScale = 0.01f;
constexpr float kDragStepRadius = 0.05f;
constexpr float kDragStepPixel = 1.0f;
// 編集値の下限・上限
constexpr float kMinEditableSize = 0.01f;
constexpr float kMinEditableHumanScale = 0.001f;
constexpr float kMaxObjectScale = 20.0f;
constexpr float kMaxRadius = 50.0f;
constexpr float kMaxHumanScale = 100.0f;
constexpr float kMaxAnimSpeed = 5.0f;
constexpr float kMaxParticleScale = 10.0f;
constexpr int kMaxParticleCount = 1024;
constexpr float kRadiusGap = 0.01f; // 内径と外径が重ならないように空ける最小差
#ifdef USE_IMGUI
// 見出し色
constexpr ImVec4 kSavedTextColor = { 0.2f, 1.0f, 0.4f, 1.0f };
constexpr ImVec4 kRingHeaderColor = { 0.4f, 1.0f, 0.8f, 1.0f };
constexpr ImVec4 kCylinderHeaderColor = { 0.8f, 0.6f, 1.0f, 1.0f };
constexpr ImVec4 kSkydomeHeaderColor = { 0.5f, 0.8f, 1.0f, 1.0f };
constexpr ImVec4 kHumanHeaderColor = { 0.4f, 0.8f, 1.0f, 1.0f };
#endif
constexpr float kSavedMessageSeconds = 1.5f;
constexpr Vector2 kNewUIElementSize = { 100.0f, 100.0f };
// スナップの間隔の範囲（ピクセル）
constexpr int kMinUISnapStep = 1;
constexpr int kMaxUISnapStep = 128;
// 白パーティクルの再放出
constexpr float kWhiteParticleSpread = 20.0f;
constexpr float kWhiteParticleSpeedMin = 2.0f;
constexpr float kWhiteParticleSpeedMax = 5.0f;
// カメラの平滑化に使うフレーム数の範囲
constexpr int kMinSmoothFrames = 1;
constexpr int kMaxSmoothFrames = 60;

#ifdef USE_IMGUI
/** @brief シーンエディタの項目の、開いた時の値を覚えておく入れ物（パネルを開き直すと覚え直す） */
engine::editor::FieldMemory& SceneFields() {
    static engine::editor::FieldMemory sFields;
    return sFields;
}

/** @brief カメラの平滑化のフレーム数のスライダーを出す（変えたら古い平均が混ざらないよう履歴を消す） */
void DrawSmoothFramesSlider(const SceneEditor::EditContext& ctx) {
    if (SceneFields().Track(ImGui::SliderInt("なめらかさ(フレーム)", ctx.cameraSmoothFrames, kMinSmoothFrames, kMaxSmoothFrames),
            *ctx.cameraSmoothFrames, "カメラが目標へ追いつくまでのフレーム数です。大きいほどゆっくり、なめらかに動きます")) {
        ctx.cameraPosHistory->clear();
        ctx.cameraRotHistory->clear();
    }
}

/** @brief 回転体メッシュに共通する位置・回転・拡大率を編集し、変化した値を実体へ反映する */
void RenderMeshTransform(RevolvedMeshBase& mesh, Vector3& position, Vector3& rotation, float& scale) {
    if (SceneFields().Track(ImGui::DragFloat3("位置", &position.x, kDragStepPosition), position, "置く場所です（横・高さ・奥行き）")) {
        mesh.SetPosition(position);
    }
    if (SceneFields().Track(ImGui::DragFloat3("向き", &rotation.x, kDragStepRotation), rotation, "それぞれの軸まわりの回転です")) {
        mesh.SetRotation(rotation);
    }
    if (SceneFields().Track(ImGui::DragFloat("大きさ", &scale, kDragStepScale, kMinEditableSize, kMaxObjectScale), scale, "全体を何倍にするかです")) {
        mesh.SetScale(scale);
    }
}
#endif
}

#ifdef USE_IMGUI
using engine::graphics::editorUI::OpenFileDialog;
using engine::graphics::editorUI::ToProjectRelativePath;
#endif

/** @brief 書き込み結果を覚え、失敗した保存先を表示できるようにする */
bool SceneEditor::SetSaveResult(bool saved, const char* path) {
    savedTimer_ = saved ? kSavedMessageSeconds : 0.0f;
    saveError_ = saved ? std::string() : std::string("保存に失敗しました: ") + path;
    return saved;
}

/** @brief 保存成功を短時間、失敗した保存先は次の保存まで表示する */
void SceneEditor::DrawSaveResult() const {
#ifdef USE_IMGUI
    if (!saveError_.empty()) {
        graphics::editorUI::TextDisabledWrapped("%s", saveError_.c_str());
    } else if (savedTimer_ > 0.0f) {
        ImGui::TextColored(kSavedTextColor, "保存しました");
    }
#endif
}

// State 遷移

// Hierarchy でオブジェクトが選ばれたとき、対応する State クラスに切り替える
// こうすることで Inspector パネルに表示する内容がオブジェクト種別に応じて変わる
// ══════════════════════════════════════════════════════
// 状態管理とパネル描画
// ══════════════════════════════════════════════════════

/** @brief 選択の種類を切り替え、対応するインスペクタの状態オブジェクトを作り直す */
void SceneEditor::ChangeState(Selection sel) {
    // 選んだ対象を覚え、UI要素の選択位置は選び直しにする
    selection_ = sel;
    selectionIndex_ = -1;
    // 対象ごとの編集状態の作り方（Selection の並び順と同じ）
    using StateFactory = std::unique_ptr<IEditorState> (*)();
    static constexpr StateFactory kFactories[] = {
        // 何も選んでいない
        [] {
            return std::unique_ptr<IEditorState>(std::make_unique<NoneState>());
        },
        // カメラ
        [] {
            return std::unique_ptr<IEditorState>(std::make_unique<CameraState>());
        },
        // 画面上のUI要素
        [] {
            return std::unique_ptr<IEditorState>(std::make_unique<UIElementState>());
        },
        // リング
        [] {
            return std::unique_ptr<IEditorState>(std::make_unique<RingState>());
        },
        // 円柱
        [] {
            return std::unique_ptr<IEditorState>(std::make_unique<CylinderState>());
        },
        // 天球
        [] {
            return std::unique_ptr<IEditorState>(std::make_unique<SkydomeState>());
        },
        // 人型モデル
        [] {
            return std::unique_ptr<IEditorState>(std::make_unique<HumanState>());
        },
        // パーティクル
        [] {
            return std::unique_ptr<IEditorState>(std::make_unique<ParticlesState>());
        },
    };
    // 表の外の値が来たら何も選んでいない状態にする
    const size_t index = static_cast<size_t>(sel);
    currentState_ = (index < std::size(kFactories)) ? kFactories[index]() : kFactories[0]();
}

// メインエントリ

// 毎フレーム呼ばれるUSE_IMGUI ビルドのときだけ各パネルを描画する
// リリースビルドでは引数を無視して何もしない
void SceneEditor::Update(const EditContext& ctx, engine::Input* input) {
#ifdef USE_IMGUI
    // F3 か、F8 のエディタ一覧のボタンで開閉する
    const bool hubToggle = editor::EditorHub::GetInstance()->Offer("scene", "シーンエディタ", "F3",
        "画面に出ている物の位置やカメラを動かし、スコアやシーンの切り替えも試せます", visible_);
    if ((input && input->TriggerKey(DIK_F3)) || hubToggle) {
        visible_ = !visible_;
        if (visible_) {
            SceneFields().Forget();
        }
    }
    if (!visible_) {
        return;
    }
    RenderHierarchy(ctx); // 左 オブジェクト一覧
    RenderInspector(ctx); // 右 選択中オブジェクトのプロパティ
    RenderSceneControls(ctx); // 左下 スコア・ゲーム時刻・シーン操作
    RenderCameraControl(ctx); // 上中央 カメラ位置の簡易操作
    savedTimer_ = (std::max)(0.0f, savedTimer_ - gameConstants::kFrameDeltaTime);
    UpdateUIGizmo(); // ゲーム画面の上のUI要素の枠
#else
    (void)ctx;
    (void)input;
#endif
}

// Hierarchy パネル（画面左）

// シーン内のオブジェクト一覧を表示する
// クリックすると Inspector パネルの内容が切り替わる
void SceneEditor::RenderHierarchy(const EditContext& ctx) {
#ifdef USE_IMGUI
    ImGui::SetNextWindowPos(ImVec2(kHierarchyX, kHierarchyY), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(kHierarchyW, kHierarchyH), ImGuiCond_Once);
    ImGui::Begin("配置一覧###Hierarchy");

    // Selectable を選んだら ChangeState を呼んで Inspector を切り替える
    auto selectable = [&](const char* label, Selection type) {
        if (ImGui::Selectable(label, selection_ == type)) {
            ChangeState(type);
        }
    };

    // Ring/Cylinder/Human/White Particles はこのシーンのEditContextに配線されておらず
    // 選んでも無効表示にしかならないため、実際に編集できる項目だけを並べる
    selectable("カメラ", Selection::kCamera);
    selectable("空", Selection::kSkydome);

    // UI Elements は可変長リストなのでツリーノードで折りたたみ表示する
    char uiHeader[textBufferCapacity::kSectionTitle];
    snprintf(uiHeader, sizeof(uiHeader), "画面の部品（%d 件）", (int)uiElements_.size());
    bool uiOpen = ImGui::TreeNodeEx(uiHeader);
    ImGui::SameLine();
    // + ボタンを押すと新しい UI スプライトを追加する
    if (ImGui::SmallButton("+##addUI")) {
        UIEntry entry;
        entry.name = "画面の部品 " + std::to_string(uiElements_.size() + 1);
        entry.texPath = "";
        entry.sprite = std::make_unique<Sprite>();
        entry.sprite->Initialize(ctx.spriteCommon, entry.texPath);
        entry.sprite->SetPosition({ gameConstants::kScreenCenterX, gameConstants::kScreenCenterY }); // 画面中央に配置
        entry.sprite->SetSize(kNewUIElementSize);
        uiElements_.push_back(std::move(entry));
        // 要素の数が変わると履歴を復元できないので消す
        uiHistory_.Clear();
        uiDirty_ = true;
    }
    if (uiOpen) {
        for (int i = 0; i < (int)uiElements_.size(); ++i) {
            bool sel = (selection_ == Selection::kUIElement && selectionIndex_ == i);
            char label[textBufferCapacity::kStatus];
            snprintf(label, sizeof(label), "  %s", uiElements_[i].name.c_str());
            if (ImGui::Selectable(label, sel)) {
                SelectUIElement(i);
            }
        }
        ImGui::TreePop();
    }

    // Save ボタン 選択中オブジェクトのパラメータを JSON に保存する
    ImGui::Separator();
    if (savedTimer_ <= 0.0f) {
        // 保存先があるのはカメラとUI要素だけなので、それ以外は押せなくする
        ImGui::BeginDisabled(selection_ != Selection::kCamera && selection_ != Selection::kUIElement);
        if (ImGui::Button("保存", ImVec2(-1, 0))) {
            if (selection_ == Selection::kCamera) {
                SaveCameraParams(ctx);
            }
            if (selection_ == Selection::kUIElement) {
                SaveUILayout();
            }
        }
        ImGui::EndDisabled();
    }
    DrawSaveResult();
    if (uiDirty_) {
        ImGui::TextDisabled("画面の部品は未保存です");
    }

    ImGui::End();
#endif
}

// Inspector パネル（画面右）

// 現在選ばれているオブジェクトの詳細プロパティを表示する
// 描画の実体は currentState_（各 State クラスの RenderInspector）に委譲する
void SceneEditor::RenderInspector(const EditContext& ctx) {
#ifdef USE_IMGUI
    // UIElement を選んでいるのにインデックスが範囲外なら選択を解除する
    if (selection_ == Selection::kUIElement && (selectionIndex_ < 0 || selectionIndex_ >= (int)uiElements_.size())) {
        ChangeState(Selection::kNone);
    }

    ImGui::SetNextWindowPos(ImVec2(kInspectorX, kInspectorY), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(kInspectorW, kInspectorH), ImGuiCond_Once);
    ImGui::Begin("選択中の項目###Inspector");

    if (currentState_) {
        currentState_->RenderInspector(ctx, *this);
    } else {
        // State が初期化されていない場合の安全処理
        currentState_ = std::make_unique<NoneState>();
        currentState_->RenderInspector(ctx, *this);
    }

    ImGui::End();
#endif
}

// Scene Controls パネル（画面左下）

// スコアの確認・ゲーム時刻の表示・シーン切り替えボタンをまとめたパネル
void SceneEditor::RenderSceneControls(const EditContext& ctx) {
#ifdef USE_IMGUI
    ImGui::SetNextWindowPos(ImVec2(kSceneCtrlX, kSceneCtrlY), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(kSceneCtrlW, kSceneCtrlH), ImGuiCond_Once);
    ImGui::Begin("シーン操作###Scene Controls");

    // スコアランキングの表示（折りたたみ）
    if (ImGui::CollapsingHeader("スコア")) {
        ImGui::Text("現在のスコア　%d", RunData::GetInstance()->GetScore());
        const auto ranking = ScoreManager::GetInstance()->GetRankingSnapshot();
        if (ranking.empty()) {
            ImGui::TextDisabled("記録はありません");
        }
        for (int i = 0; i < (int)ranking.size(); ++i) {
            ImGui::Text("  %2d. %d", i + 1, ranking[i]);
        }
        constexpr const char* kResetPopup = "ランキング削除の確認";
        if (ImGui::Button("ランキングを全部消す")) {
            ImGui::OpenPopup(kResetPopup);
        }
        const std::string message = "保存済みのランキング " + std::to_string(ranking.size()) + " 件を全部消します。";
        if (graphics::editorUI::ConfirmModal(kResetPopup, message.c_str(), "全部消す") == graphics::editorUI::ConfirmResult::kOk) {
            ScoreManager::GetInstance()->ResetAllScores();
        }
    }

    // シーン切り替えボタン（折りたたみ）
    if (ImGui::CollapsingHeader("シーンの切り替え")) {
        engine::editor::FlowRow actions;
        if (actions.Button("ゲームクリア")) {
            if (ctx.requestClear) {
                *ctx.requestClear = true;
            }
        }

        if (actions.Button("ゲームオーバー")) {
            SceneFlow::GetInstance()->Transition("GAMEPLAY", "gameover", "GAMEOVER");
        }
    }

    ImGui::End();
#endif
}

// Camera Control パネル（画面上中央）

// カメラの目標位置・角度・スムージングフレーム数を素早く調整するための簡易パネル
// Inspector の Camera よりも手軽に操作できるよう別パネルとして用意している
void SceneEditor::RenderCameraControl(const EditContext& ctx) {
#ifdef USE_IMGUI
    ImGui::SetNextWindowPos(ImVec2(kCamCtrlX, kCamCtrlY), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(kCamCtrlW, kCamCtrlH), ImGuiCond_Once);
    ImGui::Begin("カメラ操作###Camera Control");

    // ドラッグスライダーで目標座標・角度を変更する
    SceneFields().Track(ImGui::DragFloat3("位置", &ctx.cameraTargetPos->x, kDragStepPosition), *ctx.cameraTargetPos, "カメラが向かう場所です");
    SceneFields().Track(ImGui::DragFloat3("向き", &ctx.cameraTargetRot->x, kDragStepRotation), *ctx.cameraTargetRot, "カメラが向く角度です");

    DrawSmoothFramesSlider(ctx);

    ImGui::Spacing();

    // 原点にリセットするボタン
    engine::editor::FlowRow camera;
    if (camera.Button("原点に戻す")) {
        *ctx.cameraTargetPos = { 0.0f, 0.0f, 0.0f };
        *ctx.cameraTargetRot = { 0.0f, 0.0f, 0.0f };
        ctx.cameraPosHistory->clear();
        ctx.cameraRotHistory->clear();
    }

    if (camera.Button("保存##cam")) {
        SaveCameraParams(ctx);
    } // JSON に現在値を保存
    if (camera.Button("読み込み##cam")) {
        LoadCameraParams(ctx);
    } // JSON から前回値を復元
    DrawSaveResult();

    ImGui::End();
#endif
}

// JSON 永続化

// カメラの位置・角度・スムージングフレーム数を JSON ファイルに書き出す
// ══════════════════════════════════════════════════════
// 設定の保存と読み込み
// ══════════════════════════════════════════════════════

// IEditorState 実装（各オブジェクト種別ごとの Inspector 描画）

// 何も選んでいないときの Inspector 表示
// ══════════════════════════════════════════════════════
// 選択状態別の詳細設定
// ══════════════════════════════════════════════════════

/** @brief 何も選んでいないときのインスペクタに案内文を表示する */
void SceneEditor::NoneState::RenderInspector(const EditContext&, SceneEditor&) {
#ifdef USE_IMGUI
    ImGui::TextDisabled("何も選んでいません");
    graphics::editorUI::TextDisabledWrapped("配置一覧で編集する物をクリックしてください。");
#endif
}

// Camera を選んでいるときの Inspector 表示
void SceneEditor::CameraState::RenderInspector(const EditContext& ctx, SceneEditor& editor) {
#ifdef USE_IMGUI
    ImGui::TextColored(ImVec4(1, 1, 0, 1), "カメラ");
    ImGui::Separator();
    SceneFields().Track(ImGui::DragFloat3("位置", &ctx.cameraTargetPos->x, kDragStepPosition), *ctx.cameraTargetPos, "カメラが向かう場所です"); // ドラッグで位置を変更
    SceneFields().Track(ImGui::DragFloat3("向き", &ctx.cameraTargetRot->x, kDragStepRotation), *ctx.cameraTargetRot, "カメラが向く角度です"); // ドラッグで角度を変更
    DrawSmoothFramesSlider(ctx);
    ImGui::Separator();
    if (ImGui::Button("保存##inspCam")) {
        editor.SaveCameraParams(ctx);
    }
    editor.DrawSaveResult();
#endif
}

// Ring を選んでいるときの Inspector 表示
void SceneEditor::RingState::RenderInspector(const EditContext& ctx, SceneEditor&) {
#ifdef USE_IMGUI
    ImGui::TextColored(kRingHeaderColor, "輪");
    ImGui::Separator();
    if (!ctx.ring || !ctx.ringPosition) {
        ImGui::TextDisabled("輪は使用できません");
        return;
    }
    RenderMeshTransform(*ctx.ring, *ctx.ringPosition, *ctx.ringRotation, *ctx.ringScale);
    ImGui::Separator();
    if (SceneFields().Track(ImGui::DragFloat("内側の半径", ctx.ringInnerRadius, kDragStepRadius, kMinEditableSize, *ctx.ringOuterRadius - kRadiusGap),
            *ctx.ringInnerRadius, "輪の穴の大きさです。外側の半径より小さくなります")) {
        ctx.ring->SetInnerRadius(*ctx.ringInnerRadius);
    }
    if (SceneFields().Track(ImGui::DragFloat("外側の半径", ctx.ringOuterRadius, kDragStepRadius, *ctx.ringInnerRadius + kRadiusGap, kMaxRadius),
            *ctx.ringOuterRadius, "輪の外側の大きさです")) {
        ctx.ring->SetOuterRadius(*ctx.ringOuterRadius);
    }
    ImGui::Separator();
    if (SceneFields().Track(ImGui::ColorEdit4("色", &ctx.ringColor->x), *ctx.ringColor, "輪の色と透明度です")) {
        ctx.ring->SetColor(*ctx.ringColor);
    }
#endif
}

// Cylinder を選んでいるときの Inspector 表示
void SceneEditor::CylinderState::RenderInspector(const EditContext& ctx, SceneEditor&) {
#ifdef USE_IMGUI
    ImGui::TextColored(kCylinderHeaderColor, "円柱");
    ImGui::Separator();
    if (!ctx.cylinder || !ctx.cylinderPosition) {
        ImGui::TextDisabled("円柱は使用できません");
        return;
    }
    RenderMeshTransform(*ctx.cylinder, *ctx.cylinderPosition, *ctx.cylinderRotation, *ctx.cylinderScale);
    ImGui::Separator();
    if (SceneFields().Track(ImGui::DragFloat("上の半径", ctx.cylinderTopRadius, kDragStepRadius, kMinEditableSize, kMaxRadius),
            *ctx.cylinderTopRadius, "円柱の上の面の大きさです")) {
        ctx.cylinder->SetTopRadius(*ctx.cylinderTopRadius);
    }
    if (SceneFields().Track(ImGui::DragFloat("下の半径", ctx.cylinderBottomRadius, kDragStepRadius, kMinEditableSize, kMaxRadius),
            *ctx.cylinderBottomRadius, "円柱の下の面の大きさです")) {
        ctx.cylinder->SetBottomRadius(*ctx.cylinderBottomRadius);
    }
    if (SceneFields().Track(ImGui::DragFloat("高さ", ctx.cylinderHeight, kDragStepRadius, kMinEditableSize, kMaxRadius),
            *ctx.cylinderHeight, "円柱の高さです")) {
        ctx.cylinder->SetHeight(*ctx.cylinderHeight);
    }
    ImGui::Separator();
    if (SceneFields().Track(ImGui::ColorEdit4("色", &ctx.cylinderColor->x), *ctx.cylinderColor, "円柱の色と透明度です")) {
        ctx.cylinder->SetColor(*ctx.cylinderColor);
    }
    if (SceneFields().Track(ImGui::SliderFloat("切り抜きの境目", ctx.cylinderAlphaRef, 0.0f, 1.0f),
            *ctx.cylinderAlphaRef, "この値より薄い所を描かずに切り抜きます")) {
        ctx.cylinder->SetAlphaReference(*ctx.cylinderAlphaRef);
    }
#endif
}

// Skydome を選んでいるときの Inspector 表示
void SceneEditor::SkydomeState::RenderInspector(const EditContext& ctx, SceneEditor&) {
#ifdef USE_IMGUI
    ImGui::TextColored(kSkydomeHeaderColor, "空");
    ImGui::Separator();
    if (ctx.skydome) {
        if (SceneFields().Track(ImGui::ColorEdit4("空の色", &ctx.skyColor->x), *ctx.skyColor, "空に掛ける色です")) {
            ctx.skydome->SetSkyColor(*ctx.skyColor);
        }
        // 天球を Y 軸回転させることで、朝夕方夜のような方角の変化を演出できる
        if (SceneFields().Track(ImGui::SliderFloat("空の回転", ctx.skyRotOffsetY, -gameConstants::kPi, gameConstants::kPi),
                *ctx.skyRotOffsetY, "空をぐるりと回して、見える方角を変えます")) {
            ctx.skydome->SetRotationOffsetY(*ctx.skyRotOffsetY);
        }
        if (ImGui::Button("空の回転を戻す")) {
            *ctx.skyRotOffsetY = 0.0f;
            ctx.skydome->SetRotationOffsetY(0.0f);
        }
    } else {
        ImGui::TextDisabled("空は使用できません");
    }
#endif
}

// Human を選んでいるときの Inspector 表示
void SceneEditor::HumanState::RenderInspector(const EditContext& ctx, SceneEditor&) {
#ifdef USE_IMGUI
    ImGui::TextColored(kHumanHeaderColor, "人型モデル");
    ImGui::Separator();
    if (!ctx.human || !ctx.humanPosition) {
        ImGui::TextDisabled("人型モデルは使用できません");
        return;
    }
    if (SceneFields().Track(ImGui::DragFloat3("位置", &ctx.humanPosition->x, kDragStepFinePosition), *ctx.humanPosition, "置く場所です")) {
        ctx.human->SetPosition(*ctx.humanPosition);
    }
    if (SceneFields().Track(ImGui::DragFloat3("向き", &ctx.humanRotation->x, kDragStepRotation), *ctx.humanRotation, "体の向きです")) {
        ctx.human->SetRotation(*ctx.humanRotation);
    }
    if (SceneFields().Track(ImGui::DragFloat3("大きさ", &ctx.humanScale->x, kDragStepScale, kMinEditableHumanScale, kMaxHumanScale),
            *ctx.humanScale, "横・高さ・奥行きの倍率です")) {
        ctx.human->SetScale(*ctx.humanScale);
    }
    ImGui::Separator();
    if (SceneFields().Track(ImGui::SliderFloat("動きの速さ", ctx.humanAnimSpeed, 0.0f, kMaxAnimSpeed),
            *ctx.humanAnimSpeed, "動きを何倍の速さで再生するかです。0で止まります")) {
        ctx.human->SetAnimSpeed(*ctx.humanAnimSpeed);
    }
    ImGui::Separator();
    if (ImGui::Button("初期値に戻す")) {
        constexpr Vector3 kDefaultHumanPosition = { 5.0f, 0.0f, 0.0f };
        *ctx.humanPosition = kDefaultHumanPosition;
        *ctx.humanRotation = { };
        *ctx.humanScale = { 1.0f, 1.0f, 1.0f };
        *ctx.humanAnimSpeed = 1.0f;
        ctx.human->SetPosition(*ctx.humanPosition);
        ctx.human->SetRotation(*ctx.humanRotation);
        ctx.human->SetScale(*ctx.humanScale);
        ctx.human->SetAnimSpeed(*ctx.humanAnimSpeed);
    }
    ImGui::Separator();
    SceneFields().Track(ImGui::Checkbox("骨を見せる", ctx.showSkeleton), *ctx.showSkeleton, "体を動かす骨の並びを線で重ねて見せます");
#endif
}

// White Particles を選んでいるときの Inspector 表示
void SceneEditor::ParticlesState::RenderInspector(const EditContext& ctx, SceneEditor&) {
#ifdef USE_IMGUI
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1), "白い粒");
    ImGui::Separator();
    if (!ctx.whiteParticlePos) {
        ImGui::TextDisabled("白い粒は使用できません");
        return;
    }
    SceneFields().Track(ImGui::DragFloat3("位置", &ctx.whiteParticlePos->x, kDragStepPosition), *ctx.whiteParticlePos, "粒を出す場所です");
    SceneFields().Track(ImGui::ColorEdit4("色", &ctx.whiteParticleColor->x), *ctx.whiteParticleColor, "粒の色です");
    SceneFields().Track(ImGui::DragFloat("大きさ", ctx.whiteParticleScale, kDragStepScale, kMinEditableSize, kMaxParticleScale),
        *ctx.whiteParticleScale, "粒1つの大きさです");
    SceneFields().Track(ImGui::SliderInt("数", ctx.whiteParticleCount, 1, kMaxParticleCount), *ctx.whiteParticleCount, "出す粒の数です。出し直すと反映されます");
    ImGui::Separator();
    if (ImGui::Button("粒を出し直す", ImVec2(-1, 0))) {
        engine::Services().particles->EmitScatterLoop(
            "white", *ctx.whiteParticlePos, kWhiteParticleSpread,
            static_cast<uint32_t>(*ctx.whiteParticleCount),
            *ctx.whiteParticleColor, kWhiteParticleSpeedMin, kWhiteParticleSpeedMax, *ctx.whiteParticleScale);
    }
#endif
}
// UI Element を選んでいるときの Inspector 表示
void SceneEditor::UIElementState::RenderInspector(const EditContext& ctx, SceneEditor& editor) {
#ifdef USE_IMGUI
    // 選択中の番号が一覧の範囲外なら何もしない
    const int idx = editor.selectionIndex_;
    if (idx < 0 || idx >= static_cast<int>(editor.uiElements_.size())) {
        return;
    }
    // 編集はスプライトの値を直接読み書きする
    UIEntry& entry = editor.uiElements_[idx];
    Sprite* sp = entry.sprite.get();
    // 何を編集中か分かるよう、色付きの見出しを出す
    constexpr ImVec4 kHeadingColor = { 1.0f, 0.5f, 1.0f, 1.0f };
    ImGui::TextColored(kHeadingColor, "画面の部品");
    if (editor.uiDirty_) {
        graphics::editorUI::DrawUnsavedMark();
    }
    ImGui::Separator();
    // 元に戻す・やり直す（ウィンドウにフォーカスがある時はキーでも操作できる）
    ImGui::BeginDisabled(!editor.uiHistory_.CanUndo());
    engine::editor::FlowRow history;
    if (history.Button("元に戻す") || graphics::editorUI::CtrlShortcut(ImGuiKey_Z)) {
        editor.UndoUIEdit();
    }
    ImGui::EndDisabled();

    ImGui::BeginDisabled(!editor.uiHistory_.CanRedo());
    if (history.Button("やり直す") || graphics::editorUI::CtrlShortcut(ImGuiKey_Y)) {
        editor.RedoUIEdit();
    }
    ImGui::EndDisabled();
    // 画面上のドラッグのスナップ
    ImGui::Checkbox("格子に合わせる", &editor.uiSnapEnabled_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderInt("##snapStep", &editor.uiSnapStep_, kMinUISnapStep, kMaxUISnapStep, "%d px");
    graphics::editorUI::TextDisabledWrapped("画面上のスプライトをドラッグで移動、枠の四角で大きさ変更\nCtrl: スナップ反転  Shift+角: 縦横比を保つ");
    ImGui::Separator();
    // 選択が変わったときだけ名前の入力欄を読み直す
    if (editor.uiNameEditIndex_ != idx) {
        editor.uiNameEditIndex_ = idx;
        strncpy_s(editor.uiNameBuffer_, entry.name.c_str(), sizeof(editor.uiNameBuffer_) - 1);
    }
    // 入力した名前はその場で一覧の名前に反映する
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##uiname", editor.uiNameBuffer_, sizeof(editor.uiNameBuffer_))) {
        entry.name = editor.uiNameBuffer_;
        editor.uiDirty_ = true;
    }
    ImGui::Separator();
    // 位置・サイズ・回転・色を編集する（変わった値だけスプライトへ書き戻す）
    Vector2 pos = sp->GetPosition();
    if (editor.TrackUIEdit(ImGui::DragFloat2("位置", &pos.x, kDragStepPixel))) {
        sp->SetPosition(pos);
    }
    // サイズは小さすぎて見えなくならないよう上下限を付ける
    Vector2 sz = sp->GetSize();
    if (editor.TrackUIEdit(ImGui::DragFloat2("大きさ", &sz.x, kDragStepPixel, kMinSpritePixels, kMaxSpritePixels))) {
        sp->SetSize(sz);
    }
    float rot = sp->GetRotation();
    if (editor.TrackUIEdit(ImGui::DragFloat("向き", &rot, kDragStepRotation))) {
        sp->SetRotation(rot);
    }
    Vector4 col = sp->GetColor();
    if (editor.TrackUIEdit(ImGui::ColorEdit4("色", &col.x))) {
        sp->SetColor(col);
    }
    ImGui::Separator();
    // テクスチャファイルのパスを表示し、Browse ボタンでファイル選択ダイアログを開く
    ImGui::TextDisabled("%.40s", entry.texPath.empty() ? "画像なし" : entry.texPath.c_str());
    if (ImGui::Button("画像を選ぶ##uitex")) {
        const std::string selected = OpenFileDialog("PNG Files\0*.png\0All Files\0*.*\0\0", "Resources");
        // キャンセルされたら何もしない
        if (!selected.empty()) {
            // 保存するレイアウトに絶対パスを残さない
            const std::string p = ToProjectRelativePath(selected);
            entry.texPath = p;
            sp->SetTexture(p);
            editor.uiDirty_ = true;
        }
    }
    ImGui::Separator();
    // Delete で一覧から削除、Save/Load で JSON に保存・復元する（削除は保存まで行うので元に戻せず、確認を挟む）
    engine::editor::FlowRow files;
    files.Next(engine::editor::FlowRow::ButtonWidth("削除##ui"));
    if (graphics::editorUI::ConfirmButton("削除##ui", "このUI要素を消して、レイアウトを保存しますか？\n元に戻せません。")) {
        // 消した要素を指したままにならないよう、選択を外して名前の編集状態も戻す
        editor.uiElements_.erase(editor.uiElements_.begin() + idx);
        editor.ChangeState(Selection::kNone);
        editor.uiNameEditIndex_ = kNoEditedUiIndex;
        editor.uiHistory_.Clear();
        editor.uiDrag_ = UIDragState { };
        editor.SaveUILayout();
    } else {
        if (files.Button("保存##inspUI")) {
            editor.SaveUILayout();
        }
        // 未保存の変更があれば読み込み前に確認する
        const char* const loadLabel = "読み込み##inspUI";
        files.Next(engine::editor::FlowRow::ButtonWidth(loadLabel));
        if (editor.uiDirty_ ? graphics::editorUI::ConfirmButton(loadLabel, "未保存の変更を捨てて読み込みますか？", "読み込む") : ImGui::Button(loadLabel)) {
            editor.LoadUILayout(ctx);
        }
    }
    editor.DrawSaveResult();
#endif
}
/** @brief 編集対象のUIを一覧の所有権を公開せずに描画する */
void SceneEditor::DrawUIElements() {
    for (const UIEntry& entry : uiElements_) {
        entry.sprite->Update();
        entry.sprite->Draw();
    }
}
} // namespace engine::game

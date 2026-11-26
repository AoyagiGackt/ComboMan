/**
 * @file ComboFeelDirectorPanel.cpp
 * @brief ComboFeelDirector の調整パネル（F6）を実装するファイル
 * @note 各項目は直後に panelSession_.Item を呼び、元に戻す履歴・説明・右クリックで戻すメニューを付ける
 */
#include "ComboFeelDirector.h"
#include "EditorFlowRow.h"
#include "EditorHub.h"
#include "EditorUI.h"
#include "Input.h"
#include <algorithm>
#ifdef USE_IMGUI
#include <imgui.h>
#endif

namespace engine::game {

/** @brief F6 か F8 のエディタ一覧でパネルを開閉し、開いていれば描画する（Update フェーズから呼ぶ） */
void ComboFeelDirector::UpdateDebugPanel(Input* input) {
#ifdef USE_IMGUI
    // F6 か、F8 のエディタ一覧のボタンでパネルの表示を切り替える
    const bool hubToggle = editor::EditorHub::GetInstance()->Offer("comboFeel", "コンボの手応え", "F6",
        "刃の軌跡、締めの一撃の重さ、斬撃線、覚醒中の派手さを、遊びながら調整します", showPanel_);
    if ((input != nullptr && input->TriggerKey(DIK_F6)) || hubToggle) {
        showPanel_ = !showPanel_;
    }
    if (!showPanel_) {
        return;
    }

    // 各スライダーのドラッグ幅と上下限
    constexpr float kTimeDragStep = 0.005f;
    constexpr float kFineDragStep = 0.02f;
    constexpr float kMaxBlurStrength = 0.6f;
    constexpr float kDistanceDragStep = 0.05f;
    constexpr float kMinSlowScale = 0.05f;
    constexpr float kMinVisibleLifetime = 0.02f;
    constexpr float kMaxZoomDistance = 8.0f;
    constexpr float kMaxSlashHalfLength = 6.0f;
    constexpr float kThicknessDragStep = 0.1f;
    constexpr float kMaxSlashThickness = 40.0f;
    constexpr float kMaxTrailScale = 4.0f;
    constexpr float kMaxDirectionWindow = 0.3f;
    constexpr float kGlowDragStep = 0.01f;
    constexpr float kMaxGlowExtend = 1.5f;
    constexpr float kMaxSlashSpreadDegrees = 60.0f;
    constexpr float kMaxAwakenImpactScale = 3.0f;
    constexpr float kSpreadDragStep = 0.5f;
    constexpr float kMinFinisherSlashScale = 0.5f;
    constexpr float kDragSpeed = kGlowDragStep;
    constexpr int kMaxSubdivisions = 12;
    constexpr int kMinBlurSamples = 4;
    constexpr int kMaxBlurSamples = 32;
    // スライダーは設定値を直接書き換える（次の演出からすぐ反映される）
    ComboFeelSettings& s = settings_;
    using Settings = ComboFeelSettings;
    auto& session = panelSession_;
    // 変更前の値を写しておき、操作した項目を元に戻す履歴に積めるようにする
    session.BeginFrame(s);

    // ウィンドウの×ボタンでも showPanel_ が下りて閉じる
    if (ImGui::Begin("コンボの手応え (F6)", &showPanel_)) {
        // 元に戻す・まとめて戻す・未保存の印
        session.DrawToolbar(s);
        ImGui::Separator();
        // 見比べ中は値を触れないようにする（保存の操作だけは使える）
        session.BeginFields();

        // 発表・比較用に全演出をまとめて切り替える（元に戻せるよう切り替える前を履歴に積む）
        engine::editor::FlowRow topRow;
        if (topRow.Button("全部オン")) {
            session.RecordBeforeChange(s);
            s.trailEnabled = s.impactEnabled = s.radialBlurEnabled = s.zoomEnabled = s.slowEnabled = s.slashEnabled = true;
        }
        // 親の3つを切れば子の演出（ブラー・ズーム・スロー）も出なくなるので、子の設定は残しておく
        if (topRow.Button("全部オフ")) {
            session.RecordBeforeChange(s);
            s.trailEnabled = s.impactEnabled = s.slashEnabled = false;
        }
        // コンボを出さなくても、最後のプレイヤー位置で締めの演出だけを確認できる（見比べ中も使える）
        session.EndFields();
        if (topRow.Button("締め演出を試す")) {
            TriggerFinisherImpact(lastPlayerPos_);
        }
        session.BeginFields();

        // 刃の範囲と手応えを編集する武器の番号（範囲外にならないよう丸める）
        const size_t weaponIndex = static_cast<size_t>(std::clamp(lastWeaponIndex_, 0, Settings::kWeaponTypeCount - 1));

        // 刃の帯の見た目と、武器ごとの刃の範囲
        if (ImGui::CollapsingHeader("1. 刃の軌跡", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("軌跡を描く", &s.trailEnabled);
            session.Item(s, &Settings::trailEnabled, "武器を振った跡に光の帯を残します");
            ImGui::SliderInt("補間分割数 (1=補間なし)", &s.trailSubdivisions, 1, kMaxSubdivisions);
            session.Item(s, &Settings::trailSubdivisions, "帯の曲がりをなめらかにする細かさです。上げるほどきれいですが重くなります");
            ImGui::DragFloat("寿命(秒)", &s.trailLifetime, kTimeDragStep, kMinVisibleLifetime, 1.0f);
            session.Item(s, &Settings::trailLifetime, "帯が消えるまでの秒数です。長いほど尾を引きます");
            ImGui::SliderFloat("刃先の不透明度", &s.trailTipAlpha, 0.0f, 1.0f);
            session.Item(s, &Settings::trailTipAlpha, "刃先側の帯の濃さです。0で見えません");
            ImGui::SliderFloat("根元の不透明度", &s.trailBaseAlpha, 0.0f, 1.0f);
            session.Item(s, &Settings::trailBaseAlpha, "持ち手側の帯の濃さです。0にすると刃先へ向かって濃くなります");
            ImGui::SliderFloat("属性色の混ぜ具合", &s.trailTint, 0.0f, 1.0f);
            session.Item(s, &Settings::trailTint, "帯の色を武器の属性の色へ寄せる割合です。0で白");
            // 補間分割数を上げたときの負荷を見られるよう、現在の頂点数と更新時間を出す
            ImGui::Text("サンプル %d / 頂点 %u / 更新 %.1f us",
                trail_.GetSampleCount(), trail_.GetVertexCount(), lastTrailMicros_);

            ImGui::SeparatorText("刃の範囲（武器モデル空間）");
            ImGui::Combo("武器", &lastWeaponIndex_, Settings::kBladeWeaponNames.data(), static_cast<int>(Settings::kBladeWeaponNames.size()));
            ImGui::DragFloat3("根元", &s.blades[weaponIndex].base.x, kDragSpeed);
            session.Item(s, "帯を出し始める刃の根元の位置です（武器のモデルから見た位置）", [weaponIndex](Settings& x) -> Vector3& {
                return x.blades[weaponIndex].base;
            });
            ImGui::DragFloat3("先端", &s.blades[weaponIndex].tip.x, kDragSpeed);
            session.Item(s, "帯の外側になる刃先の位置です（武器のモデルから見た位置）", [weaponIndex](Settings& x) -> Vector3& {
                return x.blades[weaponIndex].tip;
            });
        }

        // 締めの段で出すブラー・ズーム・スロー
        if (ImGui::CollapsingHeader("2. 締めの重さ", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("締め演出を使う", &s.impactEnabled);
            session.Item(s, &Settings::impactEnabled, "コンボの最後の一撃で、画面のぶれ・寄り・スローを出します");
            ImGui::Checkbox("ラジアルブラー", &s.radialBlurEnabled);
            session.Item(s, &Settings::radialBlurEnabled, "当たった所を中心に、画面を放射状にぶらします");
            ImGui::SliderFloat("ブラー強度", &s.blurPeak, 0.0f, kMaxBlurStrength);
            session.Item(s, &Settings::blurPeak, "ぶれの強さです。上げすぎると画面が見づらくなります");
            ImGui::DragFloat("ブラー時間(秒)", &s.blurSeconds, kTimeDragStep, 0.0f, 1.0f);
            session.Item(s, &Settings::blurSeconds, "ぶれが消えるまでの秒数です");
            ImGui::SliderInt("ブラーサンプル数", &s.blurSamples, kMinBlurSamples, kMaxBlurSamples);
            session.Item(s, &Settings::blurSamples, "ぶれのなめらかさです。上げるほどきれいですが重くなります");
            // カメラを一瞬寄せる量と時間
            ImGui::Checkbox("ズームパンチ", &s.zoomEnabled);
            session.Item(s, &Settings::zoomEnabled, "当たった瞬間にカメラを一瞬寄せます");
            ImGui::DragFloat("寄せる距離", &s.zoomDistance, kDistanceDragStep, 0.0f, kMaxZoomDistance);
            session.Item(s, &Settings::zoomDistance, "カメラを寄せる量です");
            ImGui::DragFloat("ズーム時間(秒)", &s.zoomSeconds, kTimeDragStep, 0.0f, 1.0f);
            session.Item(s, &Settings::zoomSeconds, "寄ってから元に戻るまでの秒数です");
            // ヒットストップが明けた直後に時間をゆっくり進める量と長さ
            ImGui::Checkbox("ヒットストップ明けのスロー", &s.slowEnabled);
            session.Item(s, &Settings::slowEnabled, "止まった直後に、少しの間だけゆっくり動かします");
            ImGui::SliderFloat("スロー倍率", &s.slowScale, kMinSlowScale, 1.0f);
            session.Item(s, &Settings::slowScale, "スロー中の速さです。小さいほどゆっくりです（1で普通の速さ）");
            ImGui::DragFloat("スロー時間(秒)", &s.slowSeconds, kTimeDragStep, 0.0f, 1.0f);
            session.Item(s, &Settings::slowSeconds, "スローを続ける秒数です");
        }

        // 命中点に出す斬撃線の大きさと表示時間
        if (ImGui::CollapsingHeader("3. 振りの向きの斬撃線", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("斬撃線を出す", &s.slashEnabled);
            session.Item(s, &Settings::slashEnabled, "当たった所に、振った向きの線を出します");
            ImGui::DragFloat("長さ(半分)", &s.slashLength, kFineDragStep, 0.0f, kMaxSlashHalfLength);
            session.Item(s, &Settings::slashLength, "線の中心から端までの長さです");
            ImGui::DragFloat("太さ", &s.slashThickness, kThicknessDragStep, 0.0f, kMaxSlashThickness);
            session.Item(s, &Settings::slashThickness, "普段の一撃の線の太さです（画面の点の数）");
            // 締めの段だけ太く長くする
            ImGui::DragFloat("締めの太さ", &s.finisherSlashThickness, kThicknessDragStep, 0.0f, kMaxSlashThickness);
            session.Item(s, &Settings::finisherSlashThickness, "最後の一撃の線の太さです");
            ImGui::DragFloat("締めの長さ倍率", &s.finisherSlashScale, kFineDragStep, kMinFinisherSlashScale, kMaxTrailScale);
            session.Item(s, &Settings::finisherSlashScale, "最後の一撃の線を何倍の長さにするかです");
            ImGui::DragFloat("表示時間(秒)", &s.slashSeconds, kTimeDragStep, kMinVisibleLifetime, 1.0f);
            session.Item(s, &Settings::slashSeconds, "線が消えるまでの秒数です");
            // 斬撃線の向きは、この時間内に刃先が動いた方向から決める
            ImGui::DragFloat("向きを測る時間(秒)", &s.slashDirectionWindow, kTimeDragStep, 0.0f, kMaxDirectionWindow);
            session.Item(s, &Settings::slashDirectionWindow, "当たる直前のこの秒数の刃先の動きから、線の向きを決めます");
        }

        // 覚醒中だけ強める色・帯・斬撃線の本数
        if (ImGui::CollapsingHeader("4. 覚醒", ImGuiTreeNodeFlags_DefaultOpen)) {
            constexpr int kMaxAwakenSlashCount = 7;
            ImGui::Checkbox("覚醒中に派手にする", &s.awakenEnabled);
            session.Item(s, &Settings::awakenEnabled, "覚醒している間だけ、色・帯・線を強めます");
            ImGui::Checkbox("覚醒していなくても見る（確認用）", &previewAwaken_);
            // 色と軌跡・光の帯の強め方
            ImGui::ColorEdit4("覚醒色", &s.awakenColor.x);
            session.Item(s, &Settings::awakenColor, "覚醒中の帯と線の色です");
            ImGui::SliderFloat("軌跡を覚醒色へ寄せる", &s.awakenTint, 0.0f, 1.0f);
            session.Item(s, &Settings::awakenTint, "帯の色を覚醒色へ寄せる割合です");
            ImGui::DragFloat("軌跡の寿命の倍率", &s.awakenTrailLifetimeScale, kFineDragStep, 1.0f, kMaxTrailScale);
            session.Item(s, &Settings::awakenTrailLifetimeScale, "覚醒中は帯を何倍長く残すかです");
            ImGui::DragFloat("光の帯の伸び", &s.awakenGlowExtend, kGlowDragStep, 0.0f, kMaxGlowExtend);
            session.Item(s, &Settings::awakenGlowExtend, "刃の外側に重ねる光の帯を、刃先からどれだけ伸ばすかです");
            ImGui::SliderFloat("光の帯の不透明度", &s.awakenGlowAlpha, 0.0f, 1.0f);
            session.Item(s, &Settings::awakenGlowAlpha, "外側の光の帯の濃さです");
            // 斬撃線を扇状に増やす本数と開き、締め演出の強め方
            ImGui::SliderInt("斬撃線の本数", &s.awakenSlashCount, 1, kMaxAwakenSlashCount);
            session.Item(s, &Settings::awakenSlashCount, "覚醒中に1回当てた時に出す線の本数です");
            ImGui::DragFloat("斬撃線の開き(度)", &s.awakenSlashSpreadDeg, kSpreadDragStep, 0.0f, kMaxSlashSpreadDegrees);
            session.Item(s, &Settings::awakenSlashSpreadDeg, "複数の線を扇のように広げる角度です");
            ImGui::DragFloat("締め演出の倍率", &s.awakenImpactScale, kFineDragStep, 1.0f, kMaxAwakenImpactScale);
            session.Item(s, &Settings::awakenImpactScale, "覚醒中の締めのぶれ・寄り・スローを何倍にするかです");
        }

        // 武器ごとの倍率（選んだ武器だけを編集する。刃の範囲の武器の選択と同じ）
        if (ImGui::CollapsingHeader("5. 武器ごとの手応え", ImGuiTreeNodeFlags_DefaultOpen)) {
            constexpr float kMaxFeelScale = 3.0f;
            constexpr float kMaxLineSpread = 0.6f;
            constexpr float kMaxTrailIntensity = 4.0f;
            constexpr float kMaxTextureRepeat = 8.0f;
            constexpr float kMaxTextureScroll = 12.0f;
            constexpr int kMaxEnemyStopFrames = 60;
            ImGui::Combo("武器##feel", &lastWeaponIndex_, Settings::kBladeWeaponNames.data(), static_cast<int>(Settings::kBladeWeaponNames.size()));
            WeaponFeel& feel = s.weaponFeels[weaponIndex];
            // 選んだ武器の手応えの1項目を取り出す関数を作る（編集中・保存時・初期値の3つに使う）
            const auto feelField = [weaponIndex]<class Field>(Field WeaponFeel::* member) {
                return [weaponIndex, member](Settings& x) -> Field& {
                    return x.weaponFeels[weaponIndex].*member;
                };
            };
            ImGui::DragFloat("軌跡の長さの倍率", &feel.trailLifetimeScale, kFineDragStep, 0.0f, kMaxFeelScale);
            session.Item(s, "この武器だけ帯を何倍長く残すかです", feelField(&WeaponFeel::trailLifetimeScale));
            ImGui::DragFloat("軌跡の濃さの倍率", &feel.trailAlphaScale, kFineDragStep, 0.0f, kMaxFeelScale);
            session.Item(s, "この武器だけ帯を何倍濃くするかです", feelField(&WeaponFeel::trailAlphaScale));
            ImGui::DragFloat("斬撃線の長さの倍率", &feel.slashLengthScale, kFineDragStep, 0.0f, kMaxFeelScale);
            session.Item(s, "この武器だけ線を何倍長くするかです", feelField(&WeaponFeel::slashLengthScale));
            ImGui::DragFloat("斬撃線の太さの倍率", &feel.slashThicknessScale, kFineDragStep, 0.0f, kMaxFeelScale);
            session.Item(s, "この武器だけ線を何倍太くするかです", feelField(&WeaponFeel::slashThicknessScale));
            ImGui::DragFloat("斬撃線の残る長さの倍率", &feel.slashSecondsScale, kFineDragStep, 0.0f, kMaxFeelScale);
            session.Item(s, "この武器だけ線を何倍長く残すかです", feelField(&WeaponFeel::slashSecondsScale));
            ImGui::SliderInt("平行に重ねる線", &feel.slashExtraLines, 0, Settings::kMaxSlashExtraLines);
            session.Item(s, "太い線の横に並べる細い線の本数です。重い武器らしさが出ます", feelField(&WeaponFeel::slashExtraLines));
            ImGui::DragFloat("線どうしの間", &feel.slashLineSpread, kGlowDragStep, 0.0f, kMaxLineSpread);
            session.Item(s, "平行に重ねる線どうしの間隔です", feelField(&WeaponFeel::slashLineSpread));
            ImGui::DragFloat("締めの強さの倍率", &feel.impactScale, kFineDragStep, 0.0f, kMaxFeelScale);
            session.Item(s, "この武器の締めのぶれ・寄りを何倍にするかです", feelField(&WeaponFeel::impactScale));
            ImGui::DragFloat("スローの長さの倍率", &feel.slowSecondsScale, kFineDragStep, 0.0f, kMaxFeelScale);
            session.Item(s, "この武器の締めのスローを何倍長くするかです", feelField(&WeaponFeel::slowSecondsScale));
            // 軌跡の筋の画像は名前の表示だけ（差し替えは combo_feel.json で）。明るさ・繰り返し・流れはその場で変えられる
            ImGui::TextDisabled("筋の画像: %s", feel.trailTexture.empty() ? "なし" : feel.trailTexture.c_str());
            ImGui::DragFloat("軌跡の明るさ", &feel.trailIntensity, kFineDragStep, 0.0f, kMaxTrailIntensity);
            session.Item(s, "帯の明るさです。1より上げると光ってにじみます", feelField(&WeaponFeel::trailIntensity));
            ImGui::DragFloat("筋の繰り返し", &feel.trailTextureRepeat, kFineDragStep, 0.0f, kMaxTextureRepeat);
            session.Item(s, "帯の筋の画像を、帯の長さに何回並べるかです", feelField(&WeaponFeel::trailTextureRepeat));
            ImGui::DragFloat("筋の流れ", &feel.trailTextureScroll, kFineDragStep, -kMaxTextureScroll, kMaxTextureScroll);
            session.Item(s, "帯の筋を流す速さです。マイナスで逆向きに流れます", feelField(&WeaponFeel::trailTextureScroll));
            ImGui::SliderInt("締めで敵だけ止める(フレーム)", &feel.finisherEnemyStopFrames, 0, kMaxEnemyStopFrames);
            session.Item(s, "最後の一撃の後、自分は動けるまま敵だけを止める長さです", feelField(&WeaponFeel::finisherEnemyStopFrames));
        }

        session.EndFields();

        // 調整した値を combo_feel.json へ保存・読み直しする（見比べ中なら新しい値に戻してから）
        ImGui::Separator();
        engine::editor::FlowRow bottomRow;
        if (bottomRow.Button("保存")) {
            session.StopCompare(s);
            SaveSettings();
            session.MarkSaved(s);
        }
        if (bottomRow.Button("読み直し")) {
            session.StopCompare(s);
            session.RecordBeforeChange(s);
            LoadSettings();
        }
        // 保存先は長いので、ボタンの下に折り返して出す
        // 保存先のパスを薄く表示しておく
        graphics::editorUI::TextDisabledWrapped("%s", Settings::kFilePath);
    }
    // Begin が false でも End は必ず呼ぶ（ImGui の決まり）
    ImGui::End();
#else
    // ImGui を使わない構成では引数を使わないので、未使用の警告だけ消す
    (void)input;
#endif
}

} // namespace engine::game

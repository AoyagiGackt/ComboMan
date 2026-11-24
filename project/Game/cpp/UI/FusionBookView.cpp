/**
 * @file FusionBookView.cpp
 * @brief 合体図鑑の画面（FusionBookView）の実装
 */
#include "FusionBookView.h"
#include "Audio.h"
#include "CombatTuning.h"
#include "FontRenderer.h"
#include "Input.h"
#include "RunData.h"
#include "Weapon.h"
#include "WeaponManager.h"
#include <cstdio>
#include <string>

namespace engine::game {
namespace {
// 見出しと集めた数
constexpr Vector2 kTitlePosition = { 100.0f, 50.0f };
constexpr float kTitleScale = 1.8f;
constexpr Vector4 kTitleColor = { 1.0f, 1.0f, 0.6f, 1.0f };
constexpr float kCountGap = 40.0f; // 見出しと集めた数の間
constexpr float kCountScale = 1.3f;
constexpr Vector4 kCountColor = { 0.85f, 0.9f, 1.0f, 1.0f };
// 表（縦が土台、横が素材）
constexpr Vector2 kGridOrigin = { 300.0f, 170.0f }; // 左上のマスの左上
constexpr Vector2 kCellSize = { 110.0f, 50.0f }; // マス1つの大きさ
constexpr float kCellPadding = 4.0f; // マスの背景を少し小さくする幅
constexpr float kAxisScale = 1.1f; // 行と列の武器の名前
constexpr float kRowLabelX = 120.0f; // 行の武器の名前の左端
constexpr float kColumnLabelY = 130.0f; // 列の武器の名前の上端
constexpr float kCornerLabelY = 105.0f; // 表の左上に出す 土台＼素材 の上端
constexpr float kMarkScale = 1.5f; // マスの中の ● と ？
constexpr float kMarkOffsetX = 42.0f; // マスの左端から印までの横の距離
constexpr float kMarkOffsetY = 9.0f; // マスの上端から印までの縦の距離
constexpr float kAxisOffsetY = 12.0f; // 行の名前をマスの上端からずらす量
constexpr Vector4 kCellPanel = { 0.12f, 0.12f, 0.18f, 0.85f };
constexpr Vector4 kCellSelectedPanel = { 0.45f, 0.4f, 0.15f, 0.95f };
constexpr Vector4 kUnknownColor = { 0.45f, 0.45f, 0.5f, 1.0f };
constexpr Vector4 kAxisColor = { 0.9f, 0.9f, 0.9f, 1.0f };
constexpr Vector4 kAxisSelectedColor = { 1.0f, 0.9f, 0.4f, 1.0f };
constexpr float kKnownMarkWhiteMix = 0.25f; // 開いたマスの印を、素材の色から少し白へ寄せる
// 下の詳しい中身と操作の案内
constexpr Vector2 kDetailPosition = { 120.0f, 560.0f };
constexpr float kDetailScale = 1.2f;
constexpr Vector4 kDetailColor = { 1.0f, 1.0f, 1.0f, 1.0f };
constexpr Vector2 kHintPosition = { 120.0f, 660.0f };
constexpr float kHintScale = 1.0f;
constexpr Vector4 kHintColor = { 0.7f, 0.7f, 0.7f, 1.0f };
constexpr size_t kNumberTextCapacity = 64; // 数を文字にする時の長さ

/** @brief 数を書式どおりに文字にする */
std::wstring FormatNumber(const wchar_t* format, float value) {
    wchar_t text[kNumberTextCapacity];
    std::swprintf(text, kNumberTextCapacity, format, value);
    return text;
}
} // namespace

/** @brief 文字の描画先と効果音の再生先を受け取る */
void FusionBookView::Initialize(FontRenderer* fontRenderer, engine::Audio* audio) {
    fontRenderer_ = fontRenderer;
    audio_ = audio;
}

/** @brief 開いた時に呼ぶ（ふつうの合体のページの左上から始める） */
void FusionBookView::Open() {
    bossPage_ = false;
    cursorRow_ = 0;
    cursorColumn_ = 0;
}

/** @brief 表の行と列の数（武器の一覧の数） */
int FusionBookView::WeaponCount() const {
    return static_cast<int>(WeaponManager::GetInstance()->GetList().size());
}

/** @brief カーソル移動の音を鳴らす */
void FusionBookView::PlayCursorSound() const {
    if (audio_ != nullptr) {
        audio_->PlayMenuSelect();
    }
}

/** @brief マスの選択とページの切り替えを受け付ける */
bool FusionBookView::Update(engine::Input* input) {
    if (input->TriggerMenuCancel()) {
        PlayCursorSound();
        return true;
    }
    const int count = WeaponCount();
    if (count <= 0) {
        return false;
    }
    // 上下左右でマスを選ぶ（端から端へ回り込む）
    const int previousRow = cursorRow_;
    const int previousColumn = cursorColumn_;
    if (input->TriggerMenuUp()) {
        cursorRow_ = (cursorRow_ - 1 + count) % count;
    }
    if (input->TriggerMenuDown()) {
        cursorRow_ = (cursorRow_ + 1) % count;
    }
    if (input->TriggerMenuLeft()) {
        cursorColumn_ = (cursorColumn_ - 1 + count) % count;
    }
    if (input->TriggerMenuRight()) {
        cursorColumn_ = (cursorColumn_ + 1) % count;
    }
    if (cursorRow_ != previousRow || cursorColumn_ != previousColumn) {
        PlayCursorSound();
    }
    // 決定でふつうの合体とボスの武器どうしの合体のページを切り替える
    if (input->TriggerMenuConfirm()) {
        bossPage_ = !bossPage_;
        PlayCursorSound();
    }
    return false;
}

/** @brief 選んでいるマスの詳しい中身を作る */
std::wstring FusionBookView::DetailText() const {
    const auto& list = WeaponManager::GetInstance()->GetList();
    if (cursorRow_ >= static_cast<int>(list.size()) || cursorColumn_ >= static_cast<int>(list.size())) {
        return std::wstring();
    }
    const WeaponData& base = list[cursorRow_];
    const WeaponData& partner = list[cursorColumn_];
    const std::wstring recipe = std::wstring(WeaponTypeLabel(base.type)) + L"＋" + WeaponTypeLabel(partner.type);
    const bool known = RunData::GetInstance()->HasFusion(static_cast<int>(base.type), static_cast<int>(partner.type), bossPage_);
    if (!known) {
        return recipe + L"  ？？？（まだ作っていない）";
    }
    // ボスの武器どうしの合体は、主軸の武器のボスが味方になる
    if (bossPage_) {
        return recipe + L"  強さ " + FormatNumber(L"×%.1f", CombatTune().bossFusionDamageMult);
    }
    return recipe + L"  " + WeaponManager::GetDisplayName(partner) + L" の力をまとう  強さ "
        + FormatNumber(L"×%.1f", CombatTune().fusionDamageMult) + L"  耐久 "
        + FormatNumber(L"%.0f", static_cast<float>(CombatTune().fusionDurability));
}

/** @brief 文字とパネルを積む（毎フレーム、FontRenderer::Reset() の後に呼ぶ） */
void FusionBookView::Draw(const engine::Input* input) {
    if (fontRenderer_ == nullptr) {
        return;
    }
    FontRenderer& font = *fontRenderer_;
    const auto& list = WeaponManager::GetInstance()->GetList();
    const int count = WeaponCount();
    const RunData* run = RunData::GetInstance();

    // 見出しと、このページで集めた数
    const std::wstring title = bossPage_ ? L"合体図鑑  ボスの武器どうし" : L"合体図鑑";
    font.DrawStringW(title, kTitlePosition.x, kTitlePosition.y, kTitleScale, kTitleColor);
    const float titleWidth = FontRenderer::MeasureStringW(title, kTitleScale).x;
    const std::wstring countText = std::to_wstring(run->FusionCount(bossPage_)) + L" / " + std::to_wstring(count * count);
    font.DrawStringW(countText, kTitlePosition.x + titleWidth + kCountGap, kTitlePosition.y, kCountScale, kCountColor);

    // 列（素材）と行（土台）の武器の名前。選んでいる行と列は色を変える
    font.DrawStringW(L"土台＼素材", kRowLabelX, kCornerLabelY, kAxisScale, kAxisColor);
    for (int i = 0; i < count; ++i) {
        const std::wstring name = WeaponTypeLabel(list[i].type);
        const float columnX = kGridOrigin.x + kCellSize.x * static_cast<float>(i);
        font.DrawStringW(name, columnX, kColumnLabelY, kAxisScale, i == cursorColumn_ ? kAxisSelectedColor : kAxisColor);
        const float rowY = kGridOrigin.y + kCellSize.y * static_cast<float>(i) + kAxisOffsetY;
        font.DrawStringW(name, kRowLabelX, rowY, kAxisScale, i == cursorRow_ ? kAxisSelectedColor : kAxisColor);
    }

    // マス: 作った組み合わせは素材の色の ●、作っていない組み合わせは ？
    for (int row = 0; row < count; ++row) {
        for (int column = 0; column < count; ++column) {
            const float x = kGridOrigin.x + kCellSize.x * static_cast<float>(column);
            const float y = kGridOrigin.y + kCellSize.y * static_cast<float>(row);
            const bool selected = row == cursorRow_ && column == cursorColumn_;
            font.DrawPanel(x + kCellPadding, y + kCellPadding, kCellSize.x - kCellPadding * 2.0f, kCellSize.y - kCellPadding * 2.0f,
                selected ? kCellSelectedPanel : kCellPanel);
            const bool known = run->HasFusion(static_cast<int>(list[row].type), static_cast<int>(list[column].type), bossPage_);
            const float* c = list[column].effectColor;
            const Vector4 knownColor = { c[0] + (1.0f - c[0]) * kKnownMarkWhiteMix, c[1] + (1.0f - c[1]) * kKnownMarkWhiteMix,
                c[2] + (1.0f - c[2]) * kKnownMarkWhiteMix, 1.0f };
            font.DrawStringW(known ? L"●" : L"？", x + kMarkOffsetX, y + kMarkOffsetY, kMarkScale, known ? knownColor : kUnknownColor);
        }
    }

    // 選んでいるマスの中身と、操作の案内
    font.DrawStringW(DetailText(), kDetailPosition.x, kDetailPosition.y, kDetailScale, kDetailColor);
    const std::wstring hint = bossPage_ ? L"{Move}: マスを選ぶ   決定: ふつうの合体のページへ   戻る: 閉じる"
                                        : L"{Move}: マスを選ぶ   決定: ボスの武器どうしのページへ   戻る: 閉じる";
    font.DrawStringW(input != nullptr ? input->ExpandPrompts(hint) : hint, kHintPosition.x, kHintPosition.y, kHintScale, kHintColor);
}

} // namespace engine::game

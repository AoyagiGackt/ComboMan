/**
 * @file WeaponSlotHud.cpp
 * @brief 武器枠と所持武器アイコンの HUD 表示（WeaponSlotHud）の実装
 */
#include "WeaponSlotHud.h"
#include "Camera.h"
#include "DirectXCommon.h"
#include "FontRenderer.h"
#include "GameConstants.h"
#include "Input.h"
#include "ModelManager.h"
#include "SceneShared.h"
#include "SpriteCommon.h"
#include "UILayout.h"
#include "WeaponManager.h"
#include <algorithm>
#include <cmath>
using namespace engine::graphics;
namespace engine::game {
namespace {
// 枠の配置（画面左下から横に並べる）
constexpr const char* kLayoutName = "hud"; // 全シーン共通のHUDレイアウト（Resources/Config/UI/hud.json）
constexpr Vector2 kSlotsPosition = { 24.0f, 630.0f }; // 一番左の枠の左上
constexpr float kSlotPitch = 66.0f; // 隣の枠までの間隔
constexpr float kGunFrameGap = 24.0f; // 武器枠の列と銃枠の間の余白
constexpr float kIconInset = 6.0f; // 枠の内側に色アイコンを置く余白
constexpr float kGunIconSize = 36.0f;

// 枠とアイコンの色
constexpr Vector4 kSlotFrameColor = { 0.08f, 0.08f, 0.13f, 0.85f };
constexpr Vector4 kGunFrameColor = { 0.08f, 0.08f, 0.1f, 0.85f };
constexpr Vector4 kEmptyIconColor = { 0.2f, 0.2f, 0.2f, 0.35f };
constexpr Vector4 kGunIconColor = { 0.6f, 0.85f, 1.0f, 0.9f };
constexpr Vector4 kHiddenColor = { 0.0f, 0.0f, 0.0f, 0.0f };
constexpr float kFrameAlpha = 0.85f;
constexpr float kUnlockedIconAlpha = 0.95f;

// 選択中の枠の明滅と入手時のフラッシュ
constexpr float kPulseBase = 0.7f;
constexpr float kPulseAmplitude = 0.3f;
constexpr float kPulseSpeed = 6.0f;
constexpr float kFrameBrightnessBase = 0.08f;
constexpr float kFrameActivePulseGain = 0.35f;
constexpr float kFrameFlashGain = 0.5f;
constexpr float kFrameActiveBlueBoost = 0.2f;
constexpr float kFrameIdleBlueBoost = 0.05f;

// 3Dモデルアイコン（カメラ手前のワールド座標に置く）
constexpr float kIconDepth = 6.0f; // カメラからの距離
constexpr float kDepthScale = kIconDepth / -gameConstants::kCameraDistanceZ; // 画面端までの見かけの広さをこの距離に合わせる比率
constexpr float kIconTilt = 0.3f;
constexpr float kIconSwingSpeed = 0.8f;
constexpr float kIconSwingAmplitude = 0.35f;
constexpr float kModelActiveBrightness = 0.9f;
constexpr float kModelActivePulseGain = 0.1f;
constexpr float kModelIdleBrightness = 0.6f;
constexpr float kModelFlashGain = 0.4f;

// 色アイコン（モデルがない武器）
constexpr float kColorActiveBase = 0.7f;
constexpr float kColorActivePulseGain = 0.3f;
constexpr float kColorIdle = 0.5f;
constexpr float kColorFlashGain = 0.5f;
constexpr float kLockedBrightness = 0.2f;
constexpr float kLockedFlashGain = 0.6f;
constexpr float kLockedAlpha = 0.35f;
constexpr float kLockedAlphaFlashGain = 0.3f;
constexpr float kGunIconSpinSpeed = 0.6f;

// 文字
constexpr Vector2 kLockedMarkOffset = { 22.0f, 16.0f };
constexpr float kLockedMarkScale = 1.6f;
constexpr Vector4 kLockedMarkColor = { 0.6f, 0.6f, 0.6f, 0.9f };
constexpr Vector2 kSlotNumberOffset = { 4.0f, 3.0f };
constexpr float kSlotNumberScale = 0.9f;
constexpr Vector4 kSlotNumberColor = { 0.95f, 0.9f, 0.75f, 1.0f };
constexpr float kWeaponNameOffsetY = 60.0f; // 枠の上端から、枠の下に出す武器の名前までの高さ
constexpr float kWeaponNameScale = 0.85f;
constexpr Vector4 kWeaponNameColor = { 1.0f, 0.85f, 0.3f, 1.0f }; // 明るい床の上でも読めるよう、白ではなく黄色にする
constexpr Vector4 kActiveWeaponNameColor = { 0.55f, 0.95f, 1.0f, 1.0f }; // 持っている武器の名前
constexpr Vector4 kWeaponNamePanelColor = { 0.0f, 0.0f, 0.0f, 0.6f }; // 名前の後ろに敷く板
constexpr float kWeaponNamePadding = 3.0f; // 板の文字のまわりの余白
constexpr float kCenterFactor = 0.5f; // 枠の中央にそろえる
constexpr float kGunLabelScale = 0.85f;

// 合体している枠（相棒の色で枠が脈打って光り、枠の下の内側に耐久のバーを出す）
constexpr float kFusionGlowBase = 0.35f; // 枠へ混ぜる相棒の色の基本の強さ
constexpr float kFusionGlowPulse = 0.25f; // 脈打つ幅
constexpr float kFusionGlowSpeed = 4.0f; // 脈打つ速さ
constexpr float kDurabilityBarHeight = 6.0f; // 耐久のバーの太さ
constexpr float kDurabilityBarInset = 3.0f; // 枠の端からバーまでの余白
constexpr Vector4 kDurabilityBackColor = { 0.0f, 0.0f, 0.0f, 0.8f }; // バーの下地
constexpr float kDurabilityLowRatio = 0.3f; // これより少なくなったら赤く点滅させる
constexpr Vector4 kDurabilityLowColor = { 1.0f, 0.2f, 0.15f, 1.0f }; // 残りが少ない時の色
constexpr float kDurabilityBlinkSpeed = 14.0f; // 残りが少ない時の点滅の速さ
constexpr float kDurabilityBlinkMinAlpha = 0.35f; // 点滅で一番薄くなった時の不透明度

/** @brief 枠ごとの切り替えの入力の名前（Input の {Slot1}〜{Slot3}） */
constexpr const wchar_t* kSlotPrompts[] = { L"{Slot1}", L"{Slot2}", L"{Slot3}", L"{Slot4}" };
}

/** @brief 武器スロットのスプライトとアイコンを用意する */
void WeaponSlotHud::Initialize(const HudServices& services) {
    sprites_ = &services.sprites;
    models_ = &services.models;
    graphics_ = &services.graphics;
    weapons_ = &services.weapons;
    InitializeFrames();
    const auto assets = sceneShared::LoadWeaponIconAssets("Resources/Config/weapon_icons.json");
    const auto& list = weapons_->GetList();
    modelIcons_.resize(list.size());
    for (size_t i = 0; i < list.size(); ++i) {
        for (const auto& asset : assets) {
            if (asset.type != list[i].type) {
                continue;
            }
            auto& icon = modelIcons_[i];
            icon.scale = asset.scale;
            icon.baseYaw = asset.baseYaw;
            icon.object = std::make_unique<Object3d>();
            icon.object->Initialize(models_);
            icon.object->SetModel(ModelManager::GetInstance()->GetOrLoad(models_, asset.modelPath, asset.texturePath));
            icon.object->SetEnableLighting(true);
            break;
        }
    }
}
/** @brief 武器スロットの枠のスプライトを用意する */
void WeaponSlotHud::InitializeFrames() {
    auto create = [&](Vector2 position, Vector2 size) {
        auto sprite = std::make_unique<Sprite>();
        sprite->Initialize(sprites_, "Resources/white.png");
        sprite->SetPosition(position);
        sprite->SetSize(size);
        return sprite;
    };
    const float iconSize = kSlotSize - kIconInset * 2.0f;
    for (auto& slot : slots_) {
        slot.frame = create({}, { kSlotSize, kSlotSize });
        slot.frame->SetColor(kSlotFrameColor);
        slot.icon = create({}, { iconSize, iconSize });
        slot.icon->SetColor(kEmptyIconColor);
        // 合体の耐久のバーは最初に作っておき、合体している間だけ描く（ゲーム中に作ったり消したりしない）
        slot.durabilityBack = create({}, { iconSize, kDurabilityBarHeight });
        slot.durabilityBack->SetColor(kDurabilityBackColor);
        slot.durabilityFill = create({}, { iconSize, kDurabilityBarHeight });
    }
    gunFrame_ = create({}, { kSlotSize, kSlotSize });
    gunFrame_->SetColor(kGunFrameColor);
    gunIcon_ = create({}, { kGunIconSize, kGunIconSize });
    gunIcon_->SetAnchorPoint({ 0.5f, 0.5f });
    gunIcon_->SetColor(kGunIconColor);
    ApplyLayout();
}
/** @brief UILayoutから枠の位置・間隔を読み、枠とアイコンを並べ直す（エディタで動かした結果を毎フレーム反映する） */
void WeaponSlotHud::ApplyLayout() {
    // 表示位置・大きさ・色は UI レイアウトから読む（F2 で動かせる）
    UILayout& layout = UILayout::Get(kLayoutName);
    const float size = layout.Float("weapon_slots.size", kSlotSize);
    const float iconInset = layout.Float("weapon_slots.icon_inset", kIconInset);
    const float gunIconSize = layout.Float("weapon_slots.gun_icon_size", kGunIconSize);
    const Vector4 gunFrameColor = layout.Color("weapon_slots.gun_frame_color", kGunFrameColor);
    const Vector4 gunIconColor = layout.Color("weapon_slots.gun_icon_color", kGunIconColor);
    const Vector2 origin = layout.Pos("weapon_slots.pos", kSlotsPosition);
    const float pitch = layout.Float("weapon_slots.pitch", kSlotPitch);
    const float gunGap = layout.Float("weapon_slots.gun_gap", kGunFrameGap);
    const float bossGap = layout.Float("weapon_slots.boss_gap", gunGap);
    // 近接スロットを横に並べ、枠の内側にアイコンを置く
    for (int i = 0; i <= kSlotCount; ++i) {
        auto& slot = slots_[i];
        slot.position = { origin.x + i * pitch + (i == kSlotCount ? bossGap : 0.0f), origin.y };
        slot.frame->SetPosition(slot.position);
        slot.frame->SetSize({ size, size });
        slot.frame->Update();
        slot.icon->SetPosition({ slot.position.x + iconInset, slot.position.y + iconInset });
        const float iconSize = size - iconInset * 2.0f; // 左右（上下）両側の余白を引く
        slot.icon->SetSize({ iconSize, iconSize });
        slot.icon->Update();
    }
    // 近接スロットの右に銃の枠を置き、アイコンは枠の中心に置く
    gunPosition_ = { origin.x + kSlotCount * pitch + gunGap + (weapons_->HasBossWeapon() ? pitch + bossGap : 0.0f), origin.y };
    gunFrame_->SetPosition(gunPosition_);
    gunFrame_->SetSize({ size, size });
    gunFrame_->SetColor(gunFrameColor);
    gunFrame_->Update();
    gunIcon_->SetPosition({ gunPosition_.x + size * 0.5f, gunPosition_.y + size * 0.5f });
    gunIcon_->SetSize({ gunIconSize, gunIconSize });
    gunIcon_->SetColor(gunIconColor);
    gunIcon_->Update();
}
/** @brief 指定したスロットに表示できるモデルのアイコンがあるかを返す */
bool WeaponSlotHud::HasModelIcon(int index) const {
    return index >= 0 && index < static_cast<int>(modelIcons_.size())
        && weapons_->IsUnlocked(index) && modelIcons_[index].object;
}
/** @brief 武器を手に入れた知らせを受けたら枠を光らせる */
void WeaponSlotHud::Notify(HudEvent event) {
    if (event == HudEvent::kWeaponAcquired) {
        flashRemaining_ = kFlashDuration;
    }
}
/** @brief 武器スロットの表示を今の装備に合わせて更新する */
void WeaponSlotHud::Update(const HudFrame& frame) {
    // 枠の明るさの調整値を読み、位置を当て直してから点滅の時間を進める
    UILayout& layout = UILayout::Get(kLayoutName);
    const float frameBrightness = layout.Float("weapon_slots.frame_brightness", kFrameBrightnessBase);
    const float frameActivePulseGain = layout.Float("weapon_slots.frame_active_pulse_gain", kFrameActivePulseGain);
    const float frameFlashGain = layout.Float("weapon_slots.frame_flash_gain", kFrameFlashGain);
    const float frameActiveBlueBoost = layout.Float("weapon_slots.frame_active_blue_boost", kFrameActiveBlueBoost);
    const float frameIdleBlueBoost = layout.Float("weapon_slots.frame_idle_blue_boost", kFrameIdleBlueBoost);
    const float frameAlpha = layout.Float("weapon_slots.frame_alpha", kFrameAlpha);
    ApplyLayout();
    // 武器を切り替えた直後の光は残り時間を減らし、0 で止める
    pulseTime_ += frame.deltaSeconds;
    flashRemaining_ = (std::max)(0.0f, flashRemaining_ - frame.deltaSeconds);
    // 光の強さは残り時間の割合、脈打ちは時間の sin で上下させる
    const float flash = flashRemaining_ / kFlashDuration;
    const float pulse = kPulseBase + kPulseAmplitude * std::sin(pulseTime_ * kPulseSpeed);
    // 武器の一覧と、3Dアイコンを向けるためのカメラ位置
    const auto& list = weapons_->GetList();
    const auto& cam = frame.camera.GetTranslate();
    // 選択中のスロットの枠は脈打たせて青みを足す
    for (int i = 0; i <= kSlotCount; ++i) {
        if (i == WeaponManager::kBossSlot && !weapons_->HasBossWeapon()) {
            continue;
        }
        auto& slot = slots_[i];
        const bool active = i == weapons_->GetSelectedSlot();
        // 明るさは基準値に脈打ち（選択中だけ）と切り替え直後の光を足し、青だけ少し強める
        const float brightness = frameBrightness + (active ? pulse * frameActivePulseGain : 0.0f) + flash * frameFlashGain;
        slot.frame->SetColor({ brightness, brightness, brightness + (active ? frameActiveBlueBoost : frameIdleBlueBoost), frameAlpha });
        UpdateFusionDisplay(slot, i);
        slot.frame->Update();
        // アイコンの見せ方を決めるための、このスロットの状態をまとめる
        const int index = weapons_->GetSlotWeaponIndex(i);
        const SlotIconState state = {
            .weaponIndex = index,
            .active = active,
            .unlocked = index >= 0 && index < static_cast<int>(list.size()) && (i == WeaponManager::kBossSlot || weapons_->IsUnlocked(index)),
            .pulse = pulse,
            .flash = flash,
            .cameraPosition = cam,
        };
        // ボスの枠は色のアイコンを使う
        if (i == WeaponManager::kBossSlot) {
            if (state.unlocked) {
                ShowColorIcon(slot, state);
            } else {
                ShowLockedIcon(slot, state);
            }
            slot.icon->Update();
            continue;
        }
        // 上の行から順に見て、最初に当てはまった見せ方で出す（最後の行は必ず当てはまる）
        /** @brief アイコンの表示方法と適用条件 */
        struct IconStyle {
            bool (*applies)(const WeaponSlotHud&, const SlotIconState&);
            void (WeaponSlotHud::*show)(Slot&, const SlotIconState&);
        };
        static constexpr IconStyle kIconStyles[] = {
            // 3Dモデルのアイコンがある武器はモデルで出す
            {
                [](const WeaponSlotHud& hud, const SlotIconState& s) {
                    return hud.HasModelIcon(s.weaponIndex);
                }, &WeaponSlotHud::ShowModelIcon
            },
            // モデルが無くても解放済みなら武器の色で出す
            {
                [](const WeaponSlotHud&, const SlotIconState& s) {
                    return s.unlocked;
                }, &WeaponSlotHud::ShowColorIcon
            },
            // どれにも当てはまらなければ鍵のアイコン
            {
                [](const WeaponSlotHud&, const SlotIconState&) {
                    return true;
                }, &WeaponSlotHud::ShowLockedIcon
            },
        };
        for (const IconStyle& style : kIconStyles) {
            if (style.applies(*this, state)) {
                (this->*style.show)(slot, state);
                break;
            }
        }
        slot.icon->Update();
    }
    // 銃のアイコンはゆっくり回し続ける
    gunIcon_->SetRotation(pulseTime_ * kGunIconSpinSpeed);
    gunIcon_->Update();
}
/** @brief 合体している枠の枠を相棒の色で光らせ、耐久のバーを残りに合わせて伸ばす */
void WeaponSlotHud::UpdateFusionDisplay(Slot& slot, int slotIndex) {
    const int partner = weapons_->GetFusedWeaponIndex(slotIndex);
    slot.fused = partner >= 0;
    if (!slot.fused) {
        return;
    }
    UILayout& layout = UILayout::Get(kLayoutName);
    const float size = layout.Float("weapon_slots.size", kSlotSize);
    const float barHeight = layout.Float("weapon_slots.durability_bar_height", kDurabilityBarHeight);
    // 枠の色に相棒の色を脈打たせながら混ぜる
    const float* partnerColor = weapons_->GetList()[partner].effectColor;
    const float glow = kFusionGlowBase + kFusionGlowPulse * std::sin(pulseTime_ * kFusionGlowSpeed);
    const Vector4 frameColor = slot.frame->GetColor();
    slot.frame->SetColor({ frameColor.x + (partnerColor[0] - frameColor.x) * glow, frameColor.y + (partnerColor[1] - frameColor.y) * glow,
        frameColor.z + (partnerColor[2] - frameColor.z) * glow, frameColor.w });
    // ボスの枠には耐久のバーを表示しない
    if (slotIndex == WeaponManager::kBossSlot) {
        return;
    }
    // 耐久のバーは枠の下の内側に置き、残りの割合だけ伸ばす（少なくなったら赤く点滅）
    const float ratio = weapons_->GetFusionDurabilityRatio(slotIndex);
    const float width = size - kDurabilityBarInset * 2.0f;
    const Vector2 barPosition = { slot.position.x + kDurabilityBarInset, slot.position.y + size - kDurabilityBarInset - barHeight };
    slot.durabilityBack->SetPosition(barPosition);
    slot.durabilityBack->SetSize({ width, barHeight });
    slot.durabilityBack->Update();
    Vector4 fillColor = { partnerColor[0], partnerColor[1], partnerColor[2], 1.0f };
    if (ratio < kDurabilityLowRatio) {
        fillColor = kDurabilityLowColor;
        const float wave = (1.0f + std::sin(pulseTime_ * kDurabilityBlinkSpeed)) * kCenterFactor;
        fillColor.w = kDurabilityBlinkMinAlpha + (1.0f - kDurabilityBlinkMinAlpha) * wave;
    }
    slot.durabilityFill->SetPosition(barPosition);
    slot.durabilityFill->SetSize({ width * ratio, barHeight });
    slot.durabilityFill->SetColor(fillColor);
    slot.durabilityFill->Update();
}

/** @brief 武器の3Dモデルを枠の位置に浮かべる */
void WeaponSlotHud::ShowModelIcon(Slot& slot, const SlotIconState& state) {
    UILayout& layout = UILayout::Get(kLayoutName);
    const float size = layout.Float("weapon_slots.size", kSlotSize);
    const float modelActiveBrightness = layout.Float("weapon_slots.model_active_brightness", kModelActiveBrightness);
    const float modelActivePulseGain = layout.Float("weapon_slots.model_active_pulse_gain", kModelActivePulseGain);
    const float modelIdleBrightness = layout.Float("weapon_slots.model_idle_brightness", kModelIdleBrightness);
    const float modelFlashGain = layout.Float("weapon_slots.model_flash_gain", kModelFlashGain);
    slot.icon->SetColor(kHiddenColor);
    auto& icon = modelIcons_[state.weaponIndex];
    // 枠の中心の画面座標を、カメラ手前の奥行きのワールド座標へ写す
    const Vector3& cam = state.cameraPosition;
    const float sx = slot.position.x + size * 0.5f;
    const float sy = slot.position.y + size * 0.5f;
    icon.object->SetPosition({
        cam.x + (sx - gameConstants::kScreenCenterX) / gameConstants::kScreenCenterX * gameConstants::kCameraHalfW * kDepthScale,
        cam.y - (sy - gameConstants::kScreenCenterY) / gameConstants::kScreenCenterY * gameConstants::kCameraHalfH * kDepthScale,
        cam.z + kIconDepth });
    icon.object->SetRotation({ kIconTilt, icon.baseYaw + std::sin(pulseTime_ * kIconSwingSpeed) * kIconSwingAmplitude, 0.0f });
    const float scale = icon.scale * kDepthScale;
    icon.object->SetScale({ scale, scale, scale });
    const float b = (state.active ? (modelActiveBrightness + state.pulse * modelActivePulseGain) : modelIdleBrightness) + state.flash * modelFlashGain;
    icon.object->SetColor({ b, b, b, 1.0f });
    icon.object->Update();
}

/** @brief 武器の色で塗ったアイコンを出す */
void WeaponSlotHud::ShowColorIcon(Slot& slot, const SlotIconState& state) {
    UILayout& layout = UILayout::Get(kLayoutName);
    const float colorActiveBrightness = layout.Float("weapon_slots.color_active_brightness", kColorActiveBase);
    const float colorActivePulseGain = layout.Float("weapon_slots.color_active_pulse_gain", kColorActivePulseGain);
    const float colorIdleBrightness = layout.Float("weapon_slots.color_idle_brightness", kColorIdle);
    const float colorFlashGain = layout.Float("weapon_slots.color_flash_gain", kColorFlashGain);
    const float unlockedIconAlpha = layout.Float("weapon_slots.unlocked_icon_alpha", kUnlockedIconAlpha);
    const float m = (state.active ? (colorActiveBrightness + state.pulse * colorActivePulseGain) : colorIdleBrightness) + state.flash * colorFlashGain;
    const float* c = weapons_->GetList()[state.weaponIndex].styleColor;
    slot.icon->SetColor({ c[0] * m, c[1] * m, c[2] * m, unlockedIconAlpha });
}

/** @brief まだ手に入れていない枠を暗く出す */
void WeaponSlotHud::ShowLockedIcon(Slot& slot, const SlotIconState& state) {
    UILayout& layout = UILayout::Get(kLayoutName);
    const float lockedBrightness = layout.Float("weapon_slots.locked_brightness", kLockedBrightness);
    const float lockedFlashGain = layout.Float("weapon_slots.locked_flash_gain", kLockedFlashGain);
    const float lockedAlpha = layout.Float("weapon_slots.locked_alpha", kLockedAlpha);
    const float lockedAlphaFlashGain = layout.Float("weapon_slots.locked_alpha_flash_gain", kLockedAlphaFlashGain);
    const float b = lockedBrightness + state.flash * lockedFlashGain;
    slot.icon->SetColor({ b, b, b, lockedAlpha + state.flash * lockedAlphaFlashGain });
}

/** @brief 武器スロットの文字を描画予約する */
void WeaponSlotHud::QueueText(FontRenderer& font) const {
    UILayout& layout = UILayout::Get(kLayoutName);
    const Vector2 lockedMarkOffset = layout.Vec2("weapon_slots.locked_mark_offset", kLockedMarkOffset);
    const float lockedMarkScale = layout.Float("weapon_slots.locked_mark_scale", kLockedMarkScale);
    const Vector4 lockedMarkColor = layout.Color("weapon_slots.locked_mark_color", kLockedMarkColor);
    const Vector2 numberOffset = layout.Vec2("weapon_slots.number_offset", kSlotNumberOffset);
    const float numberScale = layout.Float("weapon_slots.number_scale", kSlotNumberScale);
    const Vector4 numberColor = layout.Color("weapon_slots.number_color", kSlotNumberColor);
    const float size = layout.Float("weapon_slots.size", kSlotSize);
    const float nameOffsetY = layout.Float("weapon_slots.name_offset_y", kWeaponNameOffsetY);
    const float nameScale = layout.Float("weapon_slots.name_scale", kWeaponNameScale);
    const Vector4 nameColor = layout.Color("weapon_slots.name_color", kWeaponNameColor);
    const Vector4 activeNameColor = layout.Color("weapon_slots.active_name_color", kActiveWeaponNameColor);
    const float gunLabelScale = layout.Float("weapon_slots.gun_label_scale", kGunLabelScale);
    const Vector4 gunIconColor = layout.Color("weapon_slots.gun_icon_color", kGunIconColor);
    const Input* input = Input::GetCurrent();
    const auto& list = weapons_->GetList();
    // 枠の下に文字を中央そろえで出す
    const Vector4 namePanelColor = layout.Color("weapon_slots.name_panel_color", kWeaponNamePanelColor);
    const auto drawBelow = [&](const std::wstring& text, const Vector2& framePosition, const Vector4& color) {
        const Vector2 textSize = FontRenderer::MeasureStringW(text, nameScale);
        const float x = framePosition.x + (size - textSize.x) * kCenterFactor;
        const float y = framePosition.y + nameOffsetY;
        // 後ろに暗い板を敷いて、明るい床の上でも読めるようにする
        font.DrawPanel(x - kWeaponNamePadding, y - kWeaponNamePadding, textSize.x + kWeaponNamePadding * 2.0f,
            textSize.y + kWeaponNamePadding * 2.0f, namePanelColor);
        font.DrawStringW(text, x, y, nameScale, color);
    };
    for (int i = 0; i <= kSlotCount; ++i) {
        if (i == WeaponManager::kBossSlot && !weapons_->HasBossWeapon()) {
            continue;
        }
        const int index = weapons_->GetSlotWeaponIndex(i);
        const bool unlocked = index >= 0 && index < static_cast<int>(list.size()) && (i == WeaponManager::kBossSlot || weapons_->IsUnlocked(index));
        if (!unlocked) {
            font.DrawStringW(L"?", slots_[i].position.x + lockedMarkOffset.x, slots_[i].position.y + lockedMarkOffset.y,
                lockedMarkScale, lockedMarkColor);
        } else {
            // 何の武器かが分かるように、枠の下に種類の名前を出す（持っている枠は明るく）
            const bool active = i == weapons_->GetSelectedSlot();
            // 合体している枠は 剣＋槌 のように相棒の名前も並べる
            const int partner = weapons_->GetFusedWeaponIndex(i);
            std::wstring label = std::wstring(i == WeaponManager::kBossSlot ? L"★" : L"") + WeaponTypeLabel(list[index].type);
            if (partner >= 0) {
                label += std::wstring(L"＋") + WeaponTypeLabel(list[partner].type);
            }
            drawBelow(label, slots_[i].position, active ? activeNameColor : nameColor);
        }
        // 切り替えの入力は、最後に使った機器の表記で枠の左上に出す
        if (input != nullptr) {
            font.DrawStringW(input->ExpandPrompts(kSlotPrompts[i]), slots_[i].position.x + numberOffset.x,
                slots_[i].position.y + numberOffset.y, numberScale, numberColor);
        }
    }
    drawBelow(L"銃", gunPosition_, gunIconColor);
}
/** @brief 武器スロットを描画する */
void WeaponSlotHud::Draw() {
    for (int i = 0; i <= kSlotCount; ++i) {
        if (i != WeaponManager::kBossSlot || weapons_->HasBossWeapon()) {
            slots_[i].frame->Draw();
        }
    }
    gunFrame_->Draw();
    models_->CommonDrawSettings();
    Object3d::RebindCommonLighting(graphics_->GetCommandList());
    for (int i = 0; i < kSlotCount; ++i) {
        const int index = weapons_->GetSlotWeaponIndex(i);
        if (HasModelIcon(index)) {
            modelIcons_[index].object->Draw();
        }
    }
    sprites_->CommonDrawSettings();
    for (int i = 0; i <= kSlotCount; ++i) {
        if (i == WeaponManager::kBossSlot && !weapons_->HasBossWeapon()) {
            continue;
        }
        auto& slot = slots_[i];
        slot.icon->Draw();
        if (slot.fused && i != WeaponManager::kBossSlot) {
            slot.durabilityBack->Draw();
            slot.durabilityFill->Draw();
        }
    }
    gunIcon_->Draw();
}
}

/**
 * @file ScreenBanner.cpp
 * @brief 画面の中央上に名前を一瞬出す表示を実装するファイル
 */
#include "ScreenBanner.h"
#include "FontRenderer.h"
#include "GameConstants.h"
#include "UILayout.h"
#include <algorithm>

namespace engine::game {

namespace {
constexpr float kBannerSeconds = 1.6f; // 出している秒数
constexpr float kBannerPopSeconds = 0.15f; // 出てから元の大きさへ戻るまでの秒数
constexpr float kBannerFadeSeconds = 0.45f; // 最後に薄く消えていく秒数
constexpr float kBannerPopScale = 0.6f; // 出た瞬間に大きさへ足す割合
constexpr Vector2 kBannerPosition = { gameConstants::kScreenCenterX, 200.0f }; // 文字の中心の位置
constexpr float kBannerScale = 2.4f; // 文字の大きさ
constexpr float kBannerWhiteMix = 0.45f; // 色へ白を混ぜる割合（暗い色でも読めるように）
constexpr Vector4 kBannerShadowColor = { 0.0f, 0.0f, 0.0f, 0.85f }; // 影の色
constexpr float kBannerShadowOffset = 3.0f; // 影をずらす量
constexpr float kCenterFactor = 0.5f; // 文字を中央にそろえる時に、大きさへ掛ける割合
} // namespace

/** @brief 知らせを出す（色には白を混ぜて読みやすくする） */
void ScreenBanner::Show(const std::wstring& text, const Vector4& color, float offsetY) {
    text_ = text;
    offsetY_ = offsetY;
    color_ = {
        color.x + (1.0f - color.x) * kBannerWhiteMix,
        color.y + (1.0f - color.y) * kBannerWhiteMix,
        color.z + (1.0f - color.z) * kBannerWhiteMix,
        1.0f,
    };
    timer_ = kBannerSeconds;
}

/** @brief 出しておく残り時間を進める */
void ScreenBanner::Update(float dt) {
    timer_ = (std::max)(timer_ - dt, 0.0f);
}

/** @brief 出ている間だけ、弾けて薄く消える文字を影つきで描く */
void ScreenBanner::Draw(FontRenderer& font, const char* layoutName) const {
    if (timer_ <= 0.0f || text_.empty()) {
        return;
    }
    UILayout& layout = UILayout::Get(layoutName);
    const Vector2 center = layout.Pos("acquire_banner.pos", kBannerPosition);
    const float baseScale = layout.Float("acquire_banner.scale", kBannerScale);
    // 出た直後ほど大きく、時間とともに元の大きさへ戻す
    const float elapsed = kBannerSeconds - timer_;
    const float pop = 1.0f - std::clamp(elapsed / kBannerPopSeconds, 0.0f, 1.0f);
    const float scale = baseScale * (1.0f + kBannerPopScale * pop);
    // 最後の時間で薄く消す
    const float alpha = std::clamp(timer_ / kBannerFadeSeconds, 0.0f, 1.0f);
    Vector4 color = color_;
    color.w *= alpha;
    Vector4 shadow = kBannerShadowColor;
    shadow.w *= alpha;
    // 文字の中心が置き場所に来るようにずらし、影を先に描く
    const Vector2 size = FontRenderer::MeasureStringW(text_, scale);
    const float x = center.x - size.x * kCenterFactor;
    const float y = center.y + offsetY_ - size.y * kCenterFactor;
    font.DrawStringW(text_, x + kBannerShadowOffset, y + kBannerShadowOffset, scale, shadow);
    font.DrawStringW(text_, x, y, scale, color);
}

} // namespace engine::game

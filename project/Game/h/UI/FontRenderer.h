/**
 * @file FontRenderer.h
 * @brief ゲームの下敷き画像を使う文字描画を、エンジンの文字描画から作るファイル
 */
#pragma once
#include "BitmapFontRenderer.h"

namespace engine::game {
using graphics::Sprite;
using graphics::SpriteCommon;

/** @brief ゲームの下敷き画像を使って文字列を描画する */
class FontRenderer : public graphics::FontRenderer {
public:
    /** @brief ゲームの文字描画に使う画像とスプライトを準備する */
    void Initialize(SpriteCommon* spriteCommon) {
        graphics::FontRenderer::Initialize(spriteCommon, "Resources/white.png");
    }
};

} // namespace engine::game

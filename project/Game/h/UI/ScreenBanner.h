/**
 * @file ScreenBanner.h
 * @brief 画面の中央上に名前を一瞬出す表示（武器を手に入れた・合体した時など）を定義するファイル
 */
#pragma once
#include "Vector4.h"
#include <string>

namespace engine::game {

class FontRenderer;

/**
 * @brief 画面の中央上に出す短い知らせ
 * @note 出てすぐ大きく弾け、最後に薄く消える。位置と大きさは UI 配置（F2）の acquire_banner で動かせる。
 */
class ScreenBanner {
public:
    /**
     * @brief 知らせを出す（前の知らせは消える）
     * @param text  出す文字
     * @param color 演出の色（読みやすいように白を混ぜて出す）
     * @param offsetY 配置の基準から下へずらす距離
     */
    void Show(const std::wstring& text, const Vector4& color, float offsetY = 0.0f);
    /**
     * @brief 出しておく残り時間を進める
     * @param dt 経過秒数
     */
    void Update(float dt);
    /**
     * @brief 出している間だけ描く
     * @param font 文字を描く先
     * @param layoutName UI 配置の名前
     */
    void Draw(FontRenderer& font, const char* layoutName) const;
    /** @brief 出している知らせを消す（ステージを読み直した時） */
    void Reset() {
        timer_ = 0.0f;
    }

private:
    std::wstring text_; ///< 出している文字
    float offsetY_ = 0.0f;
    Vector4 color_ = { 1.0f, 1.0f, 1.0f, 1.0f }; ///< 文字の色（白を混ぜた後）
    float timer_ = 0.0f; ///< 出している残り秒数（0なら出さない）
};

} // namespace engine::game

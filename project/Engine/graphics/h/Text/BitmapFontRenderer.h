/**
 * @file BitmapFontRenderer.h
 * @brief ASCII・日本語（ひらがな/カタカナ/漢字）のビットマップフォント描画を行うファイル
 */
#pragma once
#include "Sprite.h"
#include "SpriteCommon.h"
#include <string>
#include <vector>
namespace engine::graphics {

/**
 * @brief コード生成したビットマップフォントアトラスを使い、文字列をスプライトで描画するクラス
 * @note ASCII用とJP用の2種類のアトラスを内部で生成し、DrawString/DrawStringWで描き分ける
 */
class FontRenderer {
public:
    // ASCII アトラス定数
    static constexpr int kCharW = 8;
    static constexpr int kCharH = 16;
    static constexpr int kCols = 16;
    static constexpr int kRows = 6;
    static constexpr int kAtlasW = kCharW * kCols; // 128（マスの大きさで数えた幅。絵はこの kAtlasPixelScale 倍）
    static constexpr int kAtlasH = kCharH * kRows; // 96
    static constexpr int kCharBase = 32;
    // 太字の文字を一度に描画できる上限
    static constexpr int kMaxChars = 4096;

    // JP アトラス定数（ひらがな・カタカナ・漢字）
    static constexpr int kJpCharW = 16;
    static constexpr int kJpCharH = 16;
    /** @brief 文字の絵をマスの大きさの何倍の細かさで作るか（並べる間隔は kCharW などのまま。引き伸ばしても文字がにじまないように） */
    static constexpr int kAtlasPixelScale = 2;
    static constexpr int kJpCols = 64; // JIS第二水準まで焼くため横長にして縦の行数を抑える

    // 通常の太さの文字を一度に描画できる上限
    static constexpr int kMaxRegularChars = 2048;

    /**
     * @brief ASCII/JP各2種(Bold/Regular)のアトラスを構築（未生成なら）し、描画用スプライトプールを確保する
     * @param spriteCommon スプライト描画の共通設定
     * @param panelTexture 下敷きに使うテクスチャのパス
     */
    void Initialize(SpriteCommon* spriteCommon, const std::string& panelTexture);

    // ASCII 文字列描画（bold=falseでRegularウェイトを使う）
    void DrawString(const std::string& text, float x, float y,
        float scale = 1.0f,
        const Vector4& color = { 1.0f, 1.0f, 1.0f, 1.0f },
        bool bold = true);

    // 日本語（ひらがな・カタカナ・漢字）+ ASCII 混在文字列描画（bold=falseでRegularウェイトを使う）
    void DrawStringW(const std::wstring& text, float x, float y,
        float scale = 1.0f,
        const Vector4& color = { 1.0f, 1.0f, 1.0f, 1.0f },
        bool bold = true);

    /** @brief DrawStringW と同じ文字を、画面の左右中央に揃えて描画予約する */
    void DrawStringCenteredW(const std::wstring& text, float y, float scale, const Vector4& color);

    /**
     * @brief 文字の下に敷く単色の板を積む（Draw()で文字より先に描く）
     * @note 明るい背景の上でも案内文が読めるようにするための下敷き
     */
    void DrawPanel(float x, float y, float width, float height, const Vector4& color);

    /**
     * @brief DrawStringWで描いた時の大きさを返す（改行は行数として数える）
     * @return x=最も長い行の幅、y=全行の高さ
     */
    static Vector2 MeasureStringW(const std::wstring& text, float scale);

    /** @brief 積んだ描画コマンドとスプライト使用数をクリアする（毎フレーム、DrawString系を呼ぶ前に呼ぶ） */
    void Reset();
    /** @brief Reset()以降に積んだASCII/JP文字列コマンドをすべてスプライトとして描画する */
    void Draw();

private:
    /** @brief 必要な文字数に応じてスプライトを追加し、既存のものは再利用する */
    Sprite& AcquireGlyphSprite(std::vector<Sprite>& pool, int& index, const char* atlas);
    /** @brief アトラスのグリッドの glyphIndex 番目のマス（cellW×cellH、cols 列）を、画面の (x, y) へ描く */
    void DrawGlyph(std::vector<Sprite>& pool, int& poolIndex, const char* atlas, int glyphIndex,
        int cols, int cellW, int cellH, float x, float y, float scale, const Vector4& color);
    /** @brief Draw()まで遅延させるASCII文字列描画コマンド1件（文字列・座標・スケール・色・太字指定） */
    struct DrawCmd {
        std::string text;
        float x, y, scale;
        Vector4 color;
        bool bold = true;
    };
    /** @brief Draw()まで遅延させる日本語文字列描画コマンド1件（文字列・座標・スケール・色・太字指定） */
    struct DrawCmdW {
        std::wstring text;
        float x, y, scale;
        Vector4 color;
        bool bold = true;
    };

    /** @brief GDIでCourier NewのASCII文字をDIBSectionへ描画し、ASCIIアトラステクスチャとして登録する（登録済みなら何もしない） */
    void BuildAtlas(bool bold);
    /** @brief ひらがな・カタカナ・追加漢字グリフをGDIで描画し、JPアトラステクスチャとして登録する（登録済みなら何もしない） */
    void BuildJpAtlas(bool bold);
    /**
     * @brief 文字がJPアトラス内のどのグリフ番号に対応するかを返す
     * @param c 判定する文字（ひらがな/カタカナ/全角記号/漢字/追加記号）
     * @return グリフ番号（アトラス内の並び順）対応外の文字なら-1
     */
    int GetJpGlyphIdx(wchar_t c) const;

    std::string panelTexture_; ///< 下敷きに使うテクスチャ
    SpriteCommon* spriteCommon_ = nullptr;
    std::vector<Sprite> sprites_; // 半角の太字
    std::vector<Sprite> spritesRegular_; // 半角の通常文字
    std::vector<DrawCmd> cmds_;
    int spriteIdx_ = 0;
    int spriteRegularIdx_ = 0;

    std::vector<Sprite> jpSprites_; // 日本語の太字
    std::vector<Sprite> jpSpritesRegular_; // 日本語の通常文字
    std::vector<DrawCmdW> cmdsW_;
    int jpSpriteIdx_ = 0;
    int jpSpriteRegularIdx_ = 0;
    int jpAtlasRows_ = 0;
    int jpAtlasRowsRegular_ = 0;

    /** @brief Draw()まで遅延させる下敷きの板1枚 */
    struct PanelCmd {
        float x, y, width, height;
        Vector4 color;
    };
    std::vector<PanelCmd> panelCmds_;
    std::vector<Sprite> panelSprites_;
    int panelSpriteIdx_ = 0;
};

} // namespace engine::graphics

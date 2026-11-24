/**
 * @file FusionBookView.h
 * @brief 合体図鑑（このランで作った武器の合体の組み合わせを、土台×素材の表で見せる。ポーズから開く）
 */
#pragma once
#include <string>

namespace engine {
class Input;
class Audio;
} // namespace engine

namespace engine::game {
class FontRenderer;

/**
 * @brief 合体図鑑の画面
 * @note 縦が土台、横が素材の武器の表。作った組み合わせのマスが開き、作っていないマスは ？ のまま。
 *       上下左右でマスを選ぶと下の欄に詳しい中身が出る。決定でふつうの合体とボスの武器どうしの合体のページを切り替える。
 *       記録は RunData（ランごと。NEW GAME で消える）
 */
class FusionBookView {
public:
    /**
     * @brief 文字の描画先と効果音の再生先を受け取る
     * @param fontRenderer 文字とパネルを積む先（描画の実行は呼び出し側で行う）
     * @param audio カーソル移動の効果音（nullptr なら鳴らさない）
     */
    void Initialize(FontRenderer* fontRenderer, engine::Audio* audio);

    /** @brief 開いた時に呼ぶ（ふつうの合体のページの左上から始める） */
    void Open();

    /**
     * @brief マスの選択とページの切り替えを受け付ける
     * @return 戻る操作がされたら true
     */
    bool Update(engine::Input* input);

    /** @brief 文字とパネルを積む（毎フレーム、FontRenderer::Reset() の後に呼ぶ） */
    void Draw(const engine::Input* input);

private:
    /** @brief 表の行と列の数（武器の一覧の数） */
    int WeaponCount() const;
    /** @brief 選んでいるマスの詳しい中身を作る */
    std::wstring DetailText() const;
    /** @brief カーソル移動の音を鳴らす */
    void PlayCursorSound() const;

    FontRenderer* fontRenderer_ = nullptr;
    engine::Audio* audio_ = nullptr;
    bool bossPage_ = false; ///< ボスの武器どうしの合体のページを見ているか
    int cursorRow_ = 0; ///< 選んでいる行（土台の武器の、武器の一覧での番号）
    int cursorColumn_ = 0; ///< 選んでいる列（素材の武器の、武器の一覧での番号）
};

} // namespace engine::game

/**
 * @file ActionFeelTuning.h
 * @brief アクションの手応えの調整値（敵どうしの衝突・壁と床の跳ね返り・部屋の最後の1体・ジャスト回避の後の遅さ・パッドの振動）
 */
#pragma once

namespace engine::game {

/** @brief アクションの手応えの調整値（Resources/Config/action_feel.json。初期値は JSON に項目が無い時の既定値） */
struct ActionFeelTuning {
    // ── 吹き飛んだ敵がほかの敵に当たる ──
    bool collideEnabled = true; ///< 使うか
    float collideMinSpeed = 0.12f; ///< これより速く横へ飛んでいる敵だけがほかの敵に当たる（1フレームの距離）
    int collideDamage = 2; ///< 当てられた敵へのダメージ
    float collideKnockY = 0.18f; ///< 当てられた敵を打ち上げる初速
    int collideMaxChain = 2; ///< 当てられた敵がさらに別の敵へ当たれる連鎖の深さ（0 なら連鎖しない）
    float collideFlyerSpeedKeep = 0.7f; ///< 当たった後に、飛んでいた敵の速さをどれだけ残すか
    float collideStyleGain = 0.04f; ///< 当てるたびにスタイルを足す量
    // ── 壁と床で跳ね返る ──
    bool bounceEnabled = true; ///< 使うか
    float wallBounceMinSpeed = 0.12f; ///< これより速く壁にぶつかった時だけ跳ね返る（1フレームの距離）
    float wallBounceFactor = 0.55f; ///< 跳ね返った後の横の速さの割合
    float wallBounceLift = 0.2f; ///< 跳ね返った時に浮かせる初速
    int wallBounceCount = 1; ///< 1回の吹き飛びで壁に跳ね返れる回数
    int wallBounceDamage = 1; ///< 壁に跳ね返った敵へのダメージ
    float floorBounceMinFallSpeed = 0.4f; ///< これより速く落ちてきた時だけ床で跳ねる（1フレームの距離）
    float floorBounceFactor = 0.45f; ///< 跳ねた後の上向きの速さの割合
    int floorBounceCount = 1; ///< 1回の吹き飛びで床で跳ねられる回数
    float bounceStyleGain = 0.03f; ///< 跳ね返るたびにスタイルを足す量
    // ── 部屋の最後の1体 ──
    bool roomClearEnabled = true; ///< 使うか
    float roomClearSlowSeconds = 0.7f; ///< スローの長さ（実時間の秒）
    float roomClearSlowScale = 0.3f; ///< スローの間にゲームを進める割合
    // ── ジャスト回避の後に周りの敵だけ遅くなる ──
    bool dodgeSlowEnabled = true; ///< 使うか
    float dodgeSlowSeconds = 1.5f; ///< 遅くなる長さ（秒）
    float dodgeSlowScale = 0.35f; ///< 遅くなっている間に敵を進める割合
    float dodgeSlowRadius = 12.0f; ///< プレイヤーからこの距離の中の敵だけを遅くする
    // ── パッドの振動（hit_effects.json の impacts に振動の値が無い時は、画面の揺れから決める） ──
    float rumblePerShake = 2.5f; ///< 画面の揺れの強さに掛けて振動の強さにする倍率
    float rumbleHighRatio = 0.6f; ///< 高い揺れのモーターを、低い揺れのモーターの何割にするか
    float rumbleMinSeconds = 0.08f; ///< 振動の最短の長さ（秒）
    float rumbleScale = 1.0f; ///< 振動全体の強さの倍率
};

/** @brief アクションの手応えの調整値を返す（初回だけファイルを読み、以後は覚えた値を返す） */
const ActionFeelTuning& ActionFeel();

} // namespace engine::game

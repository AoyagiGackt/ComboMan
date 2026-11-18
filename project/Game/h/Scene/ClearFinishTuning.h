/**
 * @file ClearFinishTuning.h
 * @brief クリアの決め演出の調整値と、決めの一太刀・決めの一発の並び（Resources/Config/clear_finish.json）
 */
#pragma once
#include "Vector2.h"
#include "Vector4.h"
#include <string>
#include <vector>

namespace engine::game::clearFinishTuning {

inline constexpr Vector2 kScreenCenterUV = { 0.5f, 0.5f }; ///< 画面の中心（画面に対する0〜1の位置）

/** @brief 決めの一太刀1本ぶんの設定 */
struct FinishSlash {
    float angleDegrees = -18.0f; ///< 斬線の傾き（画面の中心を通る）
    float delay = 0.0f; ///< 静止が明けてから振り始めるまでの秒数
};

/** @brief クリアの決め演出の調整値（clear_finish.json。初期値はJSONに項目が無い時の既定値） */
struct FinishTuning {
    // 決めの間の暗幕
    Vector4 dimColor = { 0.02f, 0.03f, 0.1f, 0.55f };
    float dimSeconds = 0.15f; ///< 暗幕が濃くなり切るまでの長さ
    // 細かい斬線（決めの一太刀の前に画面を斬り刻む）
    int cutCount = 16; ///< 本数（最大。スプライトはこの数だけ作る）
    int cutCountJitter = 4; ///< 本数を毎回この数までの範囲で減らす
    float cutInterval = 0.045f; ///< 次の斬線までの間隔
    float cutIntervalJitter = 0.02f; ///< 間隔のばらつき（±）
    float cutSpreadU = 0.32f; ///< 斬線が通る点の横の散らばり（画面の中心から±、画面の幅に対する割合）
    float cutSpreadV = 0.3f; ///< 縦の散らばり
    float cutSeconds = 0.05f; ///< 斬線が画面の端から端まで走る長さ
    float cutThickness = 4.0f; ///< 走った瞬間の芯の太さ
    float cutRemainThickness = 1.5f; ///< 走り終えた後に残る傷の太さ
    std::string cutFlash; ///< 斬線が走った瞬間の画面フラッシュ（空なら出さない）
    std::string cutImpact = "clear_cut"; ///< 斬線が走った瞬間のカメラ揺れ
    std::string cutSound; ///< 斬線が走った瞬間に鳴らす音（空なら鳴らさない）
    // 決めの一太刀
    float bladeSilenceSeconds = 0.45f; ///< 細かい斬線を入れ終えてから決めの一太刀までの静止
    float slashSeconds = 0.12f; ///< 斬線が画面の端から端まで走る長さ
    float slashThickness = 10.0f; ///< 振った瞬間の芯の太さ
    float slashRemainThickness = 3.0f; ///< 振り終えた後に残る傷の太さ
    float slashThinSeconds = 0.2f; ///< 走り終えてから傷の太さに細るまでの長さ（細かい斬線も同じ）
    float slashGlowScale = 4.0f; ///< 光の帯の太さ（芯の何倍か）
    Vector4 slashCoreColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    Vector4 slashGlowColor = { 0.7f, 0.9f, 1.0f, 0.35f };
    float slashHoldSeconds = 0.35f; ///< 決めの一太刀を振り終えてから割れ始めるまでの長さ
    float slashAngleJitter = 30.0f; ///< 決めの一太刀の傾きのばらつき（±度）
    float slashOffsetJitter = 0.1f; ///< 決めの一太刀が通る点を画面の中心からずらす量（±、画面に対する割合）
    std::string slashFlash = "clear_slash"; ///< 振った瞬間の画面フラッシュ。hit_effects.json の flashes の項目の名前
    std::string slashImpact = "clear_slash"; ///< 振った瞬間のカメラ揺れ。impacts の項目の名前
    std::string slashSound; ///< 振った瞬間に鳴らす音（空なら鳴らさない）
    // 弾の雨（決めの一発の前に画面へ浴びせる）
    int sprayCount = 14; ///< 弾の数（最大。スプライトはこの数だけ作る）
    int sprayCountJitter = 4; ///< 弾の数を毎回この数までの範囲で減らす
    float sprayInterval = 0.05f; ///< 次の弾までの間隔
    float sprayIntervalJitter = 0.02f; ///< 間隔のばらつき（±）
    float sprayMargin = 0.1f; ///< 弾痕を画面の端からどれだけ離すか（画面に対する割合）
    float sprayHoleScale = 0.6f; ///< 弾痕の大きさ（決めの一発の何倍か）
    std::string sprayFlash; ///< 弾が入った瞬間の画面フラッシュ（空なら出さない）
    std::string sprayImpact = "clear_spray"; ///< 弾が入った瞬間のカメラ揺れ
    std::string spraySound; ///< 弾が入った瞬間に鳴らす音（空なら鳴らさない）
    // 決めの一発
    float gunSilenceSeconds = 0.4f; ///< 弾を浴びせ終えてから決めの一発までの静止
    float shotInterval = 0.12f; ///< 決めの一発が複数ある時の間隔
    float holeSize = 220.0f; ///< 弾痕の大きさ（画面の座標。ひびの広がりも含めた画像の大きさ）
    float holePopScale = 1.15f; ///< 弾が入った瞬間の大きさ（元の何倍か）
    float holePopSeconds = 0.08f; ///< 元の大きさに縮むまでの長さ
    Vector4 holeColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    float sparkScale = 0.6f; ///< 着弾の火花の大きさ（弾痕の何倍か）
    float sparkSeconds = 0.1f; ///< 着弾の火花が消えるまでの長さ
    Vector4 sparkColor = { 1.0f, 0.9f, 0.6f, 1.0f };
    float shotHoldSeconds = 0.35f; ///< 決めの一発が入ってから割れ始めるまでの長さ
    float shotJitter = 0.18f; ///< 決めの一発の位置のばらつき（±、画面に対する割合）
    std::string shotFlash = "clear_shot"; ///< 弾が入った瞬間の画面フラッシュ。flashes の項目の名前
    std::string shotImpact = "clear_shot"; ///< 弾が入った瞬間のカメラ揺れ。impacts の項目の名前
    std::string shotSound; ///< 弾が入った瞬間に鳴らす音（空なら鳴らさない）
    // 割れる
    float shatterSeconds = 3.2f; ///< ひびが広がって破片が散り、集まり直して結果画面になるまでの長さ
    float crackWidth = 0.005f; ///< ひびの線の太さ（画面の幅に対する割合）
    float shardSpeed = 0.9f; ///< 破片が落ちる速さ
    float soundVolume = 1.0f;
};

/** @brief 調整値と、決めの一太刀・決めの一発の並びをまとめた読み込み結果 */
struct FinishConfig {
    FinishTuning tuning;
    std::vector<FinishSlash> slashes = { FinishSlash {} };
    std::vector<Vector2> shots = { kScreenCenterUV };
};

/** @brief 読み込み済みの設定を取得する（初回に読む。変更は次回起動時に反映） */
const FinishConfig& Config();

/** @brief 読み込み済みの調整値を取得する */
const FinishTuning& Tune();

} // namespace engine::game::clearFinishTuning

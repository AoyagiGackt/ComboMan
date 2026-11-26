/**
 * @file GameAudio.h
 * @brief このゲームの曲の表のキーと、エンジンの音へ渡すゲーム固有の設定を定義するファイル
 */
#pragma once
#include "Audio.h"

namespace engine::game {

/** @brief Resources/Config/bgm.json に書くシーンごとの BGM のキー */
namespace bgmKey {
inline constexpr const char* kTitle = "title"; ///< タイトル
inline constexpr const char* kStageSelect = "stage_select"; ///< ステージセレクト
inline constexpr const char* kStage = "stage"; ///< ステージ本編
inline constexpr const char* kTraining = "training"; ///< トレーニング
inline constexpr const char* kBattleTest = "battle_test"; ///< バトルテスト
inline constexpr const char* kGameOver = "game_over"; ///< やられて GAME OVER が出た時（1回だけ流す）
inline constexpr const char* kGameClear = "game_clear"; ///< クリアして結果画面になった時（1回だけ流す）
} // namespace bgmKey

/** @brief このゲームの曲の表とメニュー音をエンジンの音に設定する（起動時に1回呼ぶ） */
void ConfigureGameAudio(engine::Audio& audio);

} // namespace engine::game

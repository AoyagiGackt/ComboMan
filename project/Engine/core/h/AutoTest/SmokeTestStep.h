/**
 * @file SmokeTestStep.h
 * @brief 繰り返しを展開した自動テストの手順
 */
#pragma once
#include "json.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace engine {

/** @brief 手順1つ（手順の表の steps の1要素を、繰り返しを展開して平らにしたもの） */
struct SmokeTestStep {
    std::string action; ///< 手順の名前
    std::string scene; ///< waitScene・changeScene などで使う画面の名前
    std::vector<uint8_t> keys; ///< press・hold・random などで押すキー
    std::vector<uint8_t> holdKeys; ///< random の間ずっと押し続けるキー（右へ歩きながら暴れる、など）
    std::string label; ///< 報告に出す説明
    int frames = 0; ///< 長さ（秒をフレームに直したもの）
    int timeoutFrames = 0; ///< これを過ぎたら止まったとみなす
    int intervalFrames = 0; ///< pressUntilScene でキーを押し直す間隔、random で押し直す間隔
    float chance = 0.0f; ///< random でそれぞれのキーを押す確率
    bool expectProgress = false; ///< この手順の間は先へ進めるはず（進めなければゲーム側が知らせる）
    bool sameStage = false; ///< 入ったステージの中だけの手順（自分でクリアした・やられてリトライした等で画面が変わったら飛ばす）
    std::vector<std::string> moveNames; ///< でたらめ操作で使う技の名前（空なら手順の表の monkeyMoves の全部）
    nlohmann::json raw; ///< 手順の元の要素
};

} // namespace engine

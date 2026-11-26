/**
 * @file GraphValue.h
 * @brief グラフのノードの値と、データピンの型を定義するファイル
 */
#pragma once
#include <string>
#include <variant>

namespace engine::scripting {

/** @brief ノードのパラメータ／変数が取りうる値 */
using GraphValue = std::variant<float, bool, std::string>;

/** @brief データピンの型（エディタでのピン色分け・接続可否の判定に使う） */
/// Any は何にでも接続できるワイルドカード（SetVariableのvalue等、実行時に型が決まるもの）
enum class GraphValueType { kFloat,
    kBool,
    kString,
    kAny };

} // namespace engine::scripting

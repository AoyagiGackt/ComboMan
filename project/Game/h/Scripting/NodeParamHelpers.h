/**
 * @file NodeParamHelpers.h
 * @brief ノードの実行関数と登録が共通で使う、パラメータの読み取りと初期値
 */
#pragma once
#include "GraphRuntime.h"
#include "GraphTypes.h"
#include "Vector3.h"
#include <string>

namespace engine::game::nodeParams {

/** @brief 文字列の初期値を作る（GraphValue は bool へも変換できるため、明示的に std::string へ包む） */
inline GraphValue Text(const char* text) {
    return std::string(text);
}

// ノード追加時の初期値で共通して使う値
inline constexpr float kOne = 1.0f;
inline const GraphValue kDefaultEnemyTarget = Text("enemy");

// パラメータ解決はGraphRuntime::ResolveParamに委譲する（データ配線があれば供給元ノードの出力を優先）

/** @brief ノードの target パラメータ（操作する敵や配置物の名前）を読む */
inline std::string ResolveTarget(GraphRuntime& rt, const GraphNode& node) {
    return AsString(rt.ResolveParam(node, "target", std::string { }));
}

/** @brief ノードの x/y/z パラメータを座標として読む（GraphValue に Vector3 型が無いため3つに分けて受け取る） */
inline Vector3 ResolvePosition(GraphRuntime& rt, const GraphNode& node) {
    return {
        AsFloat(rt.ResolveParam(node, "x", GraphValue { 0.0f })),
        AsFloat(rt.ResolveParam(node, "y", GraphValue { 0.0f })),
        AsFloat(rt.ResolveParam(node, "z", GraphValue { 0.0f })),
    };
}

} // namespace engine::game::nodeParams

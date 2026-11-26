/**
 * @file NodeRegistry.h
 * @brief ノード種別名（"If"等）と、その実行ロジックを結び付けるレジストリ
 * @note 新しいノード種別を増やしたいときは、ここに1つ登録すればグラフJSON側は
 * コードを書かずに（type名を書くだけで）使えるようになる
 */
#pragma once
#include "GraphTypes.h"
#include "NodeTypeRegistry.h"
#include <functional>

namespace engine::game {
class GraphRuntime;
using scripting::NodeParamSpec;
using scripting::NodeResult;
using scripting::NodeTypeSpec;

/**
 * @brief 1ノード分の実行ロジック
 * @param rt        実行中のグラフインスタンス（変数の読み書き・Wait開始に使う）
 * @param node      実行するノード本体（params・next系フィールド）
 * @param outNextId [out] 次に進むノードID（空文字列ならそこでHalt）
 * @note Suspend を返す場合も outNextId は待機完了後に進むノードを必ず設定すること
 * （Waitノードの実装を参照）
 */
using NodeExecuteFn = std::function<NodeResult(GraphRuntime& rt, const GraphNode& node, std::string& outNextId)>;

/** @brief ゲーム用の実行関数を登録する共有のノード表 */
class NodeRegistry : public scripting::NodeRegistry<NodeExecuteFn> {
public:
    /** @brief ゲームで共有する登録表を取得する */
    static NodeRegistry* GetInstance();

private:
    /** @brief ゲームのノードの定義と実行関数を登録する（GetInstance が初めて呼ばれた時に1回だけ呼ぶ） */
    void RegisterBuiltins();
    NodeRegistry() = default;
    NodeRegistry(const NodeRegistry&) = delete;
    NodeRegistry& operator=(const NodeRegistry&) = delete;
    /** @brief 進行と演出を扱うノードを登録する */
    void RegisterProgressNodes();
    /** @brief 勝ち負けの条件を作るノードを登録する（NodeRegistryRuleNodes.cpp） */
    void RegisterRuleNodes();
};

} // namespace engine::game

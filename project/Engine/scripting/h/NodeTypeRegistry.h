/**
 * @file NodeTypeRegistry.h
 * @brief ノードの種類の名前から、実行関数とピンの情報を引く登録表を定義するファイル
 */
#pragma once
#include "GraphValue.h"
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace engine::scripting {

/** @brief ノード実行の結果次に進めるか（Continue）、フレームをまたいで待つか（Suspend） */
enum class NodeResult { kContinue,
    kSuspend };

/** @brief パラメータ1個ぶんのピン情報（データ配線の型チェック・エディタでのピン色分けに使う） */
struct NodeParamSpec {
    std::string key;
    GraphValueType type = GraphValueType::kAny;
    std::optional<GraphValue> defaultValue; // ノード追加時の初期値（省略時は型ごとの既定値）

    /** @brief エディタでノードを追加した時に入れる初期値を返す */
    GraphValue InitialValue() const;
};

/** @brief ノード種別1個ぶんのピン情報 */
struct NodeTypeSpec {
    std::vector<NodeParamSpec> params; // 入力データピン（パラメータ）の並び
    bool hasOutput = false; // 出力データピンを持つか
    GraphValueType outputType = GraphValueType::kAny; // 出力データピンの型
    std::string description; // 何ができるかの一言説明（ノード追加メニューのツールチップに使う）
    std::string category; // ノード追加メニューでのグループ分け（例  "よく使う", "敵"）空ならその他扱い
    bool branches = false; // trueなら実行出力が真/偽の2本（nextTrue/nextFalse）、falseなら1本（next）
    std::string openPathParam; // 空でなければ、このパラメータが指すグラフをエディタで開くボタンを出す
};

/** @brief 名前からノードの実行関数とピン定義を引く登録表 */
template <class NodeExecuteFn>
class NodeRegistry {
public:
    /** @brief 新しいノード種別を登録する（同名で上書きも可） */
    void Register(const std::string& type, NodeExecuteFn fn) {
        executors_[type] = std::move(fn);
    }

    /** @brief ノード種別のピン情報を登録する（データ配線を使わないノードは省略可） */
    void RegisterSpec(const std::string& type, NodeTypeSpec spec) {
        specs_[type] = std::move(spec);
    }

    /** @brief 型名から実行ロジックを引く登録が無ければnullptr */
    const NodeExecuteFn* Find(const std::string& type) const {
        auto it = executors_.find(type);
        return (it != executors_.end()) ? &it->second : nullptr;
    }

    /** @brief 型名からピン情報を引く登録が無ければnullptr（エディタはAny扱いにする） */
    const NodeTypeSpec* FindSpec(const std::string& type) const {
        auto it = specs_.find(type);
        return (it != specs_.end()) ? &it->second : nullptr;
    }
    /** @brief 実行出力が真/偽の2本に分かれるノード種別か（未登録の種別はfalse） */
    bool IsBranchType(const std::string& type) const {
        const NodeTypeSpec* spec = FindSpec(type);
        return spec && spec->branches;
    }

    /** @brief 登録済みの型名一覧を返す（GraphEditorのノード追加メニュー用） */
    std::vector<std::string> GetRegisteredTypes() const {
        std::vector<std::string> types;
        types.reserve(executors_.size());
        for (const auto& [type, fn] : executors_) {
            types.push_back(type);
        }
        return types;
    }

private:
    std::map<std::string, NodeExecuteFn> executors_;
    std::map<std::string, NodeTypeSpec> specs_;
};

} // namespace engine::scripting

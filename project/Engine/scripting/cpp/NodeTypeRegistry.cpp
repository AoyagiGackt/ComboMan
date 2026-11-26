/**
 * @file NodeTypeRegistry.cpp
 * @brief ノードのピンの初期値を返す処理を実装するファイル
 */
#include "NodeTypeRegistry.h"
#include <cstddef>

namespace engine::scripting {
namespace {
// GraphValueTypeの並び順（Float/Bool/String/Any）に対応する、初期値を省略した時の既定値
const GraphValue kTypeDefaults[] = { 0.0f, false, std::string { }, 0.0f };
} // namespace

/** @brief エディタでノードを追加した時に入れる初期値を返す */
GraphValue NodeParamSpec::InitialValue() const {
    return defaultValue ? *defaultValue : kTypeDefaults[static_cast<std::size_t>(type)];
}
} // namespace engine::scripting

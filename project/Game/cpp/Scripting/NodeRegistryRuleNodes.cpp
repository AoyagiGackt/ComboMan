/**
 * @file NodeRegistryRuleNodes.cpp
 * @brief 勝ち負けの条件を作るノード（ゴールの範囲に入るまで待つ・点数が届くまで待つ・ゲームオーバーにする）を登録するファイル
 */
#include "NodeRegistry.h"
#include "NodeParamHelpers.h"
#include "GameFlags.h"
#include "GameModeLibrary.h"
#include "GraphRuntime.h"
#include "Player.h"
#include "PlayerBridge.h"
#include "RunData.h"
#include <cmath>

namespace engine::game {
using namespace nodeParams;
namespace {
constexpr float kDefaultGoalSize = 4.0f; ///< ゴールの範囲の既定の幅と高さ
constexpr float kDefaultTargetScore = 1000.0f; ///< 目標の点数の既定値
constexpr float kHalfRatio = 0.5f; ///< 幅から半分の幅を出す割合
constexpr float kRetryEveryFrame = 0.0f; ///< 条件を待つノードは毎フレーム自分を実行し直す

/** @brief 条件がそろうまで、次のフレームにこのノードをもう一度実行させる */
NodeResult WaitUntilNextFrame(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    rt.BeginWait(kRetryEveryFrame);
    outNextId = node.id;
    return NodeResult::kSuspend;
}

/** @brief WaitPlayerInArea ノード: プレイヤーが中心(x,y)・幅width・高さheightの範囲に入るまで待つ */
NodeResult ExecWaitPlayerInArea(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    const Vector3 center = ResolvePosition(rt, node);
    const float halfWidth = AsFloat(rt.ResolveParam(node, "width", GraphValue { kDefaultGoalSize })) * kHalfRatio;
    const float halfHeight = AsFloat(rt.ResolveParam(node, "height", GraphValue { kDefaultGoalSize })) * kHalfRatio;
    const Player* player = PlayerBridge::GetInstance()->Get();
    const bool inside = player != nullptr && std::abs(player->GetPosition().x - center.x) <= halfWidth
        && std::abs(player->GetPosition().y - center.y) <= halfHeight;
    if (!inside) {
        return WaitUntilNextFrame(rt, node, outNextId);
    }
    outNextId = node.next;
    return NodeResult::kContinue;
}

/** @brief WaitForScore ノード: このランの点数が value 以上になるまで待つ */
NodeResult ExecWaitForScore(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    const float target = AsFloat(rt.ResolveParam(node, "value", GraphValue { kDefaultTargetScore }));
    if (static_cast<float>(RunData::GetInstance()->GetScore()) < target) {
        return WaitUntilNextFrame(rt, node, outNextId);
    }
    outNextId = node.next;
    return NodeResult::kContinue;
}

/** @brief RequestGameOver ノード: ゲームオーバーを頼むフラグを立てる（本編がやられ演出を始める） */
NodeResult ExecRequestGameOver(GraphRuntime&, const GraphNode& node, std::string& outNextId) {
    GameFlags::GetInstance()->SetFlag(GameModeLibrary::kGameOverRequestFlag, true);
    outNextId = node.next;
    return NodeResult::kContinue;
}
} // namespace

/** @brief 勝ち負けの条件を作るノードを登録する */
void NodeRegistry::RegisterRuleNodes() {
    using VT = GraphValueType;
    Register("WaitPlayerInArea", ExecWaitPlayerInArea);
    RegisterSpec("WaitPlayerInArea", { { { "x", VT::kFloat }, { "y", VT::kFloat }, { "width", VT::kFloat, kDefaultGoalSize },
        { "height", VT::kFloat, kDefaultGoalSize } }, false, VT::kAny,
        "プレイヤーが中心(x,y)・幅width・高さheightの範囲に入るまで待つ（ゴールに着いたらクリア、などに使う）", "進行" });
    Register("WaitForScore", ExecWaitForScore);
    RegisterSpec("WaitForScore", { { { "value", VT::kFloat, kDefaultTargetScore } }, false, VT::kAny,
        "このランの点数がvalue以上になるまで待つ（目標の点数を取ったらクリア、などに使う）", "進行" });
    Register("RequestGameOver", ExecRequestGameOver);
    RegisterSpec("RequestGameOver", { { }, false, VT::kAny,
        "ゲームオーバーにする（時間切れで負け、などに使う。やられ演出の後に本編と同じ流れになる）", "進行" });
}

} // namespace engine::game

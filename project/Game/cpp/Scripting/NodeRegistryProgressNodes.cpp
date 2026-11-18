/**
 * @file NodeRegistryProgressNodes.cpp
 * @brief 組み込みノードのうち、進行の流れ・演出の追加・プレイヤーの状態を扱うノードの実行関数と登録
 */
#include "NodeRegistry.h"
#include "NodeParamHelpers.h"
#include "AudioBridge.h"
#include "EnemyEntity.h"
#include "EnemyRegistry.h"
#include "EventBus.h"
#include "GameFlags.h"
#include "GameRules.h"
#include "GraphRuntime.h"
#include "Logger.h"
#include "ParticleManager.h"
#include "Player.h"
#include "PlayerBridge.h"
#include "RunData.h"
#include "SceneEffectBridge.h"
#include "SceneManager.h"
#include "ScreenFlash.h"
#include "StageEditor.h"
#include "TimeManager.h"
#include "Weapon.h"
#include "WeaponManager.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <utility>
using namespace engine::game;
using namespace engine;
namespace engine::game {
using namespace nodeParams;
namespace {
// ── 追加ノードの初期値 ──
constexpr float kDefaultMessageSeconds = 2.5f;
constexpr float kDefaultSlowScale = 0.3f;
constexpr float kDefaultSlowSeconds = 1.0f;
constexpr float kDefaultFocusSeconds = 2.0f;
constexpr float kDefaultRepeatCount = 3.0f;
constexpr float kDefaultAwakenAmount = 0.2f;
constexpr float kDefaultAreaSize = 4.0f;
constexpr float kHalf = 0.5f;
constexpr float kNoWait = 0.0f; // 条件を待つノードは毎フレーム自分を実行し直す
constexpr const char* kRepeatCounterSuffix = "#repeat"; // 繰り返しの回数を覚えておく内部の出力名（配線からは見えない）

/** @brief 条件がそろうまで、次のフレームにこのノードをもう一度実行させる */
NodeResult RetryNextFrame(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    rt.BeginWait(kNoWait);
    outNextId = node.id;
    return NodeResult::kSuspend;
}

/** @brief ShowMessage ノード: 画面中央に文字を seconds 秒出す */
NodeResult ExecShowMessage(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    const std::string text = AsString(rt.ResolveParam(node, "text", std::string { }));
    const float seconds = AsFloat(rt.ResolveParam(node, "seconds", GraphValue { kDefaultMessageSeconds }));
    SceneEffectBridge::GetInstance()->RequestMessage(text, seconds);
    outNextId = node.next;
    return NodeResult::kContinue;
}

/** @brief SlowMotion ノード: seconds 秒のあいだゲームを scale 倍の速さにする */
NodeResult ExecSlowMotion(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    const float scale = AsFloat(rt.ResolveParam(node, "scale", GraphValue { kDefaultSlowScale }));
    const float seconds = AsFloat(rt.ResolveParam(node, "seconds", GraphValue { kDefaultSlowSeconds }));
    TimeManager::GetInstance()->RequestSlowMotion(seconds, scale);
    outNextId = node.next;
    return NodeResult::kContinue;
}

/** @brief FocusCamera ノード: seconds 秒のあいだカメラを (x,y) へ向ける */
NodeResult ExecFocusCamera(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    const Vector3 position = ResolvePosition(rt, node);
    const float seconds = AsFloat(rt.ResolveParam(node, "seconds", GraphValue { kDefaultFocusSeconds }));
    SceneEffectBridge::GetInstance()->RequestCameraFocus(position.x, position.y, seconds);
    outNextId = node.next;
    return NodeResult::kContinue;
}

/**
 * @brief Repeat ノード: interval 秒ごとに nextTrue（繰り返す処理）へ進み、count 回終えたら nextFalse へ進む
 * @note 繰り返す処理の最後をこのノードへつなぎ直すと次の回になる。count が0以下ならずっと繰り返す。
 * 出力は今が何回目か（1から）。1回ごとに必ず1フレーム以上待つので、間隔0でも固まらない
 */
NodeResult ExecRepeat(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    const int count = static_cast<int>(AsFloat(rt.ResolveParam(node, "count", GraphValue { kDefaultRepeatCount })));
    const float interval = AsFloat(rt.ResolveParam(node, "interval", GraphValue { kOne }));
    const std::string counterKey = node.id + kRepeatCounterSuffix;
    const GraphValue* counter = rt.GetNodeOutput(counterKey);
    const int next = (counter ? static_cast<int>(AsFloat(*counter)) : 0) + 1;
    // 決めた回数を終えたら数え直せるように消してから、終わった後の処理へ進む
    if (count > 0 && next > count) {
        rt.ClearNodeOutput(counterKey);
        outNextId = node.nextFalse;
        return NodeResult::kContinue;
    }
    rt.SetNodeOutput(counterKey, GraphValue { static_cast<float>(next) });
    rt.SetNodeOutput(node.id, GraphValue { static_cast<float>(next) });
    rt.BeginWait((std::max)(interval, kNoWait));
    outNextId = node.nextTrue;
    return NodeResult::kSuspend;
}

/** @brief WaitAllEnemiesDefeated ノード: 出ている敵が全員倒されるまで待つ */
NodeResult ExecWaitAllEnemiesDefeated(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    if (EnemyRegistry::GetInstance()->AnyAlive()) {
        return RetryNextFrame(rt, node, outNextId);
    }
    outNextId = node.next;
    return NodeResult::kContinue;
}

/** @brief WaitEnemyDefeated ノード: target の敵が倒されるまで待つ（見つからない敵は倒された扱い） */
NodeResult ExecWaitEnemyDefeated(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    const EnemyEntity* enemy = EnemyRegistry::GetInstance()->Find(ResolveTarget(rt, node));
    if (enemy != nullptr && !enemy->IsDefeated()) {
        return RetryNextFrame(rt, node, outNextId);
    }
    outNextId = node.next;
    return NodeResult::kContinue;
}

/** @brief WaitForFlag ノード: flag が value になるまで待つ（トリガーやほかのグラフが立てるのを待つ） */
NodeResult ExecWaitForFlag(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    const std::string flag = AsString(rt.ResolveParam(node, "flag", std::string { }));
    const bool value = AsBool(rt.ResolveParam(node, "value", GraphValue { true }));
    if (flag.empty()) {
        Logger::LogError("[Graph] WaitForFlag node '" + node.id + "' is missing 'flag' param");
    } else if (GameFlags::GetInstance()->GetFlag(flag) != value) {
        return RetryNextFrame(rt, node, outNextId);
    }
    outNextId = node.next;
    return NodeResult::kContinue;
}

/** @brief GiveWeapon ノード: weapon の武器を解放して装備させる */
NodeResult ExecGiveWeapon(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    const std::string weapon = AsString(rt.ResolveParam(node, "weapon", std::string { }));
    const WeaponType type = ParseWeaponTypeName(weapon);
    // 知らない名前は Sword に読み替わるので、打ち間違いに気づけるよう知らせる
    if (type == WeaponType::kSword && weapon != "Sword") {
        Logger::LogError("[Graph] GiveWeapon node '" + node.id + "' has unknown weapon '" + weapon + "' (Sword is used)");
    }
    // 枠が埋まっている時は、敵から奪った時と同じく選んでいる枠の武器と入れ替える（交換待ちを残さない）
    WeaponManager* weaponManager = WeaponManager::GetInstance();
    if (weaponManager->Acquire(type) == WeaponManager::AcquireResult::kNeedsReplacement) {
        weaponManager->ReplacePendingWeapon(weaponManager->GetSelectedSlot());
    }
    outNextId = node.next;
    return NodeResult::kContinue;
}

/** @brief ChargeAwaken ノード: プレイヤーの覚醒ゲージを amount 増やす */
NodeResult ExecChargeAwaken(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    const float amount = AsFloat(rt.ResolveParam(node, "amount", GraphValue { kDefaultAwakenAmount }));
    if (Player* player = PlayerBridge::GetInstance()->Get()) {
        player->ChargeAwakenGauge(amount);
    }
    outNextId = node.next;
    return NodeResult::kContinue;
}

/** @brief LockPlayerControl ノード: プレイヤーの操作を止める／戻す（演出の間に勝手に動かれないように） */
NodeResult ExecLockPlayerControl(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    const bool locked = AsBool(rt.ResolveParam(node, "locked", GraphValue { true }));
    SceneEffectBridge::GetInstance()->RequestControlLock(locked);
    outNextId = node.next;
    return NodeResult::kContinue;
}

/** @brief IfPlayerInArea ノード: プレイヤーが中心(x,y)・幅width・高さheightの範囲にいれば nextTrue、いなければ nextFalse */
NodeResult ExecIfPlayerInArea(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    const Vector3 center = ResolvePosition(rt, node);
    const float halfWidth = AsFloat(rt.ResolveParam(node, "width", GraphValue { kDefaultAreaSize })) * kHalf;
    const float halfHeight = AsFloat(rt.ResolveParam(node, "height", GraphValue { kDefaultAreaSize })) * kHalf;
    bool inside = false;
    if (const Player* player = PlayerBridge::GetInstance()->Get()) {
        const Vector3 position = player->GetPosition();
        inside = std::abs(position.x - center.x) <= halfWidth && std::abs(position.y - center.y) <= halfHeight;
    }
    rt.SetNodeOutput(node.id, GraphValue { inside });
    outNextId = inside ? node.nextTrue : node.nextFalse;
    return NodeResult::kContinue;
}

/** @brief IfPlayerHp ノード: プレイヤーの今のHPを op で value と比べ、真なら nextTrue、偽なら nextFalse */
NodeResult ExecIfPlayerHp(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    const CompareOp op = ParseCompareOp(AsString(rt.ResolveParam(node, "op", std::string("<="))));
    const GraphValue value = rt.ResolveParam(node, "value", GraphValue { kOne });
    const GraphValue hp { static_cast<float>(RunData::GetInstance()->GetHp()) };
    const bool result = Compare(op, hp, value);
    rt.SetNodeOutput(node.id, hp);
    outNextId = result ? node.nextTrue : node.nextFalse;
    return NodeResult::kContinue;
}

/** @brief GetPlayerHp ノード: プレイヤーの今のHPを出力する */
NodeResult ExecGetPlayerHp(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    rt.SetNodeOutput(node.id, GraphValue { static_cast<float>(RunData::GetInstance()->GetHp()) });
    outNextId = node.next;
    return NodeResult::kContinue;
}

/** @brief Branch ノード: condition（Compare や And の出力をつなぐ）が真なら nextTrue、偽なら nextFalse */
NodeResult ExecBranch(GraphRuntime& rt, const GraphNode& node, std::string& outNextId) {
    const bool condition = AsBool(rt.ResolveParam(node, "condition", GraphValue { false }));
    outNextId = condition ? node.nextTrue : node.nextFalse;
    return NodeResult::kContinue;
}
} // namespace

/** @brief 進行の流れ・演出の追加・プレイヤーの状態を扱うノードの定義と実行関数を登録する */
void NodeRegistry::RegisterProgressNodes() {
    using VT = GraphValueType;
    // ── 演出（文字・スローモーション・カメラの注目）──
    Register("ShowMessage", ExecShowMessage);
    RegisterSpec("ShowMessage", { { { "text", VT::kString, Text("メッセージ") }, { "seconds", VT::kFloat, kDefaultMessageSeconds } }, false, VT::kAny, "画面の中央にtextをseconds秒出す（字幕・案内・会話に）", "演出・音声" });
    Register("SlowMotion", ExecSlowMotion);
    RegisterSpec("SlowMotion", { { { "scale", VT::kFloat, kDefaultSlowScale }, { "seconds", VT::kFloat, kDefaultSlowSeconds } }, false, VT::kAny, "seconds秒のあいだゲームをscale倍の速さにする（0.3なら3割の速さ）", "演出・音声" });
    Register("FocusCamera", ExecFocusCamera);
    RegisterSpec("FocusCamera", { { { "x", VT::kFloat }, { "y", VT::kFloat }, { "seconds", VT::kFloat, kDefaultFocusSeconds } }, false, VT::kAny, "seconds秒のあいだカメラを座標(x,y)へ向け、その後プレイヤーの追従へ戻す（開いた扉や出てきた敵を見せる時に）", "演出・音声" });
    // ── 進行の流れ（繰り返し・条件を待つ・分岐）──
    Register("Repeat", ExecRepeat);
    RegisterSpec("Repeat", { { { "count", VT::kFloat, kDefaultRepeatCount }, { "interval", VT::kFloat, kOne } }, true, VT::kFloat, "interval秒ごとに真の出口へ進み、count回終えたら偽の出口へ進む（真の先の最後をこのノードへ戻す。countが0ならずっと。出力は何回目か）", "よく使う", true });
    Register("Branch", ExecBranch);
    RegisterSpec("Branch", { { { "condition", VT::kBool, false } }, false, VT::kAny, "conditionが真なら真の出口、偽なら偽の出口へ進む（CompareやAndの出力をつないで使う）", "よく使う", true });
    Register("WaitForFlag", ExecWaitForFlag);
    RegisterSpec("WaitForFlag", { { { "flag", VT::kString, Text("flag") }, { "value", VT::kBool, true } }, false, VT::kAny, "フラグflagがvalueになるまで待つ（トリガーやほかのグラフが立てるのを待つ）", "進行" });
    Register("WaitAllEnemiesDefeated", ExecWaitAllEnemiesDefeated);
    RegisterSpec("WaitAllEnemiesDefeated", { { }, false, VT::kAny, "今出ている敵が全員倒されるまで待つ（まだ出現していない敵は数えない）", "進行" });
    Register("WaitEnemyDefeated", ExecWaitEnemyDefeated);
    RegisterSpec("WaitEnemyDefeated", { { { "target", VT::kString, kDefaultEnemyTarget } }, false, VT::kAny, "targetという名前の敵が倒されるまで待つ（見つからなければすぐ次へ）", "進行" });
    // ── プレイヤー（武器・覚醒・操作・判定）──
    Register("GiveWeapon", ExecGiveWeapon);
    RegisterSpec("GiveWeapon", { { { "weapon", VT::kString, Text("Sword") } }, false, VT::kAny, "weaponの武器（Sword Spear Hammer Dagger Ball Greatsword Scythe Axe）を手に入れて装備する", "プレイヤー" });
    Register("ChargeAwaken", ExecChargeAwaken);
    RegisterSpec("ChargeAwaken", { { { "amount", VT::kFloat, kDefaultAwakenAmount } }, false, VT::kAny, "プレイヤーの覚醒ゲージをamount増やす（1で満タン）", "プレイヤー" });
    Register("LockPlayerControl", ExecLockPlayerControl);
    RegisterSpec("LockPlayerControl", { { { "locked", VT::kBool, true } }, false, VT::kAny, "lockedがONならプレイヤーの操作を止め、OFFで戻す（演出の間に動かれないように。ポーズは使える）", "プレイヤー" });
    Register("IfPlayerInArea", ExecIfPlayerInArea);
    RegisterSpec("IfPlayerInArea", { { { "x", VT::kFloat }, { "y", VT::kFloat }, { "width", VT::kFloat, kDefaultAreaSize }, { "height", VT::kFloat, kDefaultAreaSize } }, true, VT::kBool, "プレイヤーが中心(x,y)・幅width・高さheightの範囲にいれば真、いなければ偽の出口へ進む", "プレイヤー", true });
    Register("IfPlayerHp", ExecIfPlayerHp);
    RegisterSpec("IfPlayerHp", { { { "op", VT::kString, Text("<=") }, { "value", VT::kFloat, kOne } }, true, VT::kFloat, "プレイヤーの今のHPをop（== != < <= > >=）でvalueと比べ、真なら真、偽なら偽の出口へ進む", "プレイヤー", true });
    Register("GetPlayerHp", ExecGetPlayerHp);
    RegisterSpec("GetPlayerHp", { { }, true, VT::kFloat, "プレイヤーの今のHPを出力する", "プレイヤー" });
}
} // namespace engine::game

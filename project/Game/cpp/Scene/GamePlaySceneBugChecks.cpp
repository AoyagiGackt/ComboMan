/**
 * @file GamePlaySceneBugChecks.cpp
 * @brief 自動テストプレイの間だけ、遊んでいる様子からおかしな所を探して SmokeTestRunner へ知らせる
 * @note 見つけても止めずに記録だけする。一瞬だけの様子（吹き飛んで壁をかすめた等）は数えず、続いた時だけ知らせる
 */
#include "GamePlayScene.h"
#include "GameConstants.h"
#include "MeleeCombo.h"
#include "WeaponManager.h"
#include "RunData.h"
#include "SmokeTestRunner.h"
#include "StageEditor.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <string>

namespace engine::game {
namespace {
constexpr size_t kPositionTextCapacity = 64; // 位置を文字にする時の入れ物の大きさ
constexpr int kCheckIntervalFrames = 15; // 調べる間隔（約0.25秒）
constexpr float kWallColumnTolerance = 0.05f; // 真ん中の横の位置がこれだけ近いブロックは、同じ列に積まれた壁とみなす
constexpr float kWallStackGap = 0.05f; // 上下のすき間がこれ以下なら、積み上がった1本の壁とみなす
constexpr float kTrailFootClearance = 0.05f; // 体の下端をこれだけ上げて調べる（立っている足元のブロックを壁と数えない）
constexpr size_t kPlayerTrailSamples = 12; // 床抜けの報告に添える直前の様子の数（約3秒ぶん）
constexpr float kWeaponChoiceStuckSeconds = 15.0f; // 交換や主軸を選ぶ画面がこれ以上出たままなら止まっているとみなす
constexpr float kAllyFarRatio = 1.5f; // 味方のボスがプレイヤーから戻す距離のこれだけ倍より離れたままならおかしい
constexpr float kAllyFallMargin = 15.0f; // 味方のボスがプレイヤーよりこれだけ下にいたままならおかしい
constexpr float kSustainSeconds = 2.0f; // おかしな様子がこれだけ続いたら知らせる
constexpr float kConditionSustainSeconds = 3.0f; // 扉の条件は、倒し切ってから成立するまでの遅れを見込んで少し長く待つ
constexpr float kStallSeconds = 20.0f; // 先へ進む操作をしているのに、これだけ右へ進めなければ知らせる
constexpr float kProgressEpsilon = 0.5f; // これ以上右へ進んだら進んだとみなす
constexpr float kBoundsMarginX = 0.0f; // ステージの地形の端からこれ以上外は、ステージの外とみなす（端の壁の外側へ抜けた敵も見つける）
constexpr float kBoundsMarginAbove = 3.0f; // 地形の一番上からこれ以上上は、届かない所とみなす
constexpr float kFallMargin = 10.0f; // 地形の一番下からこれ以上下は、床を抜けて落ちたとみなす
constexpr float kBossNearDistance = 30.0f; // ボスがこの距離にいる間は、ボス戦の最中として進めなくても知らせない
constexpr float kPassThroughWallHeight = 4.0f; // これより高い壁や扉を、前に調べた時から今までに横切っていたら、すり抜けたとみなす（跳び越せない高さ）
constexpr float kPlayerBodyHalfHeight = 0.5f; // プレイヤーの当たり判定の高さの半分
constexpr float kEnemyTeleportDistance = 8.0f; // 前に調べた時（約0.25秒前）からこれ以上横へ動いた生きている敵は、瞬間移動したとみなす（速く動いて当然の時は除く）
constexpr float kHalf = 0.5f;
constexpr float kFightableEnemyDistance = 6.0f; // 扉の条件で待っている倒せる敵がこの距離にいる間は、ボットが倒せていないだけとみなして知らせない

/** @brief 位置に数でない値が混ざっていないか */
bool IsFinite(const Vector3& position) {
    return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
}

/** @brief 位置を報告用の文字にする */
std::string FormatPosition(const Vector3& position) {
    char text[kPositionTextCapacity];
    std::snprintf(text, sizeof(text), "(%.1f, %.1f)", position.x, position.y);
    return text;
}

/** @brief 地形の箱をまとめて、ステージの広さの箱を作る（地形が無ければ false） */
bool ComputeStageBounds(const std::vector<AABB>& terrain, AABB& bounds) {
    if (terrain.empty()) {
        return false;
    }
    bounds = { { FLT_MAX, FLT_MAX, -FLT_MAX }, { -FLT_MAX, -FLT_MAX, FLT_MAX } };
    for (const AABB& box : terrain) {
        bounds.min.x = (std::min)(bounds.min.x, box.min.x);
        bounds.min.y = (std::min)(bounds.min.y, box.min.y);
        bounds.max.x = (std::max)(bounds.max.x, box.max.x);
        bounds.max.y = (std::max)(bounds.max.y, box.max.y);
    }
    return true;
}

/** @brief その位置が地形の中に埋まっているか（奥行きは2.5Dなので見ない） */
bool IsInsideTerrain(const Vector3& position, const std::vector<AABB>& terrain) {
    return std::any_of(terrain.begin(), terrain.end(), [&](const AABB& box) {
        return position.x > box.min.x && position.x < box.max.x && position.y > box.min.y && position.y < box.max.y;
    });
}

/** @brief 届かない所にいるなら、その理由を返す（いなければ空） */
std::string UnreachableReason(const Vector3& position, bool visible, const AABB& bounds, bool hasBounds, const std::vector<AABB>& terrain) {
    if (!IsFinite(position)) {
        return "位置が数でない値";
    }
    if (!visible) {
        return "見えないのに生きている";
    }
    if (hasBounds) {
        if (position.x < bounds.min.x - kBoundsMarginX || position.x > bounds.max.x + kBoundsMarginX) {
            return "ステージの横の外にいる";
        }
        if (position.y > bounds.max.y + kBoundsMarginAbove) {
            return "地形の一番上より上（屋根の上など）にいる";
        }
        if (position.y < bounds.min.y - kFallMargin) {
            return "床を抜けて下に落ちている";
        }
    }
    if (IsInsideTerrain(position, terrain)) {
        return "地形の中に埋まっている";
    }
    return std::string();
}

/**
 * @brief おかしな様子が続いた秒数を数え、しきい値を超えたら true（様子が消えたら数え直す）
 * @param timers 続いている秒数の一覧（相手ごと）
 * @param key 相手を区別する名前
 * @param active 今おかしな様子か
 */
bool SustainedFor(std::vector<std::pair<std::string, float>>& timers, const std::string& key, bool active, float elapsed, float threshold) {
    auto found = std::find_if(timers.begin(), timers.end(), [&](const auto& entry) {
        return entry.first == key;
    });
    if (!active) {
        if (found != timers.end()) {
            timers.erase(found);
        }
        return false;
    }
    if (found == timers.end()) {
        timers.emplace_back(key, 0.0f);
        found = std::prev(timers.end());
    }
    found->second += elapsed;
    return found->second >= threshold;
}
} // namespace

/** @brief 自動テストプレイの見張りで、プレイヤーの今の様子を直前の記録に足す */
void GamePlayScene::RecordPlayerTrail(const Vector3& position) {
    std::string sample = FormatPosition(position) + (player_->IsOnGround() ? " 接地" : " 空中");
    // 床抜けにつながりそうな動きの最中かを添える
    if (player_->IsDodging()) {
        sample += " 回避";
    }
    if (player_->IsMeleeAttacking()) {
        const MeleeAttackDef* attack = player_->GetActiveMeleeAttack();
        sample += attack != nullptr ? " 攻撃 " + std::string(attack->id) : " 攻撃";
    }
    if (player_->IsSkillSequenceActive()) {
        sample += " 固有技";
    }
    if (player_->IsWarping()) {
        sample += " ワープ";
    }
    if (player_->IsRampaging()) {
        sample += " 乱舞";
    }
    if (bossExecution_.IsActive()) {
        sample += " とどめ";
    }
    if (enemy_ != nullptr && !enemy_->IsDefeated()) {
        sample += " ボス " + FormatPosition(enemy_->GetPosition());
    }
    bugWatch_.playerTrail.push_back(sample);
    if (bugWatch_.playerTrail.size() > kPlayerTrailSamples) {
        bugWatch_.playerTrail.erase(bugWatch_.playerTrail.begin());
    }
}

/** @brief 自動テストプレイの時だけ、床抜けを直前の様子と一緒に知らせる */
void GamePlayScene::ReportPlayerFall(const Vector3& position) {
    if (!SmokeTestRunner::IsActive()) {
        return;
    }
    std::string trail;
    for (const std::string& sample : bugWatch_.playerTrail) {
        trail += (trail.empty() ? "" : " → ") + sample;
    }
    SmokeTestRunner::ReportIssue("プレイヤー", "落下", "床を抜けて地形の一番下より下にいた " + FormatPosition(position)
        + "（最後に立っていた床へ戻した）。最後に床に立っていた所 " + FormatPosition(lastSafeGround_)
        + "。直前の様子（約 " + std::to_string(kCheckIntervalFrames) + " フレームごと、古い順）: " + trail);
}

/** @brief 自動テストプレイの見張りで、武器の持ち方におかしな所が無いか調べる（選ぶ画面が出たまま・合体の番号が範囲の外など） */
void GamePlayScene::CheckWeaponsForBugs(float elapsed) {
    const WeaponManager* weapons = WeaponManager::GetInstance();
    const int weaponCount = static_cast<int>(weapons->GetList().size());
    // 交換の画面と主軸を選ぶ画面は、自動の操作でも決定キーで閉じるはずなので、長く出たままなら止まっている
    const bool choosing = weapons->HasPendingWeapon() || weapons->HasPendingBossChoice();
    if (SustainedFor(bugWatch_.suspectSeconds, "weapon_choice", choosing, elapsed, kWeaponChoiceStuckSeconds)) {
        SmokeTestRunner::ReportIssue("武器", "選ぶ画面", std::string(weapons->HasPendingBossChoice() ? "ボスの武器の主軸を選ぶ画面" : "武器の交換の画面")
            + "が " + std::to_string(static_cast<int>(kWeaponChoiceStuckSeconds)) + " 秒以上出たまま");
    }
    // 選んでいる枠と、手に持っている武器が食い違っていないか
    const int selected = weapons->GetSelectedSlot();
    if (selected >= 0 && weapons->HasEquippedWeapon() && weapons->GetSlotWeaponIndex(selected) != weapons->GetIndex()) {
        SmokeTestRunner::ReportIssue("武器", "持っている武器", "枠 " + std::to_string(selected + 1) + " の武器と、手に持っている武器が違う");
    }
    // 合体の相棒の番号が、武器の一覧の中にあるか（ボスの枠も含む）
    for (int slot = 0; slot <= WeaponManager::kBossSlot; ++slot) {
        const int partner = weapons->GetFusedWeaponIndex(slot);
        if (partner >= weaponCount || (partner >= 0 && weapons->GetSlotWeaponIndex(slot) < 0)) {
            SmokeTestRunner::ReportIssue("武器", "合体", "枠 " + std::to_string(slot + 1) + " の合体の相棒の番号がおかしい " + std::to_string(partner));
        }
    }
}

/** @brief 自動テストプレイの見張りで、味方のボスにおかしな所が無いか調べる（離れすぎたまま・落ちたまま・体力が範囲の外） */
void GamePlayScene::CheckAllyForBugs(float elapsed) {
    if (!ally_.IsActive()) {
        return;
    }
    const Vector3& allyPos = ally_.GetPosition();
    const Vector3 playerPos = player_->GetPosition();
    // 離れすぎたら戻すはずなので、離れたままなら戻す処理が効いていない
    const bool tooFar = std::abs(allyPos.x - playerPos.x) > AllyTune().leashDistance * kAllyFarRatio;
    if (SustainedFor(bugWatch_.suspectSeconds, "ally_far", tooFar, elapsed, kSustainSeconds)) {
        SmokeTestRunner::ReportIssue("味方のボス", "離れすぎ", "プレイヤー " + FormatPosition(playerPos) + " から離れたまま " + FormatPosition(allyPos));
    }
    const bool fellOut = allyPos.y < playerPos.y - kAllyFallMargin;
    if (SustainedFor(bugWatch_.suspectSeconds, "ally_fall", fellOut, elapsed, kSustainSeconds)) {
        SmokeTestRunner::ReportIssue("味方のボス", "落下", "プレイヤーよりずっと下にいる " + FormatPosition(allyPos));
    }
    const float hp = ally_.HpRatio();
    if (hp < 0.0f || hp > 1.0f) {
        SmokeTestRunner::ReportIssue("味方のボス", "体力", "体力の割合が範囲の外 " + std::to_string(hp));
    }
}

/** @brief 自動テストプレイの間だけ、遊んでいる様子からおかしな所を探して SmokeTestRunner へ知らせる */
void GamePlayScene::RunBugChecks() {
    if (!SmokeTestRunner::IsActive() || clearTriggered_ || playerDefeat_.IsActive()) {
        return;
    }
    if (--bugWatch_.framesUntilCheck > 0) {
        return;
    }
    bugWatch_.framesUntilCheck = kCheckIntervalFrames;
    const float elapsed = static_cast<float>(kCheckIntervalFrames) * gameConstants::kFrameDeltaTime;

    // 地形（敵の体と扉を除く）から、ステージの広さを測る
    const std::vector<AABB> terrain = GetStageEditor().GetCameraBoundColliders();
    AABB stageBounds {};
    const bool hasBounds = ComputeStageBounds(terrain, stageBounds);
    CheckPlayerForBugs(stageBounds, hasBounds, terrain, elapsed);
    CheckWeaponsForBugs(elapsed);
    CheckAllyForBugs(elapsed);
    CheckEnemiesForBugs(stageBounds, hasBounds, terrain, elapsed);
    CheckEnemyGroupConditionsForBugs(stageBounds, hasBounds, terrain, elapsed);
}

/** @brief プレイヤーの位置・体力・先へ進めるかを調べる */
void GamePlayScene::CheckPlayerForBugs(const AABB& stageBounds, bool hasBounds, const std::vector<AABB>& terrain, float elapsed) {
    const Vector3 position = player_->GetPosition();
    if (!IsFinite(position)) {
        SmokeTestRunner::ReportIssue("プレイヤー", "位置", "位置に数でない値が入った");
        return;
    }
    // 手順がプレイヤーを離れた所へ移した後は、移す前の位置と比べない（壁抜けや先へ進めないと間違えないように）
    if (bugWatch_.teleportCount != SmokeTestRunner::TeleportCount()) {
        bugWatch_.teleportCount = SmokeTestRunner::TeleportCount();
        bugWatch_.hasLastPlayerPosition = false;
        bugWatch_.wasExpectingProgress = false;
        bugWatch_.playerTrail.clear();
        bugWatch_.lastGroundedPosition = position;
    }
    if (player_->IsOnGround()) {
        bugWatch_.lastGroundedPosition = position;
    }
    RecordPlayerTrail(position);
    // 前に調べた時から今までに、跳び越せない高さの壁や閉じた扉の真ん中を体の高さで横切っていたら、すり抜けている
    if (bugWatch_.hasLastPlayerPosition) {
        const Vector3& from = bugWatch_.lastPlayerPosition;
        const float minX = (std::min)(from.x, position.x);
        const float maxX = (std::max)(from.x, position.x);
        const std::vector<AABB> solids = GetStageEditor().GetTerrainColliders();
        // 高さ1のブロックを積んだ壁もあるので、同じ横の位置にすき間なく積み上がったブロックを1本の壁として見る
        // （床・階段・天井のように間の空いたブロックは別々に数える。立っている足元のブロックは体に数えない）
        const auto wallSegmentAt = [&](float centerX, float y, float& bottom, float& top) {
            bool found = false;
            for (const AABB& other : solids) {
                const bool sameColumn = std::abs((other.min.x + other.max.x) * kHalf - centerX) < kWallColumnTolerance;
                if (sameColumn && other.max.y > y - kPlayerBodyHalfHeight + kTrailFootClearance && other.min.y < y + kPlayerBodyHalfHeight) {
                    bottom = other.min.y;
                    top = other.max.y;
                    found = true;
                    break;
                }
            }
            if (!found) {
                return false;
            }
            // 上下に接しているブロックをつなげて、積み上がった壁の上下の端を求める
            bool extended = true;
            while (extended) {
                extended = false;
                for (const AABB& other : solids) {
                    if (std::abs((other.min.x + other.max.x) * kHalf - centerX) >= kWallColumnTolerance) {
                        continue;
                    }
                    if (other.min.y < bottom && other.max.y >= bottom - kWallStackGap) {
                        bottom = other.min.y;
                        extended = true;
                    }
                    if (other.max.y > top && other.min.y <= top + kWallStackGap) {
                        top = other.max.y;
                        extended = true;
                    }
                }
            }
            return true;
        };
        for (const AABB& block : solids) {
            const float centerX = (block.min.x + block.max.x) * kHalf;
            if (!(minX < centerX && centerX < maxX)) {
                continue;
            }
            // 動く前と後の両方の体の高さに、同じ1本の高い壁があれば、すり抜けている
            float fromBottom = 0.0f;
            float fromTop = 0.0f;
            float toBottom = 0.0f;
            float toTop = 0.0f;
            const bool atFrom = wallSegmentAt(centerX, from.y, fromBottom, fromTop);
            const bool atTo = wallSegmentAt(centerX, position.y, toBottom, toTop);
            const bool sameWall = atFrom && atTo && fromBottom == toBottom && fromTop == toTop;
            if (sameWall && fromTop - fromBottom >= kPassThroughWallHeight) {
                SmokeTestRunner::ReportIssue("プレイヤー", "壁抜け", FormatPosition(from) + " から " + FormatPosition(position)
                    + " へ動いた間に、x=" + std::to_string(centerX) + " の壁か閉じた扉をすり抜けた");
                break;
            }
        }
    }
    bugWatch_.lastPlayerPosition = position;
    bugWatch_.hasLastPlayerPosition = true;
    const bool fellOut = hasBounds && position.y < stageBounds.min.y - kFallMargin;
    if (SustainedFor(bugWatch_.suspectSeconds, "player_fall", fellOut, elapsed, kSustainSeconds)) {
        // どこから抜けたかを追えるよう、最後に床に立っていた所も添える
        SmokeTestRunner::ReportIssue("プレイヤー", "落下", "床を抜けて地形の一番下より下にいる " + FormatPosition(position)
            + "。最後に床に立っていた所 " + FormatPosition(bugWatch_.lastGroundedPosition));
    }
    const RunData* runData = RunData::GetInstance();
    if (runData->GetHp() < 0 || runData->GetHp() > runData->GetMaxHp()) {
        SmokeTestRunner::ReportIssue("プレイヤー", "体力", "体力が範囲の外 " + std::to_string(runData->GetHp()) + " / " + std::to_string(runData->GetMaxHp()));
    }

    // 先へ進む操作をしている間に、右へ進めなくなっていないか（ボス戦の最中は止まって当然なので数えない）
    const bool expecting = SmokeTestRunner::IsExpectingProgress();
    if (!expecting) {
        bugWatch_.wasExpectingProgress = false;
        return;
    }
    if (!bugWatch_.wasExpectingProgress) {
        bugWatch_.wasExpectingProgress = true;
        bugWatch_.bestX = position.x;
        bugWatch_.stallSeconds = 0.0f;
        return;
    }
    if (position.x > bugWatch_.bestX + kProgressEpsilon) {
        bugWatch_.bestX = position.x;
        bugWatch_.stallSeconds = 0.0f;
        return;
    }
    const bool bossNearOrDown = enemy_ != nullptr
        && (enemy_->IsDefeated() || std::abs(enemy_->GetPosition().x - position.x) < kBossNearDistance);
    // 近くに倒せる敵が残っていて扉が閉まっているのは当然（右を向いたままのボットが背中側の敵を倒せていないだけ）
    const auto fightableNear = [&]() {
        for (const EnemyGroupWatch& watch : GetStageEditor().CollectEnemyGroupWatches()) {
            if (watch.met) {
                continue;
            }
            for (const WatchedEnemyState& enemy : watch.enemies) {
                const bool fightable = !enemy.defeated && enemy.active
                    && UnreachableReason(enemy.position, enemy.visible, stageBounds, hasBounds, terrain).empty();
                if (fightable && std::abs(enemy.position.x - position.x) <= kFightableEnemyDistance) {
                    return true;
                }
            }
        }
        return false;
    };
    if (bossNearOrDown || IsFinalBossMode() || fightableNear()) {
        bugWatch_.stallSeconds = 0.0f;
        return;
    }
    bugWatch_.stallSeconds += elapsed;
    if (bugWatch_.stallSeconds < kStallSeconds) {
        return;
    }
    bugWatch_.stallSeconds = 0.0f;
    // どの条件が成立していないかと、その条件で生き残っている敵の様子も添えて、扉の前で止まっている訳を分かるようにする
    std::string unmet;
    std::string survivors;
    for (const EnemyGroupWatch& watch : GetStageEditor().CollectEnemyGroupWatches()) {
        if (watch.met) {
            continue;
        }
        unmet += (unmet.empty() ? "" : ", ") + watch.conditionName;
        for (const WatchedEnemyState& enemy : watch.enemies) {
            if (enemy.defeated) {
                continue;
            }
            survivors += (survivors.empty() ? "" : ", ") + enemy.name + " " + FormatPosition(enemy.position) + " 体力 " + std::to_string(enemy.hp)
                + (enemy.active ? "" : " 無効中") + (enemy.visible ? "" : " 見えない");
        }
    }
    SmokeTestRunner::ReportIssue("先へ進めない", "x=" + std::to_string(static_cast<int>(bugWatch_.bestX)),
        "右へ進む操作を続けても " + FormatPosition(position) + " から進めない。成立していない条件: " + (unmet.empty() ? "なし" : unmet)
            + "。残っている敵: " + (survivors.empty() ? "なし" : survivors));
}

/** @brief 生きている敵が届かない所にいないか・生き返っていないかを調べる */
void GamePlayScene::CheckEnemiesForBugs(const AABB& stageBounds, bool hasBounds, const std::vector<AABB>& terrain, float elapsed) {
    for (const CombatEnemyRef& ref : GetStageEditor().GetCombatEnemies()) {
        const EnemyEntity* enemy = ref.enemy;
        const std::pair<const EnemyEntity*, uint64_t> identity = { enemy, ref.instanceId };
        auto& seen = bugWatch_.defeatedSeen;
        const bool wasDefeated = std::find(seen.begin(), seen.end(), identity) != seen.end();
        if (enemy->IsDefeated()) {
            if (!wasDefeated) {
                seen.push_back(identity);
            }
            SustainedFor(bugWatch_.suspectSeconds, "enemy_" + ref.name, false, elapsed, kSustainSeconds);
            continue;
        }
        // 前に調べた時から理由も無く一気に遠くへ動いていたら、どこからどこへ動いたかを知らせる（端へ戻された・持ち場へ戻された敵を見つける）
        // ボスの技・吹き飛び・急降下などの台本の動きは速くて当然なので、前か今のどちらかがそうなら数えない
        const Vector3 position = enemy->GetPosition();
        const bool fastMoveExpected = ref.isStageBoss || enemy->IsBlownAway() || enemy->IsScripted();
        const auto last = bugWatch_.enemyLastPositions.find(ref.instanceId);
        if (last != bugWatch_.enemyLastPositions.end() && !last->second.fastMoveExpected && !fastMoveExpected
            && std::abs(position.x - last->second.position.x) >= kEnemyTeleportDistance) {
            SmokeTestRunner::ReportIssue("敵の位置", "瞬間移動 " + ref.name,
                FormatPosition(last->second.position) + " から " + FormatPosition(position) + " へ、吹き飛びも技も無しに一気に動いた（体力 "
                    + std::to_string(enemy->GetHp()) + "）");
        }
        bugWatch_.enemyLastPositions[ref.instanceId] = { position, fastMoveExpected };
        // 倒されたのを見た敵が、同じ実体のまま倒されていない扱いに戻った
        if (wasDefeated) {
            SmokeTestRunner::ReportIssue("敵が生き返った", ref.name,
                "倒された後に、倒されていない扱いに戻った（体力 " + std::to_string(enemy->GetHp()) + "、" + FormatPosition(enemy->GetPosition())
                    + (enemy->IsVisible() ? "" : "、見えない") + "）");
            seen.erase(std::remove(seen.begin(), seen.end(), identity), seen.end());
        }
        const std::string reason = UnreachableReason(enemy->GetPosition(), enemy->IsVisible(), stageBounds, hasBounds, terrain);
        if (SustainedFor(bugWatch_.suspectSeconds, "enemy_" + ref.name, !reason.empty(), elapsed, kSustainSeconds)) {
            SmokeTestRunner::ReportIssue("届かない所の敵", ref.name,
                reason + " " + FormatPosition(enemy->GetPosition()) + " 体力 " + std::to_string(enemy->GetHp()) + (ref.isStageBoss ? "（ボス）" : ""));
        }
    }
}

/** @brief 敵グループの全滅を待つ条件（扉など）が、倒し切ったのに成立しない所を探す */
void GamePlayScene::CheckEnemyGroupConditionsForBugs(const AABB& stageBounds, bool hasBounds, const std::vector<AABB>& terrain, float elapsed) {
    for (const EnemyGroupWatch& watch : GetStageEditor().CollectEnemyGroupWatches()) {
        // 出現待ちの敵がいる間と、敵がまだ1体も出ていない間は成立しなくて当然
        bool stuck = false;
        std::string detail;
        if (!watch.met && !watch.waitingSpawn && !watch.enemies.empty()) {
            // 生きている敵が残っていて、その全員が届かない所にいたら、プレイヤーには倒し切ったように見えている
            int aliveCount = 0;
            int unreachableCount = 0;
            for (const WatchedEnemyState& enemy : watch.enemies) {
                if (enemy.defeated) {
                    continue;
                }
                ++aliveCount;
                const std::string reason = enemy.active ? UnreachableReason(enemy.position, enemy.visible, stageBounds, hasBounds, terrain)
                                                        : std::string("無効中なのに生きている（攻撃が当たらない）");
                if (!reason.empty()) {
                    ++unreachableCount;
                    detail += (detail.empty() ? "" : ", ") + enemy.name + "（" + reason + " " + FormatPosition(enemy.position)
                        + " 体力 " + std::to_string(enemy.hp) + "）";
                }
            }
            if (aliveCount == 0) {
                stuck = true;
                detail = "全員倒されているのに条件が成立していない";
            } else if (unreachableCount == aliveCount) {
                stuck = true;
                detail = "残りの敵が全員届かない所にいる: " + detail;
            }
        }
        if (SustainedFor(bugWatch_.suspectSeconds, "condition_" + watch.conditionName, stuck, elapsed, kConditionSustainSeconds)) {
            SmokeTestRunner::ReportIssue("扉の条件が成立しない", watch.conditionName, "グループ " + watch.group + ": " + detail);
        }
    }
}

} // namespace engine::game

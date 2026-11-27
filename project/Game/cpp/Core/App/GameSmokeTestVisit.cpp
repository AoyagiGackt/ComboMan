/**
 * @file GameSmokeTestVisit.cpp
 * @brief バグ探しで、前回から変えたステージだけを遊ぶための飛ばし方と、変えた配置物の近くへ移って試す手順の実装
 */
#include "GameSmokeTest.h"
#include "FinalBossTuning.h"
#include "GameRules.h"
#include "Input.h"
#include "Logger.h"
#include "Player.h"
#include "PlayerBridge.h"
#include "WeaponManager.h"
#include "RunData.h"
#include "SceneManager.h"
#include "StageEditor.h"
#include <algorithm>
#include <cstdio>

namespace engine::game {
namespace {
constexpr const char* kTutorialLabel = "チュートリアル";
constexpr const char* kStageLabelPrefix = "ステージ ";
constexpr const char* kFinalBossLabel = "ラスボス戦";
constexpr size_t kPositionTextCapacity = 48; // 位置を文字にする時の長さ
constexpr const char* kPositionFormat = "(%.1f, %.1f)";

/** @brief 位置を報告用の文字にする */
std::string FormatPosition(const Vector3& position) {
    char text[kPositionTextCapacity];
    std::snprintf(text, sizeof(text), kPositionFormat, position.x, position.y);
    return text;
}
} // namespace

/** @brief 実際の拾得判定と装備枠を使って、投擲と最後の合体の結果を調べる */
bool GameSmokeTest::CheckTrainingWeapon(const SmokeTestStep& step) {
    if (!runner_.IsSettledOn("TRAINING")) {
        runner_.Fail("武器の検証にはトレーニングルームが必要です");
        return false;
    }
    Player* player = PlayerBridge::GetInstance()->Get();
    auto* weapons = WeaponManager::GetInstance();
    if (step.action == "resetTrainingWeapons") {
        weapons->Reset();
        return true;
    }
    const std::string weaponName = WeaponName(step);
    if (step.action == "visitTrainingPickup") {
        // 並んでいる実物の位置に移す。装備は次のシーン更新の接触判定で決まる。
        static constexpr std::pair<WeaponType, float> kPositions[] = {
            { WeaponType::kGreatsword, 16.5f }, { WeaponType::kScythe, 19.5f }, { WeaponType::kAxe, 22.5f }
        };
        constexpr float kPickupHeight = 3.0f;
        for (const auto& [type, x] : kPositions) {
            if (weaponName == WeaponTypeName(type)) {
                player->SetPosition({ x, kPickupHeight, 0.0f });
                return true;
            }
        }
        runner_.Fail("検証用の配置位置がありません: " + weaponName);
        return false;
    }
    if (!weapons->HasEquippedWeapon() || WeaponTypeName(weapons->GetCurrent().type) != weaponName
        || (!weapons->IsBossSlotSelected() && !weapons->IsUnlocked(weapons->GetIndex()))) {
        runner_.Fail("期待した装備になっていません: " + weaponName);
        return false;
    }
    if (step.action == "expectDuplicateWeapon") {
        const auto state = weapons->SaveSnapshot();
        if (std::count(state.slots.begin(), state.slots.end(), weapons->GetIndex()) != 2) {
            runner_.Fail("同じ種類の武器が2つの枠に入りませんでした");
            return false;
        }
    }
    if (step.action == "checkLastFusion") {
        const int slot = weapons->GetSelectedSlot();
        if (weapons->TryBreakCurrentWeapon() || !weapons->FuseInto(slot, WeaponType::kAxe, 1)
            || !weapons->ConsumeFusionDurability(1) || weapons->IsCurrentFused()
            || weapons->GetSelectedSlot() != slot || !weapons->HasEquippedWeapon()
            || WeaponTypeName(weapons->GetCurrent().type) != weaponName) {
            runner_.Fail("最後の合体武器が元の武器に戻りませんでした");
            return false;
        }
    }
    return true;
}

/** @brief 前回の記録と比べるステージの一覧を返す */
std::vector<BugHuntChanges::Stage> GameSmokeTest::GetStages() const {
    const GameRulesData& rules = GameRules::GetInstance()->Get();
    std::vector<BugHuntChanges::Stage> stages;
    if (!rules.tutorialLevelPath.empty()) {
        stages.push_back({ rules.tutorialLevelPath, kTutorialLabel });
    }
    for (size_t i = 0; i < rules.levelPaths.size(); ++i) {
        stages.push_back({ rules.levelPaths[i], kStageLabelPrefix + std::to_string(i + 1) });
    }
    stages.push_back({ FinalBossTuningStore::Get().level, kFinalBossLabel });
    return stages;
}

/** @brief 変えていないステージを遊ばずに、クリアした扱いで進行度だけ進める */
void GameSmokeTest::SkipFloor(int floor) {
    RunData* runData = RunData::GetInstance();
    // クリアした時と同じく進行度を進め、最後のステージならラスボス戦を残す（解放演出は見せない）
    runData->SetPlayFloor(floor);
    runData->AdvanceFloor();
    runData->ConsumeLiberationPending();
    if (runData->GetFloor() >= GameRules::GetInstance()->Get().finalFloor) {
        runData->SetFinalBossPending(true);
    }
    runner_.SetSkippingStage(true);
    runner_.AddLog("    ステージ " + std::to_string(floor + 1) + " は前回から変えていないので飛ばす");
}

/** @brief 最後のステージの後にラスボス戦へ入る（変えていなければ、ここで手順を終える。終わったら true） */
bool GameSmokeTest::EnterFinalBoss(const SmokeTestStep& step) {
    SceneManager* scenes = SceneManager::GetInstance();
    // 最後のステージのクリアから画面が切り替わっている途中なら、落ち着くまで待つ
    if (scenes->IsChangingScene()) {
        if (runner_.GetStepFrame() >= step.timeoutFrames) {
            runner_.Fail("enterFinalBoss: 画面の切り替えが終わりません");
        }
        return false;
    }
    runner_.SetSkippingStage(false);
    if (!runner_.GetChanges().IsStageSelected(FinalBossTuningStore::Get().level)) {
        runner_.FinishEarly("ラスボス戦は前回から変えていないので、ここで終える");
        return true;
    }
    // 最後のステージを遊んでクリアした時は、そのまま続けてラスボス戦に入っている
    if (scenes->GetCurrentSceneName() == kGameplaySceneName) {
        return true;
    }
    // 最後のステージを飛ばした時は、マップからラスボス戦へ入る
    if (!RunData::GetInstance()->IsFinalBossPending()) {
        runner_.Fail("enterFinalBoss: ラスボス戦が残っていません（全部のステージを終えていないかもしれません）");
        return false;
    }
    scenes->ChangeScene(kGameplaySceneName);
    runner_.AddLog("    ラスボス戦へ入る");
    return true;
}

/** @brief visitChanged の手順を1フレーム進める（変えた配置物の近くへ1つずつ移り、決めた秒数だけ操作する。終わったら true） */
bool GameSmokeTest::UpdateVisit(const SmokeTestStep& step) {
    if (runner_.GetStepFrame() == 0) {
        BuildVisitTargets();
    }
    if (visitTargets_.empty()) {
        return true;
    }
    // 1つの配置物を step.frames だけ試してから、次の配置物へ移る
    const int framesPerTarget = (std::max)(1, step.frames);
    const size_t index = static_cast<size_t>(runner_.GetStepFrame() / framesPerTarget);
    if (index >= visitTargets_.size()) {
        visitLabel_.clear();
        return true;
    }
    if (runner_.GetStepFrame() % framesPerTarget == 0) {
        TeleportTo(visitTargets_[index]);
        ++visitedCount_;
    }
    runner_.UpdateRandom(step);
    const bool done = runner_.GetStepFrame() + 1 >= framesPerTarget * static_cast<int>(visitTargets_.size());
    if (done) {
        visitLabel_.clear();
    }
    return done;
}

/** @brief 今のステージで近くへ移って試す配置物を集める */
void GameSmokeTest::BuildVisitTargets() {
    visitTargets_.clear();
    visitLabel_.clear();
    const StageEditor* editor = StageEditor::GetActive();
    if (editor == nullptr || SceneManager::GetInstance()->GetCurrentSceneName() != kGameplaySceneName) {
        return;
    }
    const std::vector<std::string> names = runner_.GetChanges().ChangedObjects(editor->GetLevelPath());
    if (names.empty()) {
        runner_.AddLog("    近くで試す配置物は無い（前回から変えた配置物が無い）");
        return;
    }
    const size_t limit = static_cast<size_t>(maxVisitsPerStage_);
    editor->ForEachWorldObject([&](const ObjectDesc& desc, const Vector3& world) {
        if (visitTargets_.size() >= limit) {
            return;
        }
        const bool changed = std::find(names.begin(), names.end(), desc.name) != names.end();
        const bool skippedKind = std::find(visitSkipKinds_.begin(), visitSkipKinds_.end(), desc.kind) != visitSkipKinds_.end();
        if (changed && !skippedKind) {
            visitTargets_.push_back({ desc.name, desc.kind, world });
        }
    });
    runner_.AddLog("    前回から変えた配置物 " + std::to_string(names.size()) + " 個のうち、" + std::to_string(visitTargets_.size())
        + " 個の近くへ移って試す（上限は手順の表の maxVisitsPerStage）");
}

/** @brief プレイヤーを配置物の手前へ移す */
void GameSmokeTest::TeleportTo(const VisitTarget& target) {
    Player* player = PlayerBridge::GetInstance()->Get();
    if (player == nullptr) {
        return;
    }
    // 左の少し上へ移し、落ちて床に立ってから右の配置物へ向かって操作する
    const Vector3 current = player->GetPosition();
    const Vector3 destination = { target.position.x - visitOffset_, target.position.y + visitRaise_, current.z };
    player->SetPosition(destination);
    SmokeTestRunner::NoteTeleport();
    visitLabel_ = "近くで試す " + target.name + "（" + target.kind + "）";
    runner_.AddLog("    " + visitLabel_ + " " + FormatPosition(target.position));
    Logger::LogInfo("[SmokeTest] " + visitLabel_);
}

} // namespace engine::game

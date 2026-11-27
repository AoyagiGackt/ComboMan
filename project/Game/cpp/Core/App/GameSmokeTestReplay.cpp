/**
 * @file GameSmokeTestReplay.cpp
 * @brief 再生の照合に使うゲーム状態の指紋を作る
 */
#include "GameSmokeTest.h"
#include "EnemyEntity.h"
#include "Player.h"
#include "PlayerBridge.h"
#include "RunData.h"
#include "SceneManager.h"
#include "SmokeTestFingerprint.h"
#include "StageEditor.h"
#include <algorithm>
#include <cstdio>

namespace engine::game {
namespace {
constexpr size_t kSummaryTextCapacity = 96; // 位置を文字にする時の入れ物の大きさ

/** @brief 位置を報告用の短い文字にする */
std::string FormatPosition(const Vector3& position) {
    char text[kSummaryTextCapacity];
    std::snprintf(text, sizeof(text), "(%.2f, %.2f)", position.x, position.y);
    return text;
}
} // namespace

/** @brief 今のゲームの状態から指紋を作る（画面・体力・プレイヤーと敵の位置と体力） */
SmokeTestReplay::Checkpoint GameSmokeTest::Capture(uint64_t frame) const {
    SmokeTestReplay::Checkpoint checkpoint;
    checkpoint.frame = frame;
    SmokeTestFingerprint fingerprint;
    // 画面と、何回切り替わったか
    const SceneManager* scenes = SceneManager::GetInstance();
    fingerprint.MixText(scenes->GetCurrentSceneName());
    fingerprint.MixInt(scenes->GetSceneSwitchCount());
    checkpoint.summary = scenes->GetCurrentSceneName();
    // ランの体力と進み具合
    const RunData* runData = RunData::GetInstance();
    fingerprint.MixInt(runData->GetHp());
    fingerprint.MixInt(runData->GetFloor());
    checkpoint.summary += " 体力 " + std::to_string(runData->GetHp());
    // プレイヤーと敵は、戦う画面が落ち着いている時だけ読む（切り替えの途中やほかの画面では、もう消えた物を指していることがある）
    if (scenes->GetCurrentSceneName() != kGameplaySceneName || scenes->IsChangingScene()) {
        checkpoint.hash = fingerprint.Value();
        return checkpoint;
    }
    // プレイヤーの位置
    if (const Player* player = PlayerBridge::GetInstance()->Get()) {
        fingerprint.MixPosition(player->GetPosition());
        checkpoint.summary += " プレイヤー " + FormatPosition(player->GetPosition());
    }
    // 戦っている敵の位置と体力（名前の順に混ぜる）
    if (const StageEditor* editor = StageEditor::GetActive()) {
        std::vector<CombatEnemyRef> enemies = editor->GetCombatEnemies();
        std::sort(enemies.begin(), enemies.end(), [](const CombatEnemyRef& a, const CombatEnemyRef& b) {
            return a.name < b.name;
        });
        int alive = 0;
        for (const CombatEnemyRef& ref : enemies) {
            if (ref.enemy == nullptr) {
                continue;
            }
            fingerprint.MixText(ref.name);
            fingerprint.MixInt(ref.enemy->GetHp());
            fingerprint.MixPosition(ref.enemy->GetPosition());
            alive += ref.enemy->IsDefeated() ? 0 : 1;
        }
        checkpoint.summary += " 生きている敵 " + std::to_string(alive);
    }
    checkpoint.hash = fingerprint.Value();
    return checkpoint;
}

} // namespace engine::game

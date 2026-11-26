/**
 * @file SmokeTestReplay.cpp
 * @brief 自動テストプレイの再生ファイルの読み書きと、ゲームの状態の指紋を実装するファイル
 */
#include "SmokeTestReplay.h"
#include "EnemyEntity.h"
#include "JsonHelper.h"
#include "Player.h"
#include "PlayerBridge.h"
#include "RunData.h"
#include "SceneManager.h"
#include "StageEditor.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace engine::game {

namespace {
constexpr int kReplayVersion = 1; // 再生ファイルの書き方の番号（変えたら古いファイルは読まない）
constexpr int kJsonIndent = 1; // 再生ファイルの字下げ（人が開いても読めるように少しだけ）
constexpr float kPositionQuantize = 1000.0f; // 位置を指紋に入れる時に、この分の1の細かさで丸める
constexpr uint64_t kFnvOffset = 1469598103934665603ull; // 指紋を作る計算の始まりの値
constexpr uint64_t kFnvPrime = 1099511628211ull; // 指紋を作る計算の掛け数
constexpr size_t kSummaryTextCapacity = 96; // 位置を文字にする時の入れ物の大きさ
constexpr const char* kGameplaySceneName = "GAMEPLAY"; // プレイヤーと敵を読んでよい画面（ほかの画面では消えた物を指していることがある）

/** @brief 指紋を作る途中の値へ、バイト列を混ぜる */
void MixBytes(uint64_t& hash, const void* data, size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= kFnvPrime;
    }
}

/** @brief 指紋へ整数を混ぜる */
void MixInt(uint64_t& hash, int64_t value) {
    MixBytes(hash, &value, sizeof(value));
}

/** @brief 指紋へ文字を混ぜる */
void MixText(uint64_t& hash, const std::string& text) {
    MixBytes(hash, text.data(), text.size());
}

/** @brief 指紋へ位置を混ぜる（とても小さな差で別の指紋にならないよう丸める） */
void MixPosition(uint64_t& hash, const Vector3& position) {
    MixInt(hash, std::llround(position.x * kPositionQuantize));
    MixInt(hash, std::llround(position.y * kPositionQuantize));
    MixInt(hash, std::llround(position.z * kPositionQuantize));
}

/** @brief 位置を報告用の短い文字にする */
std::string FormatPosition(const Vector3& position) {
    char text[kSummaryTextCapacity];
    std::snprintf(text, sizeof(text), "(%.2f, %.2f)", position.x, position.y);
    return text;
}
} // namespace

/** @brief 今のゲームの状態から指紋を作る */
SmokeTestReplay::Checkpoint SmokeTestReplay::Capture(uint64_t frame) {
    Checkpoint checkpoint;
    checkpoint.frame = frame;
    uint64_t hash = kFnvOffset;
    // 画面と、何回切り替わったか
    const SceneManager* scenes = SceneManager::GetInstance();
    MixText(hash, scenes->GetCurrentSceneName());
    MixInt(hash, scenes->GetSceneSwitchCount());
    checkpoint.summary = scenes->GetCurrentSceneName();
    // ランの体力と進み具合
    const RunData* runData = RunData::GetInstance();
    MixInt(hash, runData->GetHp());
    MixInt(hash, runData->GetFloor());
    checkpoint.summary += " 体力 " + std::to_string(runData->GetHp());
    // プレイヤーと敵は、戦う画面が落ち着いている時だけ読む（切り替えの途中やほかの画面では、もう消えた物を指していることがある）
    if (scenes->GetCurrentSceneName() != kGameplaySceneName || scenes->IsChangingScene()) {
        checkpoint.hash = hash;
        return checkpoint;
    }
    // プレイヤーの位置
    if (const Player* player = PlayerBridge::GetInstance()->Get()) {
        MixPosition(hash, player->GetPosition());
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
            MixText(hash, ref.name);
            MixInt(hash, ref.enemy->GetHp());
            MixPosition(hash, ref.enemy->GetPosition());
            alive += ref.enemy->IsDefeated() ? 0 : 1;
        }
        checkpoint.summary += " 生きている敵 " + std::to_string(alive);
    }
    checkpoint.hash = hash;
    return checkpoint;
}

/** @brief 決まった間隔のフレームなら、今のゲームの状態の指紋を取って残す */
void SmokeTestReplay::RecordCheckpoint(uint64_t frame) {
    if (frame % kCheckpointIntervalFrames != 0) {
        return;
    }
    recorded_.push_back(Capture(frame));
}

/** @brief 再生ファイルを書き出す */
bool SmokeTestReplay::Save(const std::string& path, const ReplayData& data) const {
    nlohmann::json file = nlohmann::json::object();
    file["version"] = kReplayVersion;
    file["scriptPath"] = data.scriptPath;
    file["script"] = data.script;
    file["seed"] = data.seed;
    file["bugHunt"] = data.bugHunt;
    file["selection"] = data.selection;
    file["failReason"] = data.failReason;
    file["failFrame"] = data.failFrame;
    file["totalFrames"] = data.totalFrames;
    nlohmann::json issues = nlohmann::json::array();
    for (const ReplayIssue& issue : data.issues) {
        issues.push_back({ { "kind", issue.kind }, { "subject", issue.subject }, { "frame", issue.frame } });
    }
    file["issues"] = issues;
    // 指紋は フレーム・指紋・様子 の3つ組で並べる
    nlohmann::json checkpoints = nlohmann::json::array();
    for (const Checkpoint& checkpoint : recorded_) {
        checkpoints.push_back({ checkpoint.frame, checkpoint.hash, checkpoint.summary });
    }
    file["checkpoints"] = checkpoints;
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);
    std::ofstream out(path);
    if (!out) {
        return false;
    }
    out << file.dump(kJsonIndent);
    return static_cast<bool>(out);
}

/** @brief 再生ファイルを読み込み、再生の準備をする */
bool SmokeTestReplay::Load(const std::string& path) {
    const nlohmann::json file = JsonHelper::Load(path);
    if (!file.is_object() || file.value("version", 0) != kReplayVersion || !file.contains("script")) {
        return false;
    }
    loaded_ = ReplayData {};
    loaded_.scriptPath = file.value("scriptPath", std::string());
    loaded_.script = file["script"];
    loaded_.seed = file.value("seed", 0u);
    loaded_.bugHunt = file.value("bugHunt", false);
    loaded_.selection = file.value("selection", nlohmann::json::object());
    loaded_.failReason = file.value("failReason", std::string());
    loaded_.failFrame = file.value("failFrame", uint64_t { 0 });
    loaded_.totalFrames = file.value("totalFrames", uint64_t { 0 });
    for (const nlohmann::json& issue : file.value("issues", nlohmann::json::array())) {
        loaded_.issues.push_back({ issue.value("kind", std::string()), issue.value("subject", std::string()),
            issue.value("frame", uint64_t { 0 }) });
    }
    // 指紋の3つ組を読み戻す（形の違う物は飛ばす）
    expected_.clear();
    for (const nlohmann::json& checkpoint : file.value("checkpoints", nlohmann::json::array())) {
        constexpr size_t kCheckpointFields = 3; // フレーム・指紋・様子
        if (checkpoint.is_array() && checkpoint.size() == kCheckpointFields) {
            expected_.push_back({ checkpoint[0].get<uint64_t>(), checkpoint[1].get<uint64_t>(), checkpoint[2].get<std::string>() });
        }
    }
    nextExpected_ = 0;
    diverged_ = false;
    playing_ = true;
    return true;
}

/** @brief 見せたいフレーム */
uint64_t SmokeTestReplay::TargetFrame() const {
    uint64_t target = loaded_.failReason.empty() ? 0 : loaded_.failFrame;
    for (const ReplayIssue& issue : loaded_.issues) {
        if (target == 0 || issue.frame < target) {
            target = issue.frame;
        }
    }
    return target;
}

/** @brief 決まった間隔のフレームなら、今の状態の指紋を記録した物と比べる */
std::optional<std::string> SmokeTestReplay::VerifyCheckpoint(uint64_t frame) {
    if (!playing_ || diverged_ || nextExpected_ >= expected_.size() || expected_[nextExpected_].frame != frame) {
        return std::nullopt;
    }
    const Checkpoint& expected = expected_[nextExpected_++];
    const Checkpoint actual = Capture(frame);
    if (actual.hash == expected.hash) {
        return std::nullopt;
    }
    // 一度ずれたら、その後は比べても意味が無いので比べない
    diverged_ = true;
    return "frame " + std::to_string(frame) + " で記録とずれた。記録: " + expected.summary + " / 今: " + actual.summary;
}

} // namespace engine::game

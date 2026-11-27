/**
 * @file SmokeTestReplay.cpp
 * @brief 自動テストプレイの再生ファイルの読み書きと、ゲームの状態の指紋を実装するファイル
 */
#include "SmokeTestReplay.h"
#include "SmokeTestGame.h"
#include "JsonHelper.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace engine {

namespace {
constexpr int kReplayVersion = 1; // 再生ファイルの書き方の番号（変えたら古いファイルは読まない）
constexpr int kJsonIndent = 1; // 再生ファイルの字下げ（人が開いても読めるように少しだけ）
} // namespace

/** @brief 決まった間隔のフレームなら、今のゲームの状態の指紋を取って残す */
void SmokeTestReplay::RecordCheckpoint(uint64_t frame, const SmokeTestGame& game) {
    if (frame % kCheckpointIntervalFrames != 0) {
        return;
    }
    recorded_.push_back(game.Capture(frame));
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
std::optional<std::string> SmokeTestReplay::VerifyCheckpoint(uint64_t frame, const SmokeTestGame& game) {
    if (!playing_ || diverged_ || nextExpected_ >= expected_.size() || expected_[nextExpected_].frame != frame) {
        return std::nullopt;
    }
    const Checkpoint& expected = expected_[nextExpected_++];
    const Checkpoint actual = game.Capture(frame);
    if (actual.hash == expected.hash) {
        return std::nullopt;
    }
    // 一度ずれたら、その後は比べても意味が無いので比べない
    diverged_ = true;
    return "frame " + std::to_string(frame) + " で記録とずれた。記録: " + expected.summary + " / 今: " + actual.summary;
}

} // namespace engine

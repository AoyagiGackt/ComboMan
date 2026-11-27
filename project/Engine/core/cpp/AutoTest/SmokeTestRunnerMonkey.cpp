/**
 * @file SmokeTestRunnerMonkey.cpp
 * @brief 自動テストプレイのでたらめ操作（monkey の手順）を実装するファイル
 * @note 手順の表の monkeyMoves に書いた技（キーの組を順に押す並び）から、重みつきでいくつも選んでつなげて押す。
 *       ふつうに遊ぶ時には起きにくい組み合わせ（技の途中の持ち替え・回避の連打・ポーズの開け閉め など）を試すため。
 *       乱数は自動テストの種から作るので、同じ種なら同じ順に押す
 *       SmokeTestRunner のでたらめ操作だけを SmokeTestRunner.cpp から分けて置いている（クラスは SmokeTestRunner のまま）
 */
#include "SmokeTestRunner.h"
#include "EngineConstants.h"
#include "Input.h"
#include "Logger.h"
#include "TestCoverage.h"
#include <algorithm>
#include <cmath>

namespace engine {

namespace {
constexpr const char* kMonkeyCoverageCategory = "monkey_move"; // 網羅のまとめに出す、でたらめ操作の技の種類の名前
constexpr int kDefaultDownFrames = 2; // 技の1組を押しておくフレーム数の既定値
constexpr int kDefaultGapFrames = 3; // 技の組と組の間に離しておくフレーム数の既定値
constexpr float kDirectionMinSeconds = 0.5f; // 左右の向きを変えるまでの短い方の秒数
constexpr float kDirectionMaxSeconds = 2.5f; // 左右の向きを変えるまでの長い方の秒数
constexpr const char* kLeftKeyName = "A"; // 左へ歩くキー
constexpr const char* kRightKeyName = "D"; // 右へ歩くキー
constexpr int kDirectionChoices = 3; // 左右の向きの選び方の数（止まる・左・右）
} // namespace

/** @brief 手順の表の monkeyMoves を読む */
void SmokeTestRunner::LoadMonkeyMoves(const nlohmann::json& moves) {
    monkeyMoves_.clear();
    if (!moves.is_array()) {
        return;
    }
    for (const nlohmann::json& value : moves) {
        if (!value.is_object()) {
            continue;
        }
        MonkeyMove move;
        move.name = value.value("name", std::string("move"));
        move.weight = (std::max)(0.0f, value.value("weight", 1.0f));
        move.downFrames = (std::max)(1, value.value("downFrames", kDefaultDownFrames));
        move.gapFrames = (std::max)(0, value.value("gapFrames", kDefaultGapFrames));
        // 押す組を1つずつ読む（組は1つのキーの名前でもキーの名前の配列でもよい）
        for (const nlohmann::json& group : value.value("sequence", nlohmann::json::array())) {
            move.sequence.push_back(ParseKeys(group));
        }
        if (move.sequence.empty() || move.weight <= 0.0f) {
            Logger::LogWarning("[SmokeTest] 押すキーの無いでたらめ操作の技を飛ばしました: " + move.name);
            continue;
        }
        testCoverage::Declare(kMonkeyCoverageCategory, move.name);
        monkeyMoves_.push_back(std::move(move));
    }
}

/** @brief monkey で次の技を選ぶ */
void SmokeTestRunner::PickMonkeyMove(const SmokeTestStep& step) {
    monkeyMoveIndex_ = -1;
    monkeySequenceIndex_ = 0;
    monkeySequenceFrame_ = 0;
    // 手順が技の名前を挙げていれば、その技だけから選ぶ
    std::vector<int> candidates;
    std::vector<double> weights;
    for (int i = 0; i < static_cast<int>(monkeyMoves_.size()); ++i) {
        const MonkeyMove& move = monkeyMoves_[static_cast<size_t>(i)];
        const bool allowed = step.moveNames.empty()
            || std::find(step.moveNames.begin(), step.moveNames.end(), move.name) != step.moveNames.end();
        if (allowed) {
            candidates.push_back(i);
            weights.push_back(move.weight);
        }
    }
    if (candidates.empty()) {
        return;
    }
    // 重みに合わせて選ぶ
    std::discrete_distribution<size_t> pick(weights.begin(), weights.end());
    monkeyMoveIndex_ = candidates[pick(rng_)];
    testCoverage::Cover(kMonkeyCoverageCategory, monkeyMoves_[static_cast<size_t>(monkeyMoveIndex_)].name);
}

/** @brief 技を重みつきで選んで押し、左右の向きも時々変える */
void SmokeTestRunner::AdvanceMonkey(const SmokeTestStep& step) {
    // 押し続けるキー（右へ進みながら暴れる、など）は毎フレーム押す
    for (const uint8_t key : step.holdKeys) {
        InjectKey(key, true);
    }
    // 押し続けるキーが無ければ、時々 止まる・左・右 を選び直して歩き回る
    if (step.holdKeys.empty()) {
        if (--monkeyDirectionFrames_ <= 0) {
            if (monkeyDirectionKey_ != 0) {
                InjectKey(monkeyDirectionKey_, false);
            }
            const std::vector<uint8_t> left = ParseKeys(kLeftKeyName);
            const std::vector<uint8_t> right = ParseKeys(kRightKeyName);
            std::uniform_int_distribution<int> direction(0, kDirectionChoices - 1);
            const int choice = direction(rng_);
            monkeyDirectionKey_ = choice == 1 && !left.empty() ? left.front() : (choice == 2 && !right.empty() ? right.front() : uint8_t { 0 });
            std::uniform_real_distribution<float> seconds(kDirectionMinSeconds, kDirectionMaxSeconds);
            monkeyDirectionFrames_ = static_cast<int>(std::lround(seconds(rng_) / engineConstants::kFrameDeltaTime));
        }
        if (monkeyDirectionKey_ != 0) {
            InjectKey(monkeyDirectionKey_, true);
        }
    }
    // 技が終わっていれば次の技を選ぶ
    if (monkeyMoveIndex_ < 0) {
        PickMonkeyMove(step);
    }
    if (monkeyMoveIndex_ >= 0) {
        const MonkeyMove& move = monkeyMoves_[static_cast<size_t>(monkeyMoveIndex_)];
        // 今の組を downFrames の間押し、gapFrames の間離す（離す間に、押した瞬間の判定が切れる）
        const bool down = monkeySequenceFrame_ < move.downFrames;
        for (const uint8_t key : move.sequence[monkeySequenceIndex_]) {
            const bool held = std::find(step.holdKeys.begin(), step.holdKeys.end(), key) != step.holdKeys.end() || key == monkeyDirectionKey_;
            if (!held) {
                InjectKey(key, down);
            }
        }
        ++monkeySequenceFrame_;
        if (monkeySequenceFrame_ >= move.downFrames + move.gapFrames) {
            monkeySequenceFrame_ = 0;
            ++monkeySequenceIndex_;
            if (monkeySequenceIndex_ >= move.sequence.size()) {
                monkeyMoveIndex_ = -1;
            }
        }
    }
}

/** @brief monkey の手順を1フレーム進める */
bool SmokeTestRunner::UpdateMonkey(const SmokeTestStep& step) {
    AdvanceMonkey(step);
    const bool done = stepFrame_ + 1 >= step.frames;
    // 手順を終える時は、次の monkey の手順が最初から始まるように戻す
    if (done) {
        ResetMonkeyState();
    }
    return done;
}

/** @brief でたらめ操作の途中の状態を消す（次の monkey が最初から始まるように） */
void SmokeTestRunner::ResetMonkeyState() {
    monkeyMoveIndex_ = -1;
    monkeySequenceIndex_ = 0;
    monkeySequenceFrame_ = 0;
    monkeyDirectionKey_ = 0;
    monkeyDirectionFrames_ = 0;
}

} // namespace engine

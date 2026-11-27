/**
 * @file SmokeTestRunner.cpp
 * @brief 自動テストプレイの手順の読み込み・実行・おかしな所の記録・結果の書き出しを実装するファイル
 */
#include "SmokeTestRunner.h"
#include "CrashBreadcrumbs.h"
#include "DirectXCommon.h"
#include "FrameProfiler.h"
#include "EngineConstants.h"
#include "Input.h"
#include "JsonHelper.h"
#include "Logger.h"
#include "RandomSeed.h"
#include "TestCoverage.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <crtdbg.h>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <thread>

namespace engine {
namespace {
constexpr const wchar_t* kCommandLineFlag = L"--smoke-test";
constexpr const wchar_t* kBugHuntFlag = L"--bug-hunt";
constexpr const wchar_t* kScriptOption = L"--script"; // 手順の表を別の場所から読む時に、続けて場所を書く
constexpr const wchar_t* kReplayOption = L"--replay"; // 再生ファイルから同じ動きをもう一度見る時に、続けて場所を書く
constexpr const wchar_t* kAllowInputFlag = L"--allow-input"; // 自動テストの間も本物の入力を使う（同じ動きの再現はできなくなる）
constexpr float kDefaultReplayLeadSeconds = 10.0f; // 再生で、見つけた所のこれだけ手前からふつうの速さにする
constexpr float kDefaultReplayAfterSeconds = 5.0f; // 再生で、見つけた所の後これだけ見せてから終える
constexpr const wchar_t* kAllFlag = L"--all"; // 前回の記録に関係なく全部遊ぶ
constexpr const wchar_t* kRealtimeFlag = L"--realtime";
constexpr float kDefaultTimeoutSeconds = 20.0f;
constexpr float kDefaultIntervalSeconds = 0.5f;
constexpr float kDefaultSettleSeconds = 0.5f;
constexpr float kDefaultRandomHoldSeconds = 0.4f;
constexpr float kDefaultRandomChance = 0.35f;
constexpr float kHitchSeconds = 1.0f; // 画面が落ち着いている時に1フレームでこれ以上かかったら固まりかけとみなす
constexpr int kMaxRepeatDepth = 4; // 繰り返しの入れ子の深さの上限（書き間違いで無限に展開しないように）
constexpr int kMillisecondsPerSecond = 1000;
constexpr int kMaxUpdatesPerDraw = 16; // 1回描くごとに進める回数の上限（上げすぎると画面がほとんど動かず様子が見えない）

/** @brief 秒をフレーム数に直す（少なくとも1フレーム） */
int SecondsToFrames(float seconds) {
    return (std::max)(1, static_cast<int>(std::lround(seconds / engineConstants::kFrameDeltaTime)));
}

/** @brief 手順の説明（報告とパンくずに出す） */
std::string Describe(const nlohmann::json& step) {
    std::string text = step.value("action", std::string("?"));
    for (const char* key : { "scene", "key", "seconds", "text" }) {
        const auto found = step.find(key);
        if (found != step.end()) {
            text += std::string(" ") + key + "=" + (found->is_string() ? found->get<std::string>() : found->dump());
        }
    }
    for (const char* key : { "keys", "hold" }) {
        const auto found = step.find(key);
        if (found != step.end()) {
            text += std::string(" ") + key + "=" + found->dump();
        }
    }
    return text;
}

/** @brief テスト中は assert や abort のダイアログで止まらないようにする（止まると無人で回せないため。報告は CrashHandler が書く） */
void SuppressErrorDialogs() {
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG);
}

/** @brief コマンド引数の optionName の後ろに書いた値を取り出す（無ければ空。空白を含む場所は "" で囲む） */
std::string OptionValue(const std::wstring& commandLine, const wchar_t* optionName) {
    const size_t option = commandLine.find(optionName);
    if (option == std::wstring::npos) {
        return std::string();
    }
    size_t begin = commandLine.find_first_not_of(L' ', option + std::wcslen(optionName));
    if (begin == std::wstring::npos) {
        return std::string();
    }
    size_t end = std::wstring::npos;
    if (commandLine[begin] == L'"') {
        ++begin;
        end = commandLine.find(L'"', begin);
    } else {
        end = commandLine.find(L' ', begin);
    }
    const std::wstring wide = commandLine.substr(begin, end == std::wstring::npos ? std::wstring::npos : end - begin);
    if (wide.empty()) {
        return std::string();
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<size_t>((std::max)(length, 0)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), utf8.data(), length, nullptr, nullptr);
    return utf8;
}
} // namespace

/** @brief 手順の表の場所をコマンド引数から決める */
std::string SmokeTestRunner::ScriptPathFromCommandLine(const std::wstring& commandLine, const SmokeTestConfig& config) {
    if (commandLine.find(kBugHuntFlag) != std::wstring::npos) {
        const std::string custom = OptionValue(commandLine, kScriptOption);
        return custom.empty() ? config.bugHuntScriptPath : custom;
    }
    if (commandLine.find(kCommandLineFlag) != std::wstring::npos) {
        return config.defaultScriptPath;
    }
    return std::string();
}

/** @brief 自動テストで起動していれば、乱数の元の種を決める */
void SmokeTestRunner::ApplySeedFromCommandLine(const SmokeTestGame& game) {
    const std::wstring commandLine = GetCommandLineW();
    // 種を書いてあるファイル（再生ファイルか手順の表）を決める。どちらも無ければ自動テストではない
    std::string seedSource = OptionValue(commandLine, kReplayOption);
    if (seedSource.empty()) {
        seedSource = ScriptPathFromCommandLine(commandLine, game.GetConfig());
    }
    if (seedSource.empty()) {
        return;
    }
    const nlohmann::json file = JsonHelper::Load(seedSource);
    uint32_t seed = file.is_object() ? file.value("seed", 0u) : 0u;
    // 0 なら毎回変える（決めた種は報告と再生ファイルに残るので、あとで同じ動きを試せる）
    if (seed == 0) {
        seed = std::random_device {}();
    }
    // 元の種の 0 は決まっていない印なので使わない
    if (seed == 0) {
        seed = 1;
    }
    sStartSeed = seed;
    engine::randomSeed::SetBase(seed);
}

/** @brief コマンド引数に --smoke-test か --bug-hunt か --replay があれば手順を読み込んで始める */
bool SmokeTestRunner::StartIfRequested(engine::Input* input, engine::DirectXCommon* dxCommon, SmokeTestGame& game) {
    game_ = &game;
    config_ = game.GetConfig();
    const std::wstring commandLine = GetCommandLineW();
    // 再生の時は、再生ファイルに入れておいた手順の表を使う（あとで表を書き換えても同じ手順になる）
    replayPath_ = OptionValue(commandLine, kReplayOption);
    nlohmann::json script;
    if (!replayPath_.empty()) {
        if (!replay_.Load(replayPath_)) {
            Logger::LogError("[SmokeTest] 再生ファイルを読めませんでした: " + replayPath_);
            return false;
        }
        script = replay_.Data().script;
        scriptPath_ = replay_.Data().scriptPath;
    } else {
        scriptPath_ = ScriptPathFromCommandLine(commandLine, config_);
        if (scriptPath_.empty()) {
            return false;
        }
        script = JsonHelper::Load(scriptPath_);
    }
    input_ = input;
    dxCommon_ = dxCommon;
    if (!script.is_object() || !script.contains("steps")) {
        Logger::LogError("[SmokeTest] 手順のファイルを読めませんでした: " + scriptPath_);
        return false;
    }
    script_ = script;
    realtimeRequested_ = commandLine.find(kRealtimeFlag) != std::wstring::npos;
    settleFrames_ = SecondsToFrames(script.value("settleSeconds", kDefaultSettleSeconds));
    // 種は起動の最初に決めてある（ApplySeedFromCommandLine）。呼ばれていなければここで決める
    seed_ = sStartSeed != 0 ? sStartSeed : script.value("seed", 0u);
    if (seed_ == 0) {
        seed_ = std::random_device {}();
    }
    rng_.seed(seed_);
    game_->LoadSettings(script);
    // 描くのを間引いて速く回す（ゲームの時間は1回ごとに決まった長さだけ進むので、何回進めても動きは同じ）
    updatesPerDraw_ = std::clamp(script.value("speed", 1), 1, kMaxUpdatesPerDraw);
    // バグ探しの時は、前回の記録から変えた所だけを遊ぶ（再生の時は、記録した時と同じ所を回す）
    bugHunt_ = replay_.IsPlaying() ? replay_.Data().bugHunt : commandLine.find(kBugHuntFlag) != std::wstring::npos;
    if (bugHunt_) {
        PrepareChanges(commandLine, replay_.IsPlaying() ? &replay_.Data().selection : nullptr);
    }
    LoadMonkeyMoves(script.value("monkeyMoves", nlohmann::json::array()));
    steps_.clear();
    AppendSteps(script["steps"], 0);
    SuppressErrorDialogs();
    // 本物の入力を切り、人の操作が混ざらないようにする（混ざると同じ動きを再現できない）
    if (commandLine.find(kAllowInputFlag) == std::wstring::npos) {
        input_->SetPhysicalInputEnabled(false);
    }
    // 再生の時は、見つけた所の少し手前まで速回しで進める
    if (replay_.IsPlaying()) {
        replayTargetFrame_ = replay_.TargetFrame();
        replayLeadFrames_ = static_cast<uint64_t>(SecondsToFrames(script.value("replayLeadSeconds", kDefaultReplayLeadSeconds)));
        replayAfterFrames_ = static_cast<uint64_t>(SecondsToFrames(script.value("replayAfterSeconds", kDefaultReplayAfterSeconds)));
    }
    // ゲームの中身とは別の見張りを始める（決め事は手順の表で変えられる）
    SmokeTestMonitors::Settings monitorSettings;
    monitorSettings.updateSpikeMs = script.value("updateSpikeMs", monitorSettings.updateSpikeMs);
    monitorSettings.slowUpdateMs = script.value("slowUpdateMs", monitorSettings.slowUpdateMs);
    monitorSettings.memoryGrowthMb = script.value("memoryGrowthMb", monitorSettings.memoryGrowthMb);
    monitorSettings.maxScreenshots = script.value("maxScreenshots", monitorSettings.maxScreenshots);
    runTag_ = std::filesystem::path(ReportPath(replay_.IsPlaying() ? "replay_view" : "run", "txt")).stem().string();
    monitors_.Start(dxCommon_, monitorSettings, runTag_);
    sMonitors = &monitors_;
    sMainThread = std::this_thread::get_id();
    game_->OnStart();
    running_ = true;
    sActive = true;
    sIssues.clear();
    stepIndex_ = 0;
    stepFrame_ = 0;
    lastSwitchCount_ = game_->GetSceneSwitchCount();
    switchCountAtStep_ = lastSwitchCount_;
    if (replay_.IsPlaying()) {
        log_.push_back("再生: " + replayPath_ + "（frame " + std::to_string(replayTargetFrame_) + " で見つけた所を見せる）");
    }
    Logger::LogInfo("[SmokeTest] 自動テストを始めます（" + scriptPath_ + "、手順 " + std::to_string(steps_.size())
        + " 個、種 " + std::to_string(seed_) + "、速さ " + std::to_string(updatesPerDraw_) + " 倍）");
    if (bugHunt_) {
        Logger::LogInfo("[SmokeTest] 回す所: " + changes_.Summary());
        log_.push_back("回す所: " + changes_.Summary());
    }
    CrashBreadcrumbs::Add("smoke", "自動テスト開始");
    return true;
}

/** @brief キーの名前の配列を、押せるキーの番号へ直す（Esc など押せないキーは飛ばす） */
std::vector<uint8_t> SmokeTestRunner::ParseKeys(const nlohmann::json& names) {
    std::vector<uint8_t> keys;
    std::vector<std::string> keyNames;
    if (names.is_string()) {
        keyNames.push_back(names.get<std::string>());
    } else if (names.is_array()) {
        for (const nlohmann::json& name : names) {
            if (name.is_string()) {
                keyNames.push_back(name.get<std::string>());
            }
        }
    }
    for (const std::string& name : keyNames) {
        const BYTE key = engine::Input::KeyFromName(name);
        // Esc はゲームを閉じてしまうので押させない
        if (key == 0 || key == DIK_ESCAPE) {
            Logger::LogWarning("[SmokeTest] 押せないキーを飛ばしました: " + name);
            continue;
        }
        keys.push_back(key);
    }
    return keys;
}

/** @brief JSON の手順の配列を、繰り返しを展開しながら steps_ へ積む */
void SmokeTestRunner::AppendSteps(const nlohmann::json& steps, int depth) {
    if (!steps.is_array() || depth > kMaxRepeatDepth) {
        return;
    }
    for (const nlohmann::json& value : steps) {
        if (!value.is_object()) {
            continue;
        }
        const std::string action = value.value("action", std::string());
        // 繰り返しはここで展開して、実行時は平らな手順として扱う
        if (action == "repeat") {
            const int count = (std::max)(0, value.value("count", 1));
            for (int i = 0; i < count; ++i) {
                SmokeTestStep mark;
                mark.action = "mark";
                mark.raw = value;
                mark.label = "繰り返し " + std::to_string(i + 1) + "/" + std::to_string(count) + " 回目";
                steps_.push_back(mark);
                AppendSteps(value.value("steps", nlohmann::json::array()), depth + 1);
            }
            continue;
        }
        SmokeTestStep step;
        step.action = action;
        step.label = Describe(value);
        step.scene = value.value("scene", std::string());
        step.frames = SecondsToFrames(value.value("seconds", 0.0f));
        step.timeoutFrames = SecondsToFrames(value.value("timeout", kDefaultTimeoutSeconds));
        const float defaultInterval = action == "random" ? kDefaultRandomHoldSeconds : kDefaultIntervalSeconds;
        step.intervalFrames = SecondsToFrames(value.value("interval", defaultInterval));
        step.chance = value.value("chance", kDefaultRandomChance);
        step.expectProgress = value.value("expectProgress", false);
        step.sameStage = value.value("sameStage", false);
        step.raw = value;
        step.moveNames = value.value("moves", std::vector<std::string>());
        if (value.contains("key")) {
            step.keys = ParseKeys(value["key"]);
        }
        if (value.contains("keys")) {
            const std::vector<uint8_t> more = ParseKeys(value["keys"]);
            step.keys.insert(step.keys.end(), more.begin(), more.end());
        }
        if (value.contains("hold")) {
            step.holdKeys = ParseKeys(value["hold"]);
        }
        if (action == "mark") {
            step.label = value.value("text", std::string("目印"));
        }
        steps_.push_back(std::move(step));
    }
}

/** @brief 手順を1フレーム進める */
bool SmokeTestRunner::Update() {
    if (!running_) {
        return false;
    }
    if (finished_) {
        return true;
    }
    ++totalFrames_;
    sFrame = totalFrames_;
    // 画面が切り替わったら、落ち着くまでの数え直しを始める
    const uint32_t switchCount = game_->GetSceneSwitchCount();
    if (switchCount != lastSwitchCount_) {
        lastSwitchCount_ = switchCount;
        framesSinceSwitch_ = 0;
    } else {
        ++framesSinceSwitch_;
    }
    // 1回の更新の重さを数え、プールが溢れていないかを見る
    const bool settled = !game_->IsChangingScene() && framesSinceSwitch_ >= settleFrames_;
    monitors_.EndUpdate(settled, game_->GetCurrentSceneName());
    monitors_.CheckPools(totalFrames_);
    // 状態の指紋: 再生の時は記録と比べ、そうでなければ残す
    if (replay_.IsPlaying()) {
        if (const std::optional<std::string> divergence = replay_.VerifyCheckpoint(totalFrames_, *game_)) {
            ReportIssue("再現のずれ", "状態の指紋", *divergence);
        }
    } else {
        replay_.RecordCheckpoint(totalFrames_, *game_);
    }
    if (UpdateReplay()) {
        return true;
    }
    CheckFrameHitch();
    game_->OnFrame();
    // 手順で入ったステージが落ち着いたら、その時の切り替え回数を覚える（以後に変わったらステージを出たとみなす）
    if (pendingStageEntry_ && IsSettledOn(config_.stageScene)) {
        pendingStageEntry_ = false;
        inStage_ = true;
        stageSwitchCount_ = switchCount;
        // 入るたびにメモリの量を測り、出入りで増え続けていないかを見る
        monitors_.OnStageEntered(game_->GetStageLabel());
    }

    if (stepIndex_ >= steps_.size()) {
        ReleaseKeys();
        sExpectProgress = false;
        Finish();
        WriteReport(true, "");
        return true;
    }
    game_->BeforeAction();
    const SmokeTestStep& step = steps_[stepIndex_];
    if (stepFrame_ == 0) {
        switchCountAtStep_ = switchCount;
        sExpectProgress = step.expectProgress;
        CrashBreadcrumbs::Add("smoke", "手順 " + std::to_string(stepIndex_ + 1) + ": " + step.label);
    }
    sContext = "手順 " + std::to_string(stepIndex_ + 1) + "（" + step.label + "）画面 " + game_->GetCurrentSceneName()
        + game_->GetContextSuffix(step);
    // ステージの中だけの手順は、自分でクリアした・やられてリトライした等で画面が変わっていたら飛ばして次のステージへ進む
    // 変えていないステージを飛ばしている間は、ステージに入るのを待つ手順も飛ばす
    const bool skippedStage = skippingStage_ && (step.sameStage || (step.action == "waitScene" && step.scene == config_.stageScene));
    const bool skipped = skippedStage || (step.sameStage && HasLeftStage());
    const bool done = skipped || UpdateStep(step);
    game_->AfterAction(step);
    if (finished_) {
        return true;
    }
    ++stepFrame_;
    if (done) {
        log_.push_back((skipped ? "--  手順 " : "OK  手順 ") + std::to_string(stepIndex_ + 1) + " (" + std::to_string(stepFrame_) + " フレーム): " + step.label
            + (skippedStage ? "（変えていないステージなので飛ばした）" : (skipped ? "（ステージから画面が変わったので飛ばした）" : "")));
        ReleaseKeys();
        sExpectProgress = false;
        game_->OnStepFinished();
        ++stepIndex_;
        stepFrame_ = 0;
    }
    return false;
}

/** @brief 今の手順を1フレーム進める（終わったら true） */
bool SmokeTestRunner::UpdateStep(const SmokeTestStep& step) {
    const auto timedOut = [&]() {
        if (stepFrame_ < step.timeoutFrames) {
            return false;
        }
        Fail("止まりました: " + step.label + "（" + std::to_string(step.timeoutFrames) + " フレーム待っても進まない。今の画面は "
            + game_->GetCurrentSceneName() + "）");
        return true;
    };

    if (step.action == "waitScene") {
        if (IsSettledOn(step.scene)) {
            return true;
        }
        timedOut();
        return false;
    }
    if (step.action == "waitSettled") {
        // どの画面でもよいので、切り替えが落ち着くまで待つ
        if (!game_->IsChangingScene() && framesSinceSwitch_ >= settleFrames_) {
            return true;
        }
        timedOut();
        return false;
    }
    if (step.action == "waitSceneChange") {
        const bool changed = game_->GetSceneSwitchCount() != switchCountAtStep_
            && framesSinceSwitch_ >= settleFrames_ && !game_->IsChangingScene();
        if (changed) {
            return true;
        }
        timedOut();
        return false;
    }
    if (step.action == "pressUntilScene") {
        if (IsSettledOn(step.scene)) {
            return true;
        }
        // 間隔ごとに1フレームだけ押して、押した瞬間の判定を毎回起こす
        const bool press = stepFrame_ % step.intervalFrames == 0;
        for (const uint8_t key : step.keys) {
            InjectKey(key, press);
        }
        timedOut();
        return false;
    }
    if (step.action == "press") {
        // 1フレーム目に押して2フレーム目に離す（押した瞬間の判定が1回だけ起きる）
        for (const uint8_t key : step.keys) {
            InjectKey(key, stepFrame_ == 0);
        }
        return stepFrame_ >= 1;
    }
    if (step.action == "hold") {
        for (const uint8_t key : step.keys) {
            InjectKey(key, true);
        }
        return stepFrame_ + 1 >= step.frames;
    }
    if (step.action == "random") {
        return UpdateRandom(step);
    }
    if (step.action == "monkey") {
        return UpdateMonkey(step);
    }
    if (step.action == "wait") {
        return stepFrame_ + 1 >= step.frames;
    }
    if (step.action == "changeScene") {
        game_->ChangeScene(step.scene);
        return true;
    }
    if (step.action == "mark") {
        Logger::LogInfo("[SmokeTest] " + step.label);
        return true;
    }
    bool done = false;
    if (game_->UpdateAction(step, done)) {
        return done;
    }
    Fail("知らない手順です: " + step.label + "（手順の表の action を確認してください）");
    return false;
}

/** @brief random の手順を1フレーム進める（終わったら true） */
bool SmokeTestRunner::UpdateRandom(const SmokeTestStep& step) {
    // 押し続けるキー（右へ歩くなど）は毎フレーム押す
    for (const uint8_t key : step.holdKeys) {
        InjectKey(key, true);
    }
    // 一定間隔で、候補のキーからいくつかを選んで押し直す（同時押しや連打の組み合わせを試す）
    if (stepFrame_ % step.intervalFrames == 0 && !step.keys.empty()) {
        std::bernoulli_distribution pressed(std::clamp(step.chance, 0.0f, 1.0f));
        for (const uint8_t key : step.keys) {
            // 押し続けるキーと同じキーは離さない
            if (std::find(step.holdKeys.begin(), step.holdKeys.end(), key) != step.holdKeys.end()) {
                continue;
            }
            InjectKey(key, pressed(rng_));
        }
    }
    return stepFrame_ + 1 >= step.frames;
}

/** @brief 前のフレームから実時間でかかりすぎていたら、固まりかけとして記録する */
void SmokeTestRunner::CheckFrameHitch() {
    const auto now = std::chrono::steady_clock::now();
    const bool hadLast = hasLastUpdateTime_;
    const auto last = lastUpdateTime_;
    lastUpdateTime_ = now;
    hasLastUpdateTime_ = true;
    // 画面の切り替え中と直後は読み込みで時間がかかって当然なので数えない
    if (!hadLast || game_->IsChangingScene() || framesSinceSwitch_ < settleFrames_) {
        return;
    }
    const float seconds = std::chrono::duration<float>(now - last).count();
    if (seconds >= kHitchSeconds) {
        ReportIssue("固まりかけ", game_->GetCurrentSceneName(),
            "1フレームに " + std::to_string(static_cast<int>(seconds * kMillisecondsPerSecond)) + " ミリ秒かかった" + DescribeFramePhases());
    }
}

/** @brief どの段階で時間がかかったかの文（前のフレームの更新・描画と、このフレームの始まりからの時間） */
std::string SmokeTestRunner::DescribeFramePhases() {
    using engine::graphics::FramePhase;
    const auto* profiler = engine::graphics::FrameProfiler::GetInstance();
    const auto ms = [](float value) {
        return std::to_string(static_cast<int>(value));
    };
    return "（前のフレーム: 更新 " + ms(profiler->GetLastPhaseMs(FramePhase::kUpdate))
        + " ・描画 " + ms(profiler->GetLastPhaseMs(FramePhase::kDraw))
        + " のうち GPU 待ち " + ms(profiler->GetLastPhaseMs(FramePhase::kGpuWait))
        + " ・画面へ出す " + ms(profiler->GetLastPhaseMs(FramePhase::kPresent))
        + "、このフレームの始まりから " + ms(profiler->GetMsSinceFrameStart()) + " ミリ秒）";
}

/** @brief 入ったステージから画面が変わっていたら true（sameStage の手順を飛ばす目安） */
bool SmokeTestRunner::HasLeftStage() const {
    return !inStage_ || game_->GetSceneSwitchCount() != stageSwitchCount_;
}

/** @brief 遊んでいる間に見つけたおかしな所を記録する */
void SmokeTestRunner::ReportIssue(const std::string& kind, const std::string& subject, const std::string& detail) {
    if (!sActive) {
        return;
    }
    const std::lock_guard<std::mutex> lock(sIssueMutex);
    for (Issue& issue : sIssues) {
        if (issue.kind == kind && issue.subject == subject) {
            ++issue.count;
            return;
        }
    }
    Issue issue;
    issue.kind = kind;
    issue.subject = subject;
    issue.detail = detail;
    issue.context = sContext;
    issue.firstFrame = sFrame;
    issue.count = 1;
    // 初めて見つけた時の画面を保存する（描画を記録するスレッドから届いた時は、画面の保存を頼めないので飛ばす）
    if (sMonitors != nullptr && std::this_thread::get_id() == sMainThread) {
        issue.screenshot = sMonitors->RequestScreenshot(sIssues.size() + 1);
    }
    sIssues.push_back(std::move(issue));
    Logger::LogWarning("[SmokeTest] おかしな所: " + kind + " " + subject + ": " + detail);
    CrashBreadcrumbs::Add("smoke", "おかしな所: " + kind + " " + subject);
}

/** @brief 今の画面が scene で、切り替えも落ち着いているか */
bool SmokeTestRunner::IsSettledOn(const std::string& scene) const {
    return game_->GetCurrentSceneName() == scene && !game_->IsChangingScene() && framesSinceSwitch_ >= settleFrames_;
}

/** @brief 押したことにしているキーを全部離す */
void SmokeTestRunner::ReleaseKeys() {
    if (input_ != nullptr) {
        input_->ClearInjectedKeys();
    }
}

/** @brief 失敗として記録して終える */
void SmokeTestRunner::Fail(const std::string& reason) {
    ReleaseKeys();
    sExpectProgress = false;
    log_.push_back("NG  手順 " + std::to_string(stepIndex_ + 1) + ": " + reason);
    Finish();
    WriteReport(false, reason);
}

/** @brief 前回の記録と比べて、遊ぶステージと近くで試す配置物を決める（--bug-hunt の時だけ） */
void SmokeTestRunner::PrepareChanges(const std::wstring& commandLine, const nlohmann::json* importedSelection) {
    const auto stages = game_->GetStages();
    // 再生の時は、記録した時と同じステージと配置物を回す
    if (importedSelection != nullptr) {
        changes_.ImportSelection(stages, *importedSelection);
        return;
    }
    changes_.Prepare(stages, commandLine.find(kAllFlag) != std::wstring::npos, scriptPath_);
}

/** @brief 残りの手順を飛ばし、成功として終える（変えた所を全部回り終えた時） */
void SmokeTestRunner::FinishEarly(const std::string& reason) {
    ReleaseKeys();
    sExpectProgress = false;
    log_.push_back("終わり: " + reason);
    Finish();
    WriteReport(true, "");
}

/** @brief 指定したキーを押すか離す */
void SmokeTestRunner::InjectKey(uint8_t key, bool pressed) {
    input_->InjectKey(key, pressed);
}

} // namespace engine

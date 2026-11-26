/**
 * @file SmokeTestRunner.cpp
 * @brief 自動テストプレイの手順の読み込み・実行・おかしな所の記録・結果の書き出しを実装するファイル
 */
#include "SmokeTestRunner.h"
#include "CrashBreadcrumbs.h"
#include "DirectXCommon.h"
#include "FrameProfiler.h"
#include "GameConstants.h"
#include "GameRules.h"
#include "Input.h"
#include "JsonHelper.h"
#include "Logger.h"
#include "MapScene.h"
#include "WeaponManager.h"
#include "RandomSeed.h"
#include "RunData.h"
#include "SaveData.h"
#include "SceneManager.h"
#include "ScoreManager.h"
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

namespace engine::game {
namespace {
constexpr const wchar_t* kCommandLineFlag = L"--smoke-test";
constexpr const wchar_t* kBugHuntFlag = L"--bug-hunt";
constexpr const char* kDefaultScriptPath = "Resources/Config/smoke_test.json";
constexpr const char* kBugHuntScriptPath = "BugHunt/bug_hunt.json"; // バグ探しの手順は BugHunt プロジェクトのフォルダに置く
constexpr const wchar_t* kScriptOption = L"--script"; // 手順の表を別の場所から読む時に、続けて場所を書く
constexpr const wchar_t* kReplayOption = L"--replay"; // 再生ファイルから同じ動きをもう一度見る時に、続けて場所を書く
constexpr const char* kNextWeaponName = "next"; // grantBossWeapon の weapon にこう書くと、一覧の順に次の種類を渡す
constexpr const wchar_t* kAllowInputFlag = L"--allow-input"; // 自動テストの間も本物の入力を使う（同じ動きの再現はできなくなる）
constexpr float kDefaultReplayLeadSeconds = 10.0f; // 再生で、見つけた所のこれだけ手前からふつうの速さにする
constexpr float kDefaultReplayAfterSeconds = 5.0f; // 再生で、見つけた所の後これだけ見せてから終える
constexpr const char* kGameplaySceneName = "GAMEPLAY";
constexpr uint64_t kChoiceAnswerIntervalFrames = 90; // 選ぶ画面が出ている時に決定を押す間隔（約1.5秒。見張りの15秒より十分短く）
constexpr float kDefaultTimeoutSeconds = 20.0f;
constexpr float kDefaultIntervalSeconds = 0.5f;
constexpr float kDefaultSettleSeconds = 0.5f;
constexpr float kDefaultRandomHoldSeconds = 0.4f;
constexpr float kDefaultRandomChance = 0.35f;
constexpr float kHitchSeconds = 1.0f; // 画面が落ち着いている時に1フレームでこれ以上かかったら固まりかけとみなす
constexpr int kMaxRepeatDepth = 4; // 繰り返しの入れ子の深さの上限（書き間違いで無限に展開しないように）
constexpr int kMillisecondsPerSecond = 1000;
constexpr int kMaxUpdatesPerDraw = 16; // 1回描くごとに進める回数の上限（上げすぎると画面がほとんど動かず様子が見えない）
constexpr int kKeepAliveHpDivisor = 2; // keepPlayerAlive の時、体力がこの分の1を切ったら全快させる
constexpr int kDefaultMaxVisitsPerStage = 12; // 1つのステージで近くへ移って試す配置物の数の上限の既定値
constexpr float kDefaultVisitOffset = 1.5f; // 配置物からどれだけ左に移るかの既定値
constexpr float kDefaultVisitRaise = 1.0f; // 配置物からどれだけ上に移るかの既定値（落ちて床に立つ）

/** @brief 秒をフレーム数に直す（少なくとも1フレーム） */
int SecondsToFrames(float seconds) {
    return (std::max)(1, static_cast<int>(std::lround(seconds / gameConstants::kFrameDeltaTime)));
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
std::string SmokeTestRunner::ScriptPathFromCommandLine(const std::wstring& commandLine) {
    if (commandLine.find(kBugHuntFlag) != std::wstring::npos) {
        const std::string custom = OptionValue(commandLine, kScriptOption);
        return custom.empty() ? std::string(kBugHuntScriptPath) : custom;
    }
    if (commandLine.find(kCommandLineFlag) != std::wstring::npos) {
        return kDefaultScriptPath;
    }
    return std::string();
}

/** @brief 自動テストで起動していれば、乱数の元の種を決める */
void SmokeTestRunner::ApplySeedFromCommandLine() {
    const std::wstring commandLine = GetCommandLineW();
    // 種を書いてあるファイル（再生ファイルか手順の表）を決める。どちらも無ければ自動テストではない
    std::string seedSource = OptionValue(commandLine, kReplayOption);
    if (seedSource.empty()) {
        seedSource = ScriptPathFromCommandLine(commandLine);
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
bool SmokeTestRunner::StartIfRequested(engine::Input* input, engine::DirectXCommon* dxCommon) {
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
        scriptPath_ = ScriptPathFromCommandLine(commandLine);
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
    settleFrames_ = SecondsToFrames(script.value("settleSeconds", kDefaultSettleSeconds));
    // 種は起動の最初に決めてある（ApplySeedFromCommandLine）。呼ばれていなければここで決める
    seed_ = sStartSeed != 0 ? sStartSeed : script.value("seed", 0u);
    if (seed_ == 0) {
        seed_ = std::random_device {}();
    }
    rng_.seed(seed_);
    keepPlayerAlive_ = script.value("keepPlayerAlive", false);
    // 描くのを間引いて速く回す（ゲームの時間は1回ごとに決まった長さだけ進むので、何回進めても動きは同じ）
    updatesPerDraw_ = std::clamp(script.value("speed", 1), 1, kMaxUpdatesPerDraw);
    maxVisitsPerStage_ = (std::max)(0, script.value("maxVisitsPerStage", kDefaultMaxVisitsPerStage));
    visitOffset_ = script.value("visitOffset", kDefaultVisitOffset);
    visitRaise_ = script.value("visitRaise", kDefaultVisitRaise);
    visitSkipKinds_.clear();
    for (const nlohmann::json& kind : script.value("visitSkipKinds", nlohmann::json::array())) {
        if (kind.is_string()) {
            visitSkipKinds_.push_back(kind.get<std::string>());
        }
    }
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
    // NEW GAME やクリアで書き換わるので、テスト中はプレイヤーのセーブとランキングをファイルへ書かない
    SaveDataManager::GetInstance()->SetWritesEnabled(false);
    ScoreManager::GetInstance()->SetWritesEnabled(false);
    running_ = true;
    sActive = true;
    sIssues.clear();
    stepIndex_ = 0;
    stepFrame_ = 0;
    lastSwitchCount_ = SceneManager::GetInstance()->GetSceneSwitchCount();
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
                Step mark;
                mark.action = "mark";
                mark.label = "繰り返し " + std::to_string(i + 1) + "/" + std::to_string(count) + " 回目";
                steps_.push_back(mark);
                AppendSteps(value.value("steps", nlohmann::json::array()), depth + 1);
            }
            continue;
        }
        Step step;
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
        step.weapon = value.value("weapon", std::string());
        step.moveNames = value.value("moves", std::vector<std::string>());
        step.makeMain = value.value("makeMain", false);
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
    const uint32_t switchCount = SceneManager::GetInstance()->GetSceneSwitchCount();
    if (switchCount != lastSwitchCount_) {
        lastSwitchCount_ = switchCount;
        framesSinceSwitch_ = 0;
    } else {
        ++framesSinceSwitch_;
    }
    // 1回の更新の重さを数え、プールが溢れていないかを見る
    const SceneManager* scenes = SceneManager::GetInstance();
    const bool settled = !scenes->IsChangingScene() && framesSinceSwitch_ >= settleFrames_;
    monitors_.EndUpdate(settled, scenes->GetCurrentSceneName());
    monitors_.CheckPools(totalFrames_);
    // 状態の指紋: 再生の時は記録と比べ、そうでなければ残す
    if (replay_.IsPlaying()) {
        if (const std::optional<std::string> divergence = replay_.VerifyCheckpoint(totalFrames_)) {
            ReportIssue("再現のずれ", "状態の指紋", *divergence);
        }
    } else {
        replay_.RecordCheckpoint(totalFrames_);
    }
    if (UpdateReplay()) {
        return true;
    }
    CheckFrameHitch();
    KeepPlayerAlive();
    // enterNextStage で入ったステージが落ち着いたら、その時の切り替え回数を覚える（以後に変わったらステージを出たとみなす）
    if (pendingStageEntry_ && IsSettledOn(kGameplaySceneName)) {
        pendingStageEntry_ = false;
        inStage_ = true;
        stageSwitchCount_ = switchCount;
        // 入るたびにメモリの量を測り、出入りで増え続けていないかを見る
        monitors_.OnStageEntered(RunData::GetInstance()->IsTutorialActive()
                ? std::string("チュートリアル")
                : "ステージ " + std::to_string(RunData::GetInstance()->GetPlayFloor() + 1));
    }

    if (stepIndex_ >= steps_.size()) {
        ReleaseKeys();
        sExpectProgress = false;
        Finish();
        WriteReport(true, "");
        return true;
    }
    AnswerWeaponChoice();
    const Step& step = steps_[stepIndex_];
    if (stepFrame_ == 0) {
        switchCountAtStep_ = switchCount;
        sExpectProgress = step.expectProgress;
        CrashBreadcrumbs::Add("smoke", "手順 " + std::to_string(stepIndex_ + 1) + ": " + step.label);
    }
    sContext = "手順 " + std::to_string(stepIndex_ + 1) + "（" + step.label + "）画面 " + SceneManager::GetInstance()->GetCurrentSceneName()
        + (RunData::GetInstance()->IsTutorialActive() ? std::string(" チュートリアル")
                                                       : " ステージ " + std::to_string(RunData::GetInstance()->GetPlayFloor() + 1));
    if (step.action == "visitChanged" && !visitLabel_.empty()) {
        sContext += " " + visitLabel_;
    }
    // ステージの中だけの手順は、自分でクリアした・やられてリトライした等で画面が変わっていたら飛ばして次のステージへ進む
    // 変えていないステージを飛ばしている間は、ステージに入るのを待つ手順も飛ばす
    const bool skippedStage = skippingStage_ && (step.sameStage || (step.action == "waitScene" && step.scene == kGameplaySceneName));
    const bool skipped = skippedStage || (step.sameStage && HasLeftStage());
    const bool done = skipped || UpdateStep(step);
    if (finished_) {
        return true;
    }
    ++stepFrame_;
    if (done) {
        log_.push_back((skipped ? "--  手順 " : "OK  手順 ") + std::to_string(stepIndex_ + 1) + " (" + std::to_string(stepFrame_) + " フレーム): " + step.label
            + (skippedStage ? "（変えていないステージなので飛ばした）" : (skipped ? "（ステージから画面が変わったので飛ばした）" : "")));
        ReleaseKeys();
        sExpectProgress = false;
        ++stepIndex_;
        stepFrame_ = 0;
    }
    return false;
}

/** @brief 今の手順を1フレーム進める（終わったら true） */
bool SmokeTestRunner::UpdateStep(const Step& step) {
    SceneManager* scenes = SceneManager::GetInstance();
    const auto timedOut = [&]() {
        if (stepFrame_ < step.timeoutFrames) {
            return false;
        }
        Fail("止まりました: " + step.label + "（" + std::to_string(step.timeoutFrames) + " フレーム待っても進まない。今の画面は "
            + scenes->GetCurrentSceneName() + "）");
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
        if (!scenes->IsChangingScene() && framesSinceSwitch_ >= settleFrames_) {
            return true;
        }
        timedOut();
        return false;
    }
    if (step.action == "waitSceneChange") {
        const bool changed = scenes->GetSceneSwitchCount() != switchCountAtStep_
            && framesSinceSwitch_ >= settleFrames_ && !scenes->IsChangingScene();
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
            input_->InjectKey(key, press);
        }
        timedOut();
        return false;
    }
    if (step.action == "press") {
        // 1フレーム目に押して2フレーム目に離す（押した瞬間の判定が1回だけ起きる）
        for (const uint8_t key : step.keys) {
            input_->InjectKey(key, stepFrame_ == 0);
        }
        return stepFrame_ >= 1;
    }
    if (step.action == "hold") {
        for (const uint8_t key : step.keys) {
            input_->InjectKey(key, true);
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
        scenes->ChangeScene(step.scene);
        return true;
    }
    if (step.action == "enterNextStage") {
        return EnterNextStage();
    }
    if (step.action == "enterTutorial") {
        return EnterTutorial();
    }
    if (step.action == "enterFinalBoss") {
        return EnterFinalBoss(step);
    }
    if (step.action == "visitChanged") {
        return UpdateVisit(step);
    }
    if (step.action == "grantBossWeapon") {
        return GrantBossWeapon(step.weapon, step.makeMain);
    }
    if (step.action == "resetTrainingWeapons" || step.action == "visitTrainingPickup"
        || step.action == "expectWeapon" || step.action == "expectDuplicateWeapon" || step.action == "checkLastFusion") {
        return CheckTrainingWeapon(step);
    }
    if (step.action == "mark") {
        Logger::LogInfo("[SmokeTest] " + step.label);
        return true;
    }
    Fail("知らない手順です: " + step.label + "（手順の表の action を確認してください）");
    return false;
}

/** @brief random の手順を1フレーム進める（終わったら true） */
bool SmokeTestRunner::UpdateRandom(const Step& step) {
    // 押し続けるキー（右へ歩くなど）は毎フレーム押す
    for (const uint8_t key : step.holdKeys) {
        input_->InjectKey(key, true);
    }
    // 一定間隔で、候補のキーからいくつかを選んで押し直す（同時押しや連打の組み合わせを試す）
    if (stepFrame_ % step.intervalFrames == 0 && !step.keys.empty()) {
        std::bernoulli_distribution pressed(std::clamp(step.chance, 0.0f, 1.0f));
        for (const uint8_t key : step.keys) {
            // 押し続けるキーと同じキーは離さない
            if (std::find(step.holdKeys.begin(), step.holdKeys.end(), key) != step.holdKeys.end()) {
                continue;
            }
            input_->InjectKey(key, pressed(rng_));
        }
    }
    return stepFrame_ + 1 >= step.frames;
}

/** @brief 今のランで次に遊ぶステージ（最後のステージの後はラスボス戦）へ直接入る */
bool SmokeTestRunner::EnterNextStage() {
    RunData* runData = RunData::GetInstance();
    if (!runData->IsRunActive()) {
        Fail("enterNextStage: ランが始まっていません（先にタイトルから NEW GAME してください）");
        return false;
    }
    const int stageCount = static_cast<int>(GameRules::GetInstance()->Get().levelPaths.size());
    const int floor = runData->GetFloor();
    if (floor >= stageCount) {
        // 全部のステージを終えた後は、マップと同じくラスボス戦へ入る
        if (!runData->IsFinalBossPending()) {
            Fail("enterNextStage: 全部のステージを終えていて、ラスボス戦も残っていません");
            return false;
        }
        SceneManager::GetInstance()->ChangeScene(kGameplaySceneName);
        pendingStageEntry_ = true;
        inStage_ = false;
        return true;
    }
    // 前回から変えていないステージは遊ばずに、クリアした扱いで進める
    if (!changes_.IsStageSelected(GameRules::GetInstance()->LevelPathForFloor(floor, std::string()))) {
        SkipFloor(floor);
        return true;
    }
    skippingStage_ = false;
    // マップと同じく、最初は通常・途中は強敵・最後はボス扱いのマスとして入る
    const int lastFloor = stageCount - 1;
    const RunData::NodeType node = floor == lastFloor ? RunData::NodeType::kBoss
        : floor == 0                                  ? RunData::NodeType::kCombat
                                                      : RunData::NodeType::kElite;
    runData->SetCurrentNode(node);
    runData->SetPlayFloor(floor);
    SceneManager::GetInstance()->ChangeScene(kGameplaySceneName);
    pendingStageEntry_ = true;
    inStage_ = false;
    log_.push_back("    ステージ " + std::to_string(floor + 1) + " へ入る");
    return true;
}

/** @brief マップの入口と同じ準備をしてチュートリアルのステージへ直接入る（クリアやポーズで抜けるとマップへ戻る） */
bool SmokeTestRunner::EnterTutorial() {
    if (!RunData::GetInstance()->IsRunActive()) {
        Fail("enterTutorial: ランが始まっていません（先にタイトルから NEW GAME してください）");
        return false;
    }
    // チュートリアルを置いていない設定なら、飛ばして次の手順へ進む
    if (GameRules::GetInstance()->Get().tutorialLevelPath.empty()) {
        log_.push_back("    チュートリアルが無いので飛ばす");
        return true;
    }
    // 前回から変えていなければ入らずに、チュートリアルの中の手順を飛ばす
    if (!changes_.IsStageSelected(GameRules::GetInstance()->Get().tutorialLevelPath)) {
        log_.push_back("    チュートリアルは前回から変えていないので飛ばす");
        skippingStage_ = true;
        inStage_ = false;
        pendingStageEntry_ = false;
        return true;
    }
    skippingStage_ = false;
    MapScene::BeginTutorial();
    SceneManager::GetInstance()->ChangeScene(kGameplaySceneName);
    pendingStageEntry_ = true;
    inStage_ = false;
    log_.push_back("    チュートリアルへ入る");
    return true;
}

/** @brief ボスを倒さずにボスの武器を渡す（ボスの枠が埋まっていれば、主軸を選ぶ画面が出る） */
bool SmokeTestRunner::GrantBossWeapon(const std::string& weaponName, bool makeMain) {
    WeaponManager* weapons = WeaponManager::GetInstance();
    const auto& list = weapons->GetList();
    if (list.empty()) {
        Fail("grantBossWeapon: 武器の一覧が空です");
        return false;
    }
    // 名前が無ければ、今のボスの枠の主軸とも相棒とも違う種類を、一覧の順に選ぶ（主軸を選ぶ画面を通すため）
    WeaponType type = list.front().type;
    if (weaponName == kNextWeaponName) {
        // 一覧の順に1つずつ次の種類を渡す（今の主軸と同じならもう1つ先へ。どの武器のボスのコンボも順に試せる）
        const int main = weapons->GetSlotWeaponIndex(WeaponManager::kBossSlot);
        size_t index = grantRotation_++ % list.size();
        if (static_cast<int>(index) == main) {
            index = grantRotation_++ % list.size();
        }
        type = list[index].type;
    } else if (!weaponName.empty()) {
        type = ParseWeaponTypeName(weaponName);
    } else {
        const int main = weapons->GetSlotWeaponIndex(WeaponManager::kBossSlot);
        const int partner = weapons->GetFusedWeaponIndex(WeaponManager::kBossSlot);
        for (int i = 0; i < static_cast<int>(list.size()); ++i) {
            if (i != main && i != partner) {
                type = list[static_cast<size_t>(i)].type;
                break;
            }
        }
    }
    const WeaponManager::BossAcquireResult result = weapons->AcquireBossWeapon(type);
    // 頼まれていれば、主軸を選ぶ画面を通さずに渡した武器を主軸にする（前の主軸は相棒になる）
    const bool chose = makeMain && weapons->HasPendingBossChoice();
    if (chose) {
        weapons->ChooseBossMain(false);
    }
    log_.push_back("    ボスの武器を渡す（" + std::string(WeaponTypeName(type)) + "、結果 " + std::to_string(static_cast<int>(result))
        + (chose ? "、主軸にした）" : "）"));
    return true;
}

/** @brief 武器の交換や主軸を選ぶ画面が出ていたら、決まった間隔で決定（交換は1つ目の枠）を押して閉じる */
void SmokeTestRunner::AnswerWeaponChoice() {
    const WeaponManager* weapons = WeaponManager::GetInstance();
    const bool bossChoice = weapons->HasPendingBossChoice();
    const bool exchange = weapons->HasPendingWeapon();
    if (!bossChoice && !exchange) {
        return;
    }
    // 間隔の最初のフレームに押して、次のフレームに離す（押した瞬間の判定が1回だけ起きる）
    const uint64_t phase = totalFrames_ % kChoiceAnswerIntervalFrames;
    const uint8_t key = bossChoice ? static_cast<uint8_t>(DIK_RETURN) : static_cast<uint8_t>(DIK_1);
    if (phase <= 1) {
        input_->InjectKey(key, phase == 0);
    }
}

/** @brief 前のフレームから実時間でかかりすぎていたら、固まりかけとして記録する */
void SmokeTestRunner::CheckFrameHitch() {
    const auto now = std::chrono::steady_clock::now();
    const bool hadLast = hasLastUpdateTime_;
    const auto last = lastUpdateTime_;
    lastUpdateTime_ = now;
    hasLastUpdateTime_ = true;
    // 画面の切り替え中と直後は読み込みで時間がかかって当然なので数えない
    if (!hadLast || SceneManager::GetInstance()->IsChangingScene() || framesSinceSwitch_ < settleFrames_) {
        return;
    }
    const float seconds = std::chrono::duration<float>(now - last).count();
    if (seconds >= kHitchSeconds) {
        ReportIssue("固まりかけ", SceneManager::GetInstance()->GetCurrentSceneName(),
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

/** @brief keepPlayerAlive の時、体力が減りすぎたら全快させて最後まで回れるようにする（ダメージ自体は受ける） */
void SmokeTestRunner::KeepPlayerAlive() {
    RunData* runData = RunData::GetInstance();
    if (!keepPlayerAlive_ || !runData->IsRunActive()) {
        return;
    }
    // 大きな一撃で一気に0にならないよう、半分を切った所で戻す
    if (runData->GetHp() * kKeepAliveHpDivisor <= runData->GetMaxHp()) {
        runData->Heal(runData->GetMaxHp());
    }
}

/** @brief 入ったステージから画面が変わっていたら true（sameStage の手順を飛ばす目安） */
bool SmokeTestRunner::HasLeftStage() const {
    return !inStage_ || SceneManager::GetInstance()->GetSceneSwitchCount() != stageSwitchCount_;
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
    const SceneManager* scenes = SceneManager::GetInstance();
    return scenes->GetCurrentSceneName() == scene && !scenes->IsChangingScene() && framesSinceSwitch_ >= settleFrames_;
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

} // namespace engine::game

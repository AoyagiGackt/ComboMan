/**
 * @file SmokeTestRunner.cpp
 * @brief 自動テストプレイの手順の読み込み・実行・おかしな所の記録・結果の書き出しを実装するファイル
 */
#include "SmokeTestRunner.h"
#include "CrashBreadcrumbs.h"
#include "GameConstants.h"
#include "GameRules.h"
#include "Input.h"
#include "JsonHelper.h"
#include "Logger.h"
#include "MapScene.h"
#include "WeaponManager.h"
#include "RunData.h"
#include "SaveData.h"
#include "SceneManager.h"
#include "ScoreManager.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <crtdbg.h>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <fstream>

namespace engine::game {
namespace {
constexpr const wchar_t* kCommandLineFlag = L"--smoke-test";
constexpr const wchar_t* kBugHuntFlag = L"--bug-hunt";
constexpr const char* kDefaultScriptPath = "Resources/Config/smoke_test.json";
constexpr const char* kBugHuntScriptPath = "BugHunt/bug_hunt.json"; // バグ探しの手順は BugHunt プロジェクトのフォルダに置く
constexpr const wchar_t* kScriptOption = L"--script"; // 手順の表を別の場所から読む時に、続けて場所を書く
constexpr const char* kReportDirectory = "smoke_test";
constexpr const char* kGameplaySceneName = "GAMEPLAY";
constexpr uint64_t kChoiceAnswerIntervalFrames = 90; // 選ぶ画面が出ている時に決定を押す間隔（約1.5秒。見張りの15秒より十分短く）
constexpr float kDefaultTimeoutSeconds = 20.0f;
constexpr float kDefaultIntervalSeconds = 0.5f;
constexpr float kDefaultSettleSeconds = 0.5f;
constexpr float kDefaultRandomHoldSeconds = 0.4f;
constexpr float kDefaultRandomChance = 0.35f;
constexpr float kHitchSeconds = 1.0f; // 画面が落ち着いている時に1フレームでこれ以上かかったら固まりかけとみなす
constexpr int kMaxRepeatDepth = 4; // 繰り返しの入れ子の深さの上限（書き間違いで無限に展開しないように）
constexpr size_t kReportPathCapacity = 256;
constexpr int kMillisecondsPerSecond = 1000;
constexpr int kMaxUpdatesPerDraw = 16; // 1回描くごとに進める回数の上限（上げすぎると画面がほとんど動かず様子が見えない）
constexpr int kKeepAliveHpDivisor = 2; // keepPlayerAlive の時、体力がこの分の1を切ったら全快させる

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

/** @brief コマンド引数の --script の後ろに書いた場所を取り出す（無ければ空。空白を含む場所は "" で囲む） */
std::string ScriptOptionValue(const std::wstring& commandLine) {
    const size_t option = commandLine.find(kScriptOption);
    if (option == std::wstring::npos) {
        return std::string();
    }
    size_t begin = commandLine.find_first_not_of(L' ', option + std::wcslen(kScriptOption));
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

/** @brief 今の時刻を入れた報告ファイルの場所を作る（prefix は result や bugs） */
std::string TimestampedReportPath(const char* prefix) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char path[kReportPathCapacity];
    std::snprintf(path, sizeof(path), "%s/%s_%04d%02d%02d_%02d%02d%02d.txt", kReportDirectory, prefix,
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return path;
}
} // namespace

/** @brief 手順の表の場所をコマンド引数から決める */
std::string SmokeTestRunner::ScriptPathFromCommandLine(const std::wstring& commandLine) {
    if (commandLine.find(kBugHuntFlag) != std::wstring::npos) {
        const std::string custom = ScriptOptionValue(commandLine);
        return custom.empty() ? std::string(kBugHuntScriptPath) : custom;
    }
    if (commandLine.find(kCommandLineFlag) != std::wstring::npos) {
        return kDefaultScriptPath;
    }
    return std::string();
}

/** @brief コマンド引数に --smoke-test か --bug-hunt があれば手順を読み込んで始める */
bool SmokeTestRunner::StartIfRequested(engine::Input* input) {
    scriptPath_ = ScriptPathFromCommandLine(GetCommandLineW());
    if (scriptPath_.empty()) {
        return false;
    }
    input_ = input;
    const nlohmann::json script = JsonHelper::Load(scriptPath_);
    if (!script.is_object() || !script.contains("steps")) {
        Logger::LogError("[SmokeTest] 手順のファイルを読めませんでした: " + scriptPath_);
        return false;
    }
    settleFrames_ = SecondsToFrames(script.value("settleSeconds", kDefaultSettleSeconds));
    // 種を決めておくと、おかしな所が見つかった時に同じ動きでもう一度試せる（0 なら毎回変える）
    seed_ = script.value("seed", 0u);
    if (seed_ == 0) {
        seed_ = std::random_device {}();
    }
    rng_.seed(seed_);
    keepPlayerAlive_ = script.value("keepPlayerAlive", false);
    // 描くのを間引いて速く回す（ゲームの時間は1回ごとに決まった長さだけ進むので、何回進めても動きは同じ）
    updatesPerDraw_ = std::clamp(script.value("speed", 1), 1, kMaxUpdatesPerDraw);
    steps_.clear();
    AppendSteps(script["steps"], 0);
    SuppressErrorDialogs();
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
    Logger::LogInfo("[SmokeTest] 自動テストを始めます（" + scriptPath_ + "、手順 " + std::to_string(steps_.size())
        + " 個、種 " + std::to_string(seed_) + "、速さ " + std::to_string(updatesPerDraw_) + " 倍）");
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
    CheckFrameHitch();
    KeepPlayerAlive();
    // enterNextStage で入ったステージが落ち着いたら、その時の切り替え回数を覚える（以後に変わったらステージを出たとみなす）
    if (pendingStageEntry_ && IsSettledOn(kGameplaySceneName)) {
        pendingStageEntry_ = false;
        inStage_ = true;
        stageSwitchCount_ = switchCount;
    }

    if (stepIndex_ >= steps_.size()) {
        ReleaseKeys();
        sExpectProgress = false;
        WriteReport(true, "");
        finished_ = true;
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
    // ステージの中だけの手順は、自分でクリアした・やられてリトライした等で画面が変わっていたら飛ばして次のステージへ進む
    const bool skipped = step.sameStage && HasLeftStage();
    const bool done = skipped || UpdateStep(step);
    if (finished_) {
        return true;
    }
    ++stepFrame_;
    if (done) {
        log_.push_back((skipped ? "--  手順 " : "OK  手順 ") + std::to_string(stepIndex_ + 1) + " (" + std::to_string(stepFrame_) + " フレーム): " + step.label
            + (skipped ? "（ステージから画面が変わったので飛ばした）" : ""));
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
    if (step.action == "grantBossWeapon") {
        return GrantBossWeapon(step.weapon);
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
    MapScene::BeginTutorial();
    SceneManager::GetInstance()->ChangeScene(kGameplaySceneName);
    pendingStageEntry_ = true;
    inStage_ = false;
    log_.push_back("    チュートリアルへ入る");
    return true;
}

/** @brief ボスを倒さずにボスの武器を渡す（ボスの枠が埋まっていれば、主軸を選ぶ画面が出る） */
bool SmokeTestRunner::GrantBossWeapon(const std::string& weaponName) {
    WeaponManager* weapons = WeaponManager::GetInstance();
    const auto& list = weapons->GetList();
    if (list.empty()) {
        Fail("grantBossWeapon: 武器の一覧が空です");
        return false;
    }
    // 名前が無ければ、今のボスの枠の主軸とも相棒とも違う種類を、一覧の順に選ぶ（主軸を選ぶ画面を通すため）
    WeaponType type = list.front().type;
    if (!weaponName.empty()) {
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
    log_.push_back("    ボスの武器を渡す（結果 " + std::to_string(static_cast<int>(result)) + "）");
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
            "1フレームに " + std::to_string(static_cast<int>(seconds * kMillisecondsPerSecond)) + " ミリ秒かかった");
    }
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
    WriteReport(false, reason);
    finished_ = true;
}

/** @brief 結果のファイルを書き出す */
void SmokeTestRunner::WriteReport(bool passed, const std::string& reason) const {
    const std::string headline = passed ? "[SmokeTest] 成功: 全部の手順を終えました" : "[SmokeTest] 失敗: " + reason;
    if (passed) {
        Logger::LogInfo(headline);
    } else {
        Logger::LogError(headline);
    }
    CrashBreadcrumbs::Add("smoke", passed ? "自動テスト成功" : "自動テスト失敗");

    std::error_code error;
    std::filesystem::create_directories(kReportDirectory, error);
    const std::string path = TimestampedReportPath("result");
    std::ofstream report(path);
    if (!report) {
        return;
    }
    report << (passed ? "成功" : "失敗") << '\n';
    if (!passed) {
        report << "理由: " << reason << '\n';
    }
    report << "手順の表: " << scriptPath_ << "  ランダムの種: " << seed_ << '\n';
    report << "かかったフレーム数: " << totalFrames_ << "（約 " << static_cast<int>(static_cast<float>(totalFrames_) * gameConstants::kFrameDeltaTime) << " 秒）\n";
    report << "見つけたおかしな所: " << sIssues.size() << " 件\n";
    report << "--- 見つけたおかしな所（見つけた順） ---\n";
    for (const Issue& issue : sIssues) {
        report << "[" << issue.kind << "] " << issue.subject << "（" << issue.count << " 回、最初は frame " << issue.firstFrame << "）\n";
        report << "    様子: " << issue.detail << '\n';
        report << "    その時: " << issue.context << '\n';
    }
    report << "--- 手順の記録 ---\n";
    for (const std::string& line : log_) {
        report << line << '\n';
    }
    if (!passed) {
        report << "--- 直前の出来事（古い順） ---\n";
        CrashBreadcrumbs::ForEach([&](const CrashBreadcrumbs::Entry& entry) {
            report << "[frame " << entry.frame << "] " << entry.category << ": " << entry.text << '\n';
        });
    }
    Logger::LogInfo("[SmokeTest] 結果を書き出しました: " + path);
}

} // namespace engine::game

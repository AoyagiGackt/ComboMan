/**
 * @file GameSmokeTest.cpp
 * @brief ゲーム固有の手順、画面の問い合わせ、プレイヤーの状態の管理
 */
#include "GameSmokeTest.h"
#include "GameRules.h"
#include "Input.h"
#include "MapScene.h"
#include "WeaponManager.h"
#include "RunData.h"
#include "SaveData.h"
#include "SceneManager.h"
#include "ScoreManager.h"
#include <algorithm>

namespace engine::game {
namespace {
constexpr const char* kDefaultScriptPath = "Resources/Config/smoke_test.json";
constexpr const char* kBugHuntScriptPath = "BugHunt/bug_hunt.json"; // バグ探しの手順は BugHunt プロジェクトのフォルダに置く
constexpr uint64_t kChoiceAnswerIntervalFrames = 90; // 選ぶ画面が出ている時に決定を押す間隔（約1.5秒。見張りの15秒より十分短く）
constexpr int kKeepAliveHpDivisor = 2; // keepPlayerAlive の時、体力がこの分の1を切ったら全快させる
constexpr int kDefaultMaxVisitsPerStage = 12; // 1つのステージで近くへ移って試す配置物の数の上限の既定値
constexpr float kDefaultVisitOffset = 1.5f; // 配置物からどれだけ左に移るかの既定値
constexpr float kDefaultVisitRaise = 1.0f; // 配置物からどれだけ上に移るかの既定値（落ちて床に立つ）
} // namespace

/** @brief 設定を返す */
SmokeTestConfig GameSmokeTest::GetConfig() const {
    return { kDefaultScriptPath, kBugHuntScriptPath, kGameplaySceneName };
}

/** @brief 現在の画面の名前を返す */
const std::string& GameSmokeTest::GetCurrentSceneName() const {
    return SceneManager::GetInstance()->GetCurrentSceneName();
}

/** @brief 画面の切り替え中かを返す */
bool GameSmokeTest::IsChangingScene() const {
    return SceneManager::GetInstance()->IsChangingScene();
}

/** @brief 画面を切り替えた回数を返す */
uint32_t GameSmokeTest::GetSceneSwitchCount() const {
    return SceneManager::GetInstance()->GetSceneSwitchCount();
}

/** @brief 画面の切り替えを頼む */
void GameSmokeTest::ChangeScene(const std::string& scene) {
    SceneManager::GetInstance()->ChangeScene(scene);
}

/** @brief ゲーム固有の設定を読む */
void GameSmokeTest::LoadSettings(const nlohmann::json& script) {
    keepPlayerAlive_ = script.value("keepPlayerAlive", false);
    maxVisitsPerStage_ = (std::max)(0, script.value("maxVisitsPerStage", kDefaultMaxVisitsPerStage));
    visitOffset_ = script.value("visitOffset", kDefaultVisitOffset);
    visitRaise_ = script.value("visitRaise", kDefaultVisitRaise);
    visitSkipKinds_.clear();
    for (const nlohmann::json& kind : script.value("visitSkipKinds", nlohmann::json::array())) {
        if (kind.is_string()) {
            visitSkipKinds_.push_back(kind.get<std::string>());
        }
    }
}

/** @brief 自動テスト開始時の準備をする */
void GameSmokeTest::OnStart() {
    // NEW GAME やクリアで書き換わるので、テスト中はプレイヤーのセーブとランキングをファイルへ書かない
    SaveDataManager::GetInstance()->SetWritesEnabled(false);
    ScoreManager::GetInstance()->SetWritesEnabled(false);
}

/** @brief 手順終了時に残った要求を片付ける */
void GameSmokeTest::OnStepFinished() {
    // 受け取られないまま手順が終わった頼みは、次のステージへ持ち越さない
    sBossDuelRequested = false;
}

/** @brief ゲーム固有の手順なら真を返し、完了したかを done に入れる */
bool GameSmokeTest::UpdateAction(const SmokeTestStep& step, bool& done) {
    if (step.action == "enterNextStage") {
        done = EnterNextStage();
    } else if (step.action == "enterTutorial") {
        done = EnterTutorial();
    } else if (step.action == "enterFinalBoss") {
        done = EnterFinalBoss(step);
    } else if (step.action == "visitChanged") {
        done = UpdateVisit(step);
    } else if (step.action == "grantBossWeapon") {
        done = GrantBossWeapon(WeaponName(step), step.raw.value("makeMain", false));
    } else if (step.action == "grantWeapon") {
        done = UpdateGrantWeapon(step);
    } else if (step.action == "coverGaps") {
        done = UpdateCoverGaps(step);
    } else if (step.action == "resetTrainingWeapons" || step.action == "visitTrainingPickup"
        || step.action == "expectWeapon" || step.action == "expectDuplicateWeapon" || step.action == "checkLastFusion") {
        done = CheckTrainingWeapon(step);
    } else {
        return false;
    }
    return true;
}

/** @brief 見張りに渡すステージの名前を返す */
std::string GameSmokeTest::GetStageLabel() const {
    return RunData::GetInstance()->IsTutorialActive() ? std::string("チュートリアル")
        : "ステージ " + std::to_string(RunData::GetInstance()->GetPlayFloor() + 1);
}

/** @brief 不具合の記録に添える様子を返す */
std::string GameSmokeTest::GetContextSuffix(const SmokeTestStep& step) const {
    std::string suffix = " " + GetStageLabel();
    if (step.action == "visitChanged" && !visitLabel_.empty()) {
        suffix += " " + visitLabel_;
    }
    return suffix;
}

/** @brief 変更のまとめにゲーム固有の結果を書き足す */
void GameSmokeTest::WriteChangesReport(std::ostream& report) const {
    report << "、近くで試した配置物 " << visitedCount_ << " 個";
}

/** @brief 手順で指定した武器の名前を読む */
std::string GameSmokeTest::WeaponName(const SmokeTestStep& step) {
    return step.raw.value("weapon", std::string());
}

/** @brief 今のランで次に遊ぶステージ（最後のステージの後はラスボス戦）へ直接入る */
bool GameSmokeTest::EnterNextStage() {
    RunData* runData = RunData::GetInstance();
    if (!runData->IsRunActive()) {
        runner_.Fail("enterNextStage: ランが始まっていません（先にタイトルから NEW GAME してください）");
        return false;
    }
    const int stageCount = static_cast<int>(GameRules::GetInstance()->Get().levelPaths.size());
    const int floor = runData->GetFloor();
    if (floor >= stageCount) {
        // 全部のステージを終えた後は、マップと同じくラスボス戦へ入る
        if (!runData->IsFinalBossPending()) {
            runner_.Fail("enterNextStage: 全部のステージを終えていて、ラスボス戦も残っていません");
            return false;
        }
        SceneManager::GetInstance()->ChangeScene(kGameplaySceneName);
        runner_.MarkStageEntry();
        return true;
    }
    // 前回から変えていないステージは遊ばずに、クリアした扱いで進める
    if (!runner_.GetChanges().IsStageSelected(GameRules::GetInstance()->LevelPathForFloor(floor, std::string()))) {
        SkipFloor(floor);
        return true;
    }
    runner_.SetSkippingStage(false);
    // マップと同じく、最初は通常・途中は強敵・最後はボス扱いのマスとして入る
    const int lastFloor = stageCount - 1;
    const RunData::NodeType node = floor == lastFloor ? RunData::NodeType::kBoss
        : floor == 0                                  ? RunData::NodeType::kCombat
                                                      : RunData::NodeType::kElite;
    runData->SetCurrentNode(node);
    runData->SetPlayFloor(floor);
    SceneManager::GetInstance()->ChangeScene(kGameplaySceneName);
    runner_.MarkStageEntry();
    runner_.AddLog("    ステージ " + std::to_string(floor + 1) + " へ入る");
    return true;
}

/** @brief マップの入口と同じ準備をしてチュートリアルのステージへ直接入る（クリアやポーズで抜けるとマップへ戻る） */
bool GameSmokeTest::EnterTutorial() {
    if (!RunData::GetInstance()->IsRunActive()) {
        runner_.Fail("enterTutorial: ランが始まっていません（先にタイトルから NEW GAME してください）");
        return false;
    }
    // チュートリアルを置いていない設定なら、飛ばして次の手順へ進む
    if (GameRules::GetInstance()->Get().tutorialLevelPath.empty()) {
        runner_.AddLog("    チュートリアルが無いので飛ばす");
        return true;
    }
    // 前回から変えていなければ入らずに、チュートリアルの中の手順を飛ばす
    if (!runner_.GetChanges().IsStageSelected(GameRules::GetInstance()->Get().tutorialLevelPath)) {
        runner_.AddLog("    チュートリアルは前回から変えていないので飛ばす");
        runner_.SetSkippingStage(true);
        return true;
    }
    runner_.SetSkippingStage(false);
    MapScene::BeginTutorial();
    SceneManager::GetInstance()->ChangeScene(kGameplaySceneName);
    runner_.MarkStageEntry();
    runner_.AddLog("    チュートリアルへ入る");
    return true;
}

/** @brief ボスを倒さずにボスの武器を渡す（ボスの枠が埋まっていれば、主軸を選ぶ画面が出る） */
bool GameSmokeTest::GrantBossWeapon(const std::string& weaponName, bool makeMain) {
    WeaponManager* weapons = WeaponManager::GetInstance();
    const auto& list = weapons->GetList();
    if (list.empty()) {
        runner_.Fail("grantBossWeapon: 武器の一覧が空です");
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
    runner_.AddLog("    ボスの武器を渡す（" + std::string(WeaponTypeName(type)) + "、結果 " + std::to_string(static_cast<int>(result))
        + (chose ? "、主軸にした）" : "）"));
    return true;
}

/** @brief 武器の交換や主軸を選ぶ画面が出ていたら、決まった間隔で決定（交換は1つ目の枠）を押して閉じる */
void GameSmokeTest::BeforeAction() {
    const WeaponManager* weapons = WeaponManager::GetInstance();
    const bool bossChoice = weapons->HasPendingBossChoice();
    const bool exchange = weapons->HasPendingWeapon();
    if (!bossChoice && !exchange) {
        return;
    }
    // 間隔の最初のフレームに押して、次のフレームに離す（押した瞬間の判定が1回だけ起きる）
    const uint64_t phase = runner_.GetTotalFrames() % kChoiceAnswerIntervalFrames;
    const uint8_t key = bossChoice ? static_cast<uint8_t>(DIK_RETURN) : static_cast<uint8_t>(DIK_1);
    if (phase <= 1) {
        runner_.InjectKey(key, phase == 0);
    }
}

/** @brief keepPlayerAlive の時、体力が減りすぎたら全快させて最後まで回れるようにする（ダメージ自体は受ける） */
void GameSmokeTest::OnFrame() {
    RunData* runData = RunData::GetInstance();
    if (!keepPlayerAlive_ || !runData->IsRunActive()) {
        return;
    }
    // 大きな一撃で一気に0にならないよう、半分を切った所で戻す
    if (runData->GetHp() * kKeepAliveHpDivisor <= runData->GetMaxHp()) {
        runData->Heal(runData->GetMaxHp());
    }
}

} // namespace engine::game

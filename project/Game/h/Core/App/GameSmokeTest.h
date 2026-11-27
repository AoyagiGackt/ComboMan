/**
 * @file GameSmokeTest.h
 * @brief ゲーム専用の自動テスト手順と状態の見張り
 */
#pragma once
#include "SmokeTestRunner.h"
#include "Vector3.h"

namespace engine::game {

/** @brief ステージと武器に関わる手順を進める */
class GameSmokeTest final : public SmokeTestGame {
public:
    /** @brief ゲーム本体が所有する進行役を借りる */
    explicit GameSmokeTest(SmokeTestRunner& runner) : runner_(runner) {}
    /** @brief 手順が頼んだ、ボスと向き合う準備を受け取る（受け取ったら頼みは消える。ボスの体力を1にしてプレイヤーを手前へ移すため） */
    static bool ConsumeBossDuelRequest() {
        const bool requested = sBossDuelRequested;
        sBossDuelRequested = false;
        return requested;
    }
    /** @brief 設定を返す */
    SmokeTestConfig GetConfig() const override;
    /** @brief 現在の画面の名前を返す */
    const std::string& GetCurrentSceneName() const override;
    /** @brief 画面の切り替え中かを返す */
    bool IsChangingScene() const override;
    /** @brief 画面を切り替えた回数を返す */
    uint32_t GetSceneSwitchCount() const override;
    /** @brief 画面の切り替えを頼む */
    void ChangeScene(const std::string& scene) override;
    /** @brief ゲーム固有の設定を読む */
    void LoadSettings(const nlohmann::json& script) override;
    /** @brief 自動テスト開始時の準備をする */
    void OnStart() override;
    /** @brief 画面が落ち着いたかを調べる前にゲームの状態を整える */
    void OnFrame() override;
    /** @brief 手順を進める直前にゲームの入力を補う */
    void BeforeAction() override;
    /** @brief 手順終了時に残った要求を片付ける */
    void OnStepFinished() override;
    /** @brief ゲーム固有の手順なら真を返し、完了したかを done に入れる */
    bool UpdateAction(const SmokeTestStep& step, bool& done) override;
    /** @brief 状態の指紋を作る */
    SmokeTestReplay::Checkpoint Capture(uint64_t frame) const override;
    /** @brief 前回の記録と比べるステージの一覧を返す */
    std::vector<BugHuntChanges::Stage> GetStages() const override;
    /** @brief 見張りに渡すステージの名前を返す */
    std::string GetStageLabel() const override;
    /** @brief 不具合の記録に添える様子を返す */
    std::string GetContextSuffix(const SmokeTestStep& step) const override;
    /** @brief 変更のまとめにゲーム固有の結果を書き足す */
    void WriteChangesReport(std::ostream& report) const override;

private:
    static constexpr const char* kNextWeaponName = "next"; ///< 武器を一覧の順に渡す指定
    static constexpr const char* kGameplaySceneName = "GAMEPLAY"; ///< プレイヤーと敵を調べるステージの画面
    /** @brief 近くへ移って試す配置物1つ */
    struct VisitTarget {
        std::string name; ///< 配置物の名前
        std::string kind; ///< 配置物の種類
        Vector3 position {}; ///< ワールドの位置
    };

    /** @brief coverGaps で狙う物1つ（まだ試していない物を起こすために渡す武器と、することの組） */
    struct GapTarget {
        std::string weapon; ///< 渡す武器の種類の名前
        bool bossWeapon = false; ///< ボスの枠に渡すか（ボスの武器のコンボを試す時）
        bool duel = false; ///< ボスの体力を1にして手前へ移り、この武器で倒すか（とどめの型を試す時）
        std::string reason; ///< 報告に出す、狙う物の説明
    };

    /** @brief 今のランで次に遊ぶステージ（最後のステージの後はラスボス戦）へ直接入る */
    bool EnterNextStage();
    /** @brief マップの入口と同じ準備をしてチュートリアルのステージへ直接入る（クリアやポーズで抜けるとマップへ戻る） */
    bool EnterTutorial();
    /**
     * @brief ボスを倒さずにボスの武器を渡す（ボスの枠が埋まっていれば、主軸を選ぶ画面が出る）
     * @param weaponName 武器の種類の名前（空なら今のボスの枠と違う種類を選ぶ）
     * @param makeMain 主軸を選ぶ画面が出る時に、画面を通さずに渡した武器を主軸にするか
     */
    bool GrantBossWeapon(const std::string& weaponName, bool makeMain);
    /**
     * @brief ふつうの武器を渡して枠に入れる（枠が満杯なら1つ目の枠と入れ替える）
     * @param weaponName 武器の種類の名前（next なら一覧の順に次の種類）
     * @return 入れた枠（渡せなければ -1）
     */
    int GrantWeapon(const std::string& weaponName);
    /** @brief grantWeapon の手順を1フレーム進める（渡した後、その枠の数字キーで持ち替える。終わったら true） */
    bool UpdateGrantWeapon(const SmokeTestStep& step);
    /** @brief coverGaps の手順を1フレーム進める（まだ試していない物を1つずつ狙って操作する。終わったら true） */
    bool UpdateCoverGaps(const SmokeTestStep& step);
    /**
     * @brief まだ試していないコンボの段ととどめの型から、狙う物の並びを作る
     * @param only combo ならコンボの段だけ、execution ならとどめの型だけ、空なら両方
     */
    void BuildGapTargets(const std::string& only);
    /** @brief 今のステージのボスを置いた場所の手前へプレイヤーを移す（ボスがまだ出ていなくても近づいて出させるため。置いていなければ false） */
    bool TeleportToStageBoss();
    /** @brief 変えていないステージを遊ばずに、クリアした扱いで進行度だけ進める */
    void SkipFloor(int floor);
    /** @brief 最後のステージの後にラスボス戦へ入る（変えていなければ、ここで手順を終える。終わったら true） */
    bool EnterFinalBoss(const SmokeTestStep& step);
    /** @brief visitChanged の手順を1フレーム進める（変えた配置物の近くへ1つずつ移り、決めた秒数だけ操作する。終わったら true） */
    bool UpdateVisit(const SmokeTestStep& step);
    /** @brief 練習場で武器を拾うか、装備と合体耐久の結果を検証する */
    bool CheckTrainingWeapon(const SmokeTestStep& step);
    /** @brief 今のステージで近くへ移って試す配置物を集める */
    void BuildVisitTargets();
    /** @brief プレイヤーを配置物の手前へ移す */
    void TeleportTo(const VisitTarget& target);
    /** @brief 手順で指定した武器の名前を読む */
    static std::string WeaponName(const SmokeTestStep& step);

    SmokeTestRunner& runner_; ///< ゲーム本体が所有する進行役
    size_t grantRotation_ = 0; ///< grantBossWeapon の weapon が next の時に、次に渡す武器の一覧の番号
    size_t weaponGrantRotation_ = 0; ///< grantWeapon の weapon が next の時に、次に渡す武器の一覧の番号
    int grantedSlot_ = -1; ///< grantWeapon・coverGaps で武器を入れた枠（数字キーで持ち替えるため。ボスの枠なら kBossSlot）
    std::vector<GapTarget> gapTargets_; ///< coverGaps で狙う物の並び
    size_t gapIndex_ = 0; ///< coverGaps で今狙っている物の番号
    int gapFrame_ = 0; ///< coverGaps で今の物を狙い始めてからのフレーム数
    bool keepPlayerAlive_ = false; ///< 体力が減りすぎたら全快させるか（手順の表の keepPlayerAlive）
    std::vector<VisitTarget> visitTargets_; ///< 今の visitChanged で近くへ移って試す配置物
    std::string visitLabel_; ///< 今試している配置物（記録に添える）
    int visitedCount_ = 0; ///< 近くへ移って試した配置物の数（報告に出す）
    int maxVisitsPerStage_ = 0; ///< 1つのステージで試す配置物の数の上限（手順の表の maxVisitsPerStage）
    float visitOffset_ = 0.0f; ///< 配置物からどれだけ左に移るか（手順の表の visitOffset）
    float visitRaise_ = 0.0f; ///< 配置物からどれだけ上に移るか（手順の表の visitRaise）
    std::vector<std::string> visitSkipKinds_; ///< 近くで試さない種類（手順の表の visitSkipKinds）
    static inline bool sBossDuelRequested = false; ///< ボスと向き合う準備を頼んでいるか（ゲーム側が ConsumeBossDuelRequest で受け取る）
};

} // namespace engine::game

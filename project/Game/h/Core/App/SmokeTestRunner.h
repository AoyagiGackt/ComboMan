/**
 * @file SmokeTestRunner.h
 * @brief 自動テストプレイ（--smoke-test で起動すると、手順の表のとおりにゲームを自分で遊び、落ちる・止まる所を見つける）
 * @note 手順は Resources/Config/smoke_test.json に書く（画面を待つ・キーを押す・押し続ける・ランダムに操作する・繰り返す）。
 *       --bug-hunt で起動すると、BugHunt/bug_hunt.json（--script で別の場所も指定できる）の手順で全ステージとラスボス戦をいろいろな遊び方で回す。
 *       操作は Input::InjectKey で本物のキーと同じ経路から流すので、ゲーム側のコードはテスト用に変えなくてよい。
 *       ゲーム側は遊んでいる間におかしな所を見つけたら ReportIssue で知らせる（止めずに記録だけして先へ進む）。
 *       結果は smoke_test フォルダに書き出す。途中で落ちた場合は CrashHandler の報告に、どの手順の途中だったかが残る
 */
#pragma once
#include "json.hpp"
#include <chrono>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace engine {
class Input;
}

namespace engine::game {

/** @brief 自動テストプレイの進行役 */
class SmokeTestRunner {
public:
    /**
     * @brief コマンド引数に --smoke-test か --bug-hunt があれば手順を読み込んで始める
     * @return 自動テストを始めたら true
     */
    bool StartIfRequested(engine::Input* input);

    /** @brief 自動テスト中か */
    bool IsRunning() const {
        return running_;
    }
    /** @brief 自動テストプレイで起動しているか（シーンが、自動テストの時だけ効く近道のキーを受けるかを決める） */
    static bool IsActive() {
        return sActive;
    }
    /** @brief 1回描くごとにゲームを何回進めるか（手順の表の speed。速回しにしない時は 1） */
    int GetUpdatesPerDraw() const {
        return running_ && !finished_ ? updatesPerDraw_ : 1;
    }
    /** @brief 今の手順が、先へ進む操作をしているか（進めない所を見張る目安。手順の expectProgress） */
    static bool IsExpectingProgress() {
        return sExpectProgress;
    }

    /**
     * @brief 遊んでいる間に見つけたおかしな所を記録する（同じ種類と相手は1件にまとめて回数を数える）
     * @param kind 種類（例: 扉の条件・敵の位置）
     * @param subject 相手（敵の名前など。同じ種類でも相手が違えば別の件にする）
     * @param detail 詳しい様子（最初に見つけた時のものを残す）
     * @note 自動テストプレイでない時は何もしない
     */
    static void ReportIssue(const std::string& kind, const std::string& subject, const std::string& detail);

    /**
     * @brief 手順を1フレーム進める（シーンの更新の後、毎フレーム呼ぶ。押したキーは次のフレームの入力から効く）
     * @return 自動テストを終えてゲームを閉じてよければ true
     */
    bool Update();

private:
    /** @brief 手順1つ（手順の表の steps の1要素を、繰り返しを展開して平らにしたもの） */
    struct Step {
        std::string action; ///< waitScene / waitSceneChange / waitSettled / pressUntilScene / press / hold / wait / random / changeScene / enterNextStage / enterTutorial / grantBossWeapon / mark
        std::string weapon; ///< grantBossWeapon で渡す武器の種類の名前（空なら、今のボスの枠の主軸とも相棒とも違う種類を選ぶ）
        std::string scene;
        std::vector<uint8_t> keys;
        std::vector<uint8_t> holdKeys; ///< random の間ずっと押し続けるキー（右へ歩きながら暴れる、など）
        std::string label; ///< 報告に出す説明
        int frames = 0; ///< 長さ（秒をフレームに直したもの）
        int timeoutFrames = 0; ///< これを過ぎたら止まったとみなす
        int intervalFrames = 0; ///< pressUntilScene でキーを押し直す間隔、random で押し直す間隔
        float chance = 0.0f; ///< random でそれぞれのキーを押す確率
        bool expectProgress = false; ///< この手順の間は先へ進めるはず（進めなければゲーム側が知らせる）
        bool sameStage = false; ///< 入ったステージの中だけの手順（自分でクリアした・やられてリトライした等で画面が変わったら飛ばす）
    };

    /** @brief 見つけたおかしな所1件 */
    struct Issue {
        std::string kind; ///< 種類
        std::string subject; ///< 相手
        std::string detail; ///< 最初に見つけた時の様子
        std::string context; ///< 見つけた時の手順と画面
        uint64_t firstFrame = 0; ///< 最初に見つけたフレーム
        int count = 0; ///< 見つけた回数
    };

    /** @brief 手順の表の場所をコマンド引数から決める */
    static std::string ScriptPathFromCommandLine(const std::wstring& commandLine);
    /** @brief キーの名前の配列を、押せるキーの番号へ直す（Esc など押せないキーは飛ばす） */
    static std::vector<uint8_t> ParseKeys(const nlohmann::json& names);
    /** @brief JSON の手順の配列を、繰り返しを展開しながら steps_ へ積む */
    void AppendSteps(const nlohmann::json& steps, int depth);
    /** @brief 今の手順を1フレーム進める（終わったら true） */
    bool UpdateStep(const Step& step);
    /** @brief random の手順を1フレーム進める（終わったら true） */
    bool UpdateRandom(const Step& step);
    /** @brief 今のランで次に遊ぶステージ（最後のステージの後はラスボス戦）へ直接入る */
    bool EnterNextStage();
    /** @brief マップの入口と同じ準備をしてチュートリアルのステージへ直接入る（クリアやポーズで抜けるとマップへ戻る） */
    bool EnterTutorial();
    /**
     * @brief ボスを倒さずにボスの武器を渡す（ボスの枠が埋まっていれば、主軸を選ぶ画面が出る）
     * @param weaponName 武器の種類の名前（空なら今のボスの枠と違う種類を選ぶ）
     */
    bool GrantBossWeapon(const std::string& weaponName);
    /** @brief 武器の交換や主軸を選ぶ画面が出ていたら、決まった間隔で決定（交換は1つ目の枠）を押して閉じる（手順が決定を押さない時に止まらないように） */
    void AnswerWeaponChoice();
    /** @brief 前のフレームから実時間でかかりすぎていたら、固まりかけとして記録する */
    void CheckFrameHitch();
    /** @brief keepPlayerAlive の時、体力が減りすぎたら全快させて最後まで回れるようにする（ダメージ自体は受ける） */
    void KeepPlayerAlive();
    /** @brief 入ったステージから画面が変わっていたら true（sameStage の手順を飛ばす目安） */
    bool HasLeftStage() const;
    /** @brief 今の画面が scene で、切り替えも落ち着いているか */
    bool IsSettledOn(const std::string& scene) const;
    /** @brief 押したことにしているキーを全部離す */
    void ReleaseKeys();
    /** @brief 失敗として記録して終える */
    void Fail(const std::string& reason);
    /** @brief 結果のファイルを書き出す */
    void WriteReport(bool passed, const std::string& reason) const;

    engine::Input* input_ = nullptr;
    std::vector<Step> steps_;
    std::vector<std::string> log_; ///< 報告に出す、終えた手順の記録
    std::mt19937 rng_ { std::random_device {}() };
    uint32_t seed_ = 0; ///< ランダム操作の種（報告に残し、同じ種でもう一度同じ動きを試せるようにする）
    std::string scriptPath_; ///< 読んだ手順の表
    size_t stepIndex_ = 0;
    int stepFrame_ = 0; ///< 今の手順を始めてからのフレーム数
    uint32_t switchCountAtStep_ = 0; ///< 今の手順を始めた時の画面の切り替え回数
    uint32_t lastSwitchCount_ = 0;
    int framesSinceSwitch_ = 0; ///< 最後に画面が切り替わってからのフレーム数（切り替え直後の不安定な間は待つ）
    int settleFrames_ = 0; ///< 画面が切り替わってから落ち着いたとみなすまでのフレーム数
    uint64_t totalFrames_ = 0;
    uint32_t stageSwitchCount_ = 0; ///< enterNextStage で入ったステージが落ち着いた時の画面の切り替え回数
    bool inStage_ = false; ///< enterNextStage で入ったステージがまだ続いているとみなせるか
    bool pendingStageEntry_ = false; ///< enterNextStage で切り替えを頼み、ステージが落ち着くのを待っているか
    bool keepPlayerAlive_ = false; ///< 体力が減りすぎたら全快させるか（手順の表の keepPlayerAlive）
    int updatesPerDraw_ = 1; ///< 1回描くごとにゲームを何回進めるか（手順の表の speed）
    std::chrono::steady_clock::time_point lastUpdateTime_; ///< 前のフレームの実時間（固まりかけを見張る）
    bool hasLastUpdateTime_ = false;
    bool running_ = false;
    static inline bool sActive = false; ///< 自動テストプレイで起動しているか
    static inline bool sExpectProgress = false; ///< 今の手順が先へ進む操作をしているか
    static inline std::vector<Issue> sIssues; ///< 見つけたおかしな所
    static inline std::string sContext; ///< 今の手順と画面（記録に添える）
    static inline uint64_t sFrame = 0; ///< 今のフレーム（記録に添える）
    bool finished_ = false;
};

} // namespace engine::game

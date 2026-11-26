/**
 * @file SmokeTestRunner.h
 * @brief 自動テストプレイ（--smoke-test で起動すると、手順の表のとおりにゲームを自分で遊び、落ちる・止まる所を見つける）
 * @note 手順は Resources/Config/smoke_test.json に書く（画面を待つ・キーを押す・押し続ける・ランダムに操作する・繰り返す）。
 *       --bug-hunt で起動すると、BugHunt/bug_hunt.json（--script で別の場所も指定できる）の手順で全ステージとラスボス戦をいろいろな遊び方で回す。
 *       操作は Input::InjectKey で本物のキーと同じ経路から流すので、ゲーム側のコードはテスト用に変えなくてよい。
 *       ゲーム側は遊んでいる間におかしな所を見つけたら ReportIssue で知らせる（止めずに記録だけして先へ進む）。
 *       結果は smoke_test フォルダに書き出す。途中で落ちた場合は CrashHandler の報告に、どの手順の途中だったかが残る
 *       --bug-hunt の時は、前回の記録（BugHuntChanges）から変えたステージだけを遊び、変えた配置物の近くへ移って試す（--all なら全部遊ぶ）
 *       自動テストの間は本物の入力を切り（--allow-input で使える）、乱数の種もすべて1つの種から作るので、同じ表と種で回すと同じ動きになる。
 *       おかしな所が見つかったら smoke_test/replay_～.json に再生ファイルを書き、--replay <ファイル> で同じ動きをもう一度見られる
 *       （見つけた所の少し手前までは速回しで進め、そこからふつうの速さで見せる）
 */
#pragma once
#include "BugHuntChanges.h"
#include "SmokeTestMonitors.h"
#include "SmokeTestReplay.h"
#include "Vector3.h"
#include "json.hpp"
#include <chrono>
#include <mutex>
#include <ostream>
#include <thread>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace engine {
class DirectXCommon;
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
    bool StartIfRequested(engine::Input* input, engine::DirectXCommon* dxCommon);
    /**
     * @brief 自動テストで起動していれば、乱数の元の種を決める（乱数を作る物より先、起動の最初に呼ぶ）
     * @note 種は 再生ファイル → 手順の表の seed → その場で決めた種 の順に決める。決めた種は報告と再生ファイルに残る
     */
    static void ApplySeedFromCommandLine();
    /** @brief ゲームを1回進める前に呼ぶ（1回の更新の重さを測る） */
    void BeginUpdate() {
        monitors_.BeginUpdate();
    }
    /** @brief 再生で見つけた所が近づき、速回しをやめてふつうの速さで見せたいか */
    bool WantsRealtime() const {
        return replayRealtime_;
    }

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
        return running_ && !finished_ && !replayRealtime_ ? updatesPerDraw_ : 1;
    }
    /** @brief 今の手順が、先へ進む操作をしているか（進めない所を見張る目安。手順の expectProgress） */
    static bool IsExpectingProgress() {
        return sExpectProgress;
    }
    /** @brief 手順がプレイヤーを離れた所へ移した回数（見張りが、移す前と後の位置を比べて壁抜けと間違えないように使う） */
    static uint32_t TeleportCount() {
        return sTeleportCount;
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
        std::string action; ///< waitScene / waitSceneChange / waitSettled / pressUntilScene / press / hold / wait / random / changeScene / enterNextStage / enterTutorial / enterFinalBoss / visitChanged / grantBossWeapon / mark
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
        bool makeMain = false; ///< grantBossWeapon で、渡した武器をその場で合体の主軸にするか（ボスの武器のコンボを順に試すため）
        std::vector<std::string> moveNames; ///< monkey で使う技の名前（空なら手順の表の monkeyMoves の全部）
    };

    /** @brief でたらめ操作の技1つ（手順の表の monkeyMoves の1要素） */
    struct MonkeyMove {
        std::string name; ///< 技の名前（報告と網羅に出す）
        float weight = 1.0f; ///< 選ばれやすさ
        std::vector<std::vector<uint8_t>> sequence; ///< 順に押すキーの組（1つの組の中は同時押し）
        int downFrames = 1; ///< 1つの組を押しておくフレーム数
        int gapFrames = 1; ///< 組と組の間に離しておくフレーム数
    };

    /** @brief 近くへ移って試す配置物1つ */
    struct VisitTarget {
        std::string name; ///< 配置物の名前
        std::string kind; ///< 配置物の種類
        Vector3 position {}; ///< ワールドの位置
    };

    /** @brief 見つけたおかしな所1件 */
    struct Issue {
        std::string kind; ///< 種類
        std::string subject; ///< 相手
        std::string detail; ///< 最初に見つけた時の様子
        std::string context; ///< 見つけた時の手順と画面
        uint64_t firstFrame = 0; ///< 最初に見つけたフレーム
        int count = 0; ///< 見つけた回数
        std::string screenshot; ///< 見つけた時の画面を保存した場所（保存しなかったら空）
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
     * @param makeMain 主軸を選ぶ画面が出る時に、画面を通さずに渡した武器を主軸にするか
     */
    bool GrantBossWeapon(const std::string& weaponName, bool makeMain);
    /**
     * @brief 前回の記録と比べて、遊ぶステージと近くで試す配置物を決める（--bug-hunt の時だけ）
     * @param commandLine コマンド引数（--all を見る）
     * @param importedSelection 再生の時は、再生ファイルに入れておいた選び方（それをそのまま使う）。ふだんは nullptr
     */
    void PrepareChanges(const std::wstring& commandLine, const nlohmann::json* importedSelection);
    /** @brief 変えていないステージを遊ばずに、クリアした扱いで進行度だけ進める */
    void SkipFloor(int floor);
    /** @brief 最後のステージの後にラスボス戦へ入る（変えていなければ、ここで手順を終える。終わったら true） */
    bool EnterFinalBoss(const Step& step);
    /** @brief visitChanged の手順を1フレーム進める（変えた配置物の近くへ1つずつ移り、決めた秒数だけ操作する。終わったら true） */
    bool UpdateVisit(const Step& step);
    /** @brief 練習場で武器を拾うか、装備と合体耐久の結果を検証する */
    bool CheckTrainingWeapon(const Step& step);
    /** @brief 今のステージで近くへ移って試す配置物を集める */
    void BuildVisitTargets();
    /** @brief プレイヤーを配置物の手前へ移す */
    void TeleportTo(const VisitTarget& target);
    /** @brief 残りの手順を飛ばし、成功として終える（変えた所を全部回り終えた時） */
    void FinishEarly(const std::string& reason);
    /** @brief 武器の交換や主軸を選ぶ画面が出ていたら、決まった間隔で決定（交換は1つ目の枠）を押して閉じる（手順が決定を押さない時に止まらないように） */
    void AnswerWeaponChoice();
    /** @brief 前のフレームから実時間でかかりすぎていたら、固まりかけとして記録する */
    void CheckFrameHitch();
    /** @brief どの段階で時間がかかったかの文（前のフレームの更新・描画と、このフレームの始まりからの時間） */
    static std::string DescribeFramePhases();
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
    /**
     * @brief 今の時刻を入れた、報告のフォルダの中の場所を作る
     * @param prefix ファイル名の頭（result や replay）
     * @param extension 拡張子（点は付けない）
     */
    static std::string ReportPath(const char* prefix, const char* extension);
    /** @brief 結果のファイルを書き出す */
    void WriteReport(bool passed, const std::string& reason) const;
    /** @brief 報告に、何を試したか（網羅）のまとめを書く */
    static void WriteCoverage(std::ostream& out);
    /** @brief おかしな所があったか止まった時に、再生ファイルを書く（書いた場所を返す。書かなければ空） */
    std::string WriteReplay(bool passed, const std::string& reason) const;
    /** @brief 見張りをやめて、終わったことにする */
    void Finish();
    /** @brief 再生の時、見せたいフレームに近づいたらふつうの速さにし、見せ終えたら終える（終えたら true） */
    bool UpdateReplay();
    /** @brief 手順の表の monkeyMoves を読む */
    void LoadMonkeyMoves(const nlohmann::json& moves);
    /** @brief monkey の手順を1フレーム進める（技を重みつきで選んで押し、左右の向きも時々変える。終わったら true） */
    bool UpdateMonkey(const Step& step);
    /** @brief monkey で次の技を選ぶ（step の moveNames に絞る） */
    void PickMonkeyMove(const Step& step);

    engine::Input* input_ = nullptr;
    engine::DirectXCommon* dxCommon_ = nullptr; ///< 描画の基盤（見張りと画面の保存に使う。借りもの）
    nlohmann::json script_; ///< 読んだ手順の表の中身（再生ファイルに入れる）
    SmokeTestReplay replay_; ///< 再生ファイルの読み書きと状態の指紋
    SmokeTestMonitors monitors_; ///< ゲームの中身とは別の見張り
    std::string runTag_; ///< この回の印（始めた時刻。保存する画面のフォルダ名に使う）
    std::string replayPath_; ///< 読んだ再生ファイル（再生でなければ空）
    uint64_t replayTargetFrame_ = 0; ///< 再生で見せたいフレーム
    uint64_t replayLeadFrames_ = 0; ///< 見せたいフレームのどれだけ手前からふつうの速さにするか
    uint64_t replayAfterFrames_ = 0; ///< 見せたいフレームの後、どれだけ見せてから終えるか
    bool replayRealtime_ = false; ///< 再生で、ふつうの速さに切り替えたか
    bool replayReachedTarget_ = false; ///< 再生で、見せたいフレームまで来たか
    std::vector<MonkeyMove> monkeyMoves_; ///< でたらめ操作の技の表
    int monkeyMoveIndex_ = -1; ///< 今出している技（-1 なら選び直す）
    size_t monkeySequenceIndex_ = 0; ///< 今の技の何組目を押しているか
    int monkeySequenceFrame_ = 0; ///< 今の組を押し始めてからのフレーム数
    uint8_t monkeyDirectionKey_ = 0; ///< でたらめ操作で押し続けている左右のキー（0 なら押していない）
    int monkeyDirectionFrames_ = 0; ///< 左右の向きを次に変えるまでのフレーム数
    size_t grantRotation_ = 0; ///< grantBossWeapon の weapon が next の時に、次に渡す武器の一覧の番号
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
    static inline std::mutex sIssueMutex; ///< おかしな所の記録の鍵（デバッグレイヤーの知らせは描画を記録するスレッドから届くことがある）
    static inline SmokeTestMonitors* sMonitors = nullptr; ///< 初めて見つけた時に画面を保存する見張り（動いている間だけ）
    static inline uint32_t sStartSeed = 0; ///< ApplySeedFromCommandLine で決めた元の種
    static inline std::thread::id sMainThread; ///< ゲームを進めているスレッド（画面の保存はここからだけ頼む）
    bool finished_ = false;
    bool bugHunt_ = false; ///< --bug-hunt で起動したか（変えた所だけ遊ぶ仕組みを使う）
    BugHuntChanges changes_; ///< 前回の記録から変わった所
    bool skippingStage_ = false; ///< 変えていないステージを飛ばしている途中か（ステージの中の手順と、ステージに入るのを待つ手順を飛ばす）
    std::vector<VisitTarget> visitTargets_; ///< 今の visitChanged で近くへ移って試す配置物
    std::string visitLabel_; ///< 今試している配置物（記録に添える）
    int visitedCount_ = 0; ///< 近くへ移って試した配置物の数（報告に出す）
    int maxVisitsPerStage_ = 0; ///< 1つのステージで試す配置物の数の上限（手順の表の maxVisitsPerStage）
    float visitOffset_ = 0.0f; ///< 配置物からどれだけ左に移るか（手順の表の visitOffset）
    float visitRaise_ = 0.0f; ///< 配置物からどれだけ上に移るか（手順の表の visitRaise）
    std::vector<std::string> visitSkipKinds_; ///< 近くで試さない種類（手順の表の visitSkipKinds）
    static inline uint32_t sTeleportCount = 0; ///< プレイヤーを離れた所へ移した回数
};

} // namespace engine::game

/**
 * @file SmokeTestGame.h
 * @brief ゲーム固有の自動テスト処理の窓口
 * @note 別のゲームで使う手順
 * - SmokeTestGame を継承し、設定、画面の問い合わせと切り替え、状態の指紋を実装する。専用の手順は UpdateAction で扱う。
 * - 状態の指紋は SmokeTestFingerprint に画面の名前や体力・位置などを毎回同じ順で混ぜて作る。
 * - ゲーム本体で進行役と実装を所有し、初期化の最初に ApplySeedFromCommandLine、入力の準備後に StartIfRequested を呼ぶ。
 * - 更新の前後で BeginUpdate と Update を呼び、Update が真なら終了する。速回しは GetUpdatesPerDraw と WantsRealtime に従う。
 * - 手順の表は steps 配列に並べる。例として {"action":"wait","seconds":1} は1秒待つ。専用の欄は raw から読む。
 * - 不具合は SmokeTestRunner::ReportIssue で知らせ、網羅の候補は testCoverage::Declare、実際に試した物は Cover で記録する。
 */
#pragma once
#include "BugHuntChanges.h"
#include "SmokeTestReplay.h"
#include "SmokeTestStep.h"
#include <ostream>

namespace engine {

/** @brief ゲームが用意する手順の場所とステージの画面名 */
struct SmokeTestConfig {
    std::string defaultScriptPath; ///< --smoke-test の時に読む手順の表の場所
    std::string bugHuntScriptPath; ///< --bug-hunt の時に読む手順の表の場所（--script で別の場所も指定できる）
    std::string stageScene; ///< ステージを遊ぶ画面の名前（sameStage の手順と、ステージに入るのを待つ手順の目安）
};

/** @brief 自動テスト中にゲームへ問い合わせる処理 */
class SmokeTestGame {
public:
    /** @brief ゲーム側の実装を破棄する */
    virtual ~SmokeTestGame() = default;
    /** @brief 設定を返す */
    virtual SmokeTestConfig GetConfig() const = 0;
    /** @brief 現在の画面の名前を返す */
    virtual const std::string& GetCurrentSceneName() const = 0;
    /** @brief 画面の切り替え中かを返す */
    virtual bool IsChangingScene() const = 0;
    /** @brief 画面を切り替えた回数を返す */
    virtual uint32_t GetSceneSwitchCount() const = 0;
    /** @brief 画面の切り替えを頼む */
    virtual void ChangeScene(const std::string& scene) = 0;
    /** @brief ゲーム固有の設定を読む */
    virtual void LoadSettings(const nlohmann::json&) {}
    /** @brief 自動テスト開始時の準備をする */
    virtual void OnStart() {}
    /** @brief 画面が落ち着いたかを調べる前にゲームの状態を整える */
    virtual void OnFrame() {}
    /** @brief 手順を進める直前にゲームの入力を補う */
    virtual void BeforeAction() {}
    /** @brief 各フレームの手順を進めた直後にゲームの状態を確認する */
    virtual void AfterAction(const SmokeTestStep&) {}
    /** @brief 手順終了時に残った要求を片付ける */
    virtual void OnStepFinished() {}
    /** @brief ゲーム固有の手順なら真を返し、完了したかを done に入れる */
    virtual bool UpdateAction(const SmokeTestStep&, bool&) { return false; }
    /** @brief 状態の指紋を作る */
    virtual SmokeTestReplay::Checkpoint Capture(uint64_t frame) const = 0;
    /** @brief 前回の記録と比べるステージの一覧を返す */
    virtual std::vector<BugHuntChanges::Stage> GetStages() const { return {}; }
    /** @brief 見張りに渡すステージの名前を返す */
    virtual std::string GetStageLabel() const { return {}; }
    /** @brief 不具合の記録に添える様子を返す */
    virtual std::string GetContextSuffix(const SmokeTestStep&) const { return {}; }
    /** @brief 変更のまとめにゲーム固有の結果を書き足す */
    virtual void WriteChangesReport(std::ostream&) const {}
};

} // namespace engine

/**
 * @file SmokeTestMonitors.h
 * @brief 自動テストプレイの間、ゲームの中身とは別の見えにくい不具合（D3D12 の警告・重い更新・メモリの増え方・プールの溢れ）を見張る部品を定義するファイル
 * @note 見つけた物は SmokeTestRunner::ReportIssue で知らせ、まとめは報告の最後に書く。
 *       おかしな所が初めて見つかった時は、その時の画面を PNG で保存する
 */
#pragma once
#include <chrono>
#include <cstdint>
#include <map>
#include <ostream>
#include <string>
#include <vector>

namespace engine {
class DirectXCommon;
}

namespace engine {

/** @brief 自動テストプレイの見張り（ゲームの中身に関係しない物） */
class SmokeTestMonitors {
public:
    /** @brief 見張りの決め事（手順の表で変えられる） */
    struct Settings {
        float updateSpikeMs = 100.0f; ///< 1回の更新がこれ以上かかったら重い更新として知らせる（Debug ビルドは遅いので大きめ）
        float slowUpdateMs = 16.7f; ///< これ以上かかった更新を、まとめで処理落ちとして数える
        float memoryGrowthMb = 300.0f; ///< ステージに入るたびに測るメモリが、最初に入った時からこれ以上増えたら知らせる
        int maxScreenshots = 30; ///< 1回の自動テストで保存する画面の上限
    };

    /**
     * @brief 見張りを始める（デバッグレイヤーの受け口を登録し、エラーで止めないようにする）
     * @param dxCommon 描画の基盤（画面の保存と GPU のメモリの量に使う。nullptr ならその2つはしない）
     * @param settings 決め事
     * @param runTag 保存する画面のフォルダ名に使う、この回の印（時刻など）
     */
    void Start(DirectXCommon* dxCommon, const Settings& settings, const std::string& runTag);
    /** @brief 見張りをやめる（デバッグレイヤーの受け口を外し、エラーで止める設定に戻す） */
    void Stop();

    /** @brief 1回の更新を始める（ゲームを1回進める前に呼ぶ） */
    void BeginUpdate();
    /**
     * @brief 1回の更新を終える（かかった時間を数え、重すぎたら知らせる）
     * @param settled 画面の切り替えが落ち着いているか（読み込み中は重くて当然なので数えない）
     * @param scene 今の画面の名前
     */
    void EndUpdate(bool settled, const std::string& scene);
    /**
     * @brief ステージに入って落ち着いた時に、メモリの量を測る（最初に入った時より増え続けていたら知らせる）
     * @param label ステージの名前（報告に出す）
     */
    void OnStageEntered(const std::string& label);
    /** @brief プールが溢れた回数を見て、初めて溢れたプールを知らせる（毎フレーム呼んでよい。重い処理は間を空ける） */
    void CheckPools(uint64_t frame);
    /**
     * @brief おかしな所が初めて見つかった時に、その時の画面を保存するよう頼む
     * @param index 何件目のおかしな所か（ファイル名に使う）
     * @return 保存する場所（上限を超えた・保存できない時は空）
     */
    std::string RequestScreenshot(size_t index);
    /** @brief 報告の最後に、重さ・メモリ・プールのまとめを書く */
    void WriteSummary(std::ostream& out) const;

private:
    /** @brief 画面ごとの更新の重さのまとめ */
    struct UpdateStats {
        uint64_t count = 0; ///< 数えた更新の回数
        uint64_t slowCount = 0; ///< slowUpdateMs 以上かかった回数
        double totalMs = 0.0; ///< 合計の時間
        float maxMs = 0.0f; ///< 一番かかった時間
    };
    /** @brief ステージに入った時に測ったメモリの量 */
    struct MemorySample {
        std::string label; ///< ステージの名前
        double cpuMb = 0.0; ///< このアプリが使っているメモリ（MB）
        double gpuMb = 0.0; ///< このアプリが使っている GPU のメモリ（MB）
    };
    /** @brief 今のメモリの量を測る */
    MemorySample Measure(const std::string& label) const;

    DirectXCommon* dxCommon_ = nullptr; ///< 描画の基盤（借りもの）
    Settings settings_; ///< 決め事
    std::string screenshotFolder_; ///< 画面を保存するフォルダ
    int screenshotCount_ = 0; ///< 保存を頼んだ画面の数
    std::chrono::steady_clock::time_point updateStart_; ///< 今の更新を始めた時刻
    bool updating_ = false; ///< 更新の途中か
    std::map<std::string, UpdateStats> updateStats_; ///< 画面の名前 → 更新の重さのまとめ
    std::vector<MemorySample> memorySamples_; ///< ステージに入るたびに測ったメモリの量
    bool cpuGrowthReported_ = false; ///< メモリの増え方をもう知らせたか
    bool gpuGrowthReported_ = false; ///< GPU のメモリの増え方をもう知らせたか
    std::map<std::string, uint64_t> reportedPools_; ///< もう知らせたプール → 知らせた時の回数
    bool running_ = false; ///< 見張っているか
};

} // namespace engine

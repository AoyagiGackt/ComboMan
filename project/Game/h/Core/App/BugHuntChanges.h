/**
 * @file BugHuntChanges.h
 * @brief バグ探しで、前回の記録から変わった所を調べる（変えたステージだけを遊び、変えた配置物を近くで試すため）
 */
#pragma once
#include "json.hpp"
#include <map>
#include <string>
#include <vector>

namespace engine::game {

/**
 * @brief 前回のバグ探しの記録と今のファイルを比べ、遊ぶステージと近くで試す配置物を決める
 * @note 記録は smoke_test/bug_hunt_state.json。最後まで回った時だけ、今の様子を bug_hunt_state_next.json へ書き、
 *       起動役（BugHunt）が全部の回を終えてから置き換える（何回も続けて回した時に2回目から何も回らなくならないように）
 */
class BugHuntChanges {
public:
    /** @brief 遊ぶ候補のステージ1つ */
    struct Stage {
        std::string levelPath; ///< レベルの JSON の場所
        std::string label; ///< 報告に出す名前（チュートリアル・ステージ 1・ラスボス戦 など）
    };

    /**
     * @brief 今のファイルの様子を集め、前回の記録と比べて遊ぶステージを決める
     * @param stages 遊ぶ候補のステージ
     * @param forceAll 記録に関係なく全部遊ぶか（起動の --all）
     * @param scriptPath 手順の表（変わったら全部遊ぶ）
     */
    void Prepare(const std::vector<Stage>& stages, bool forceAll, const std::string& scriptPath);

    /** @brief このステージを遊ぶか（Prepare していなければ全部遊ぶ） */
    bool IsStageSelected(const std::string& levelPath) const;
    /** @brief 前回から増えた・変わった配置物の名前（前回の記録にこのステージが無ければ空） */
    std::vector<std::string> ChangedObjects(const std::string& levelPath) const;
    /** @brief 報告の 回した所 の行に出す説明 */
    std::string Summary() const;
    /** @brief 今のファイルの様子を次の記録として書き出す（最後まで回った時に呼ぶ） */
    void WriteNextState() const;

private:
    /** @brief ステージ1つ分の今の様子 */
    struct StageState {
        std::string files; ///< レベルとそのグラフの更新時刻と大きさをまとめた印
        std::map<std::string, std::string> objects; ///< 配置物の名前 → 中身の印
    };

    /** @brief レベルの JSON とそのグラフから、ステージ1つ分の様子を作る */
    static StageState ReadStage(const std::string& levelPath);
    /** @brief 全部のステージに効くファイル（exe・設定・シェーダー・手順の表）の印を、場所ごとに集める */
    static std::map<std::string, std::string> ReadGlobalFiles(const std::string& scriptPath);
    /** @brief 今の様子を記録の JSON にする */
    nlohmann::json ToJson() const;

    bool prepared_ = false; ///< Prepare したか
    std::vector<Stage> stages_; ///< 遊ぶ候補のステージ
    std::map<std::string, StageState> current_; ///< 今の様子（レベルの場所ごと）
    std::map<std::string, std::string> currentGlobal_; ///< 全部に効くファイルの今の印
    nlohmann::json previous_; ///< 前回の記録（無ければ null）
    std::vector<std::string> selected_; ///< 遊ぶステージのレベルの場所
    bool selectAll_ = true; ///< 全部遊ぶか
    std::string allReason_; ///< 全部遊ぶ理由（報告に出す）
};

} // namespace engine::game

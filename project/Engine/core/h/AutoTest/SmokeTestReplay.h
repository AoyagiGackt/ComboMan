/**
 * @file SmokeTestReplay.h
 * @brief 自動テストプレイの再生ファイル（見つけた不具合をもう一度起こすための記録）を読み書きする部品を定義するファイル
 * @note 自動テストの間は本物の入力を切り、乱数の種もすべて1つの種から作るので、同じ手順の表と種で回せば同じ動きになる。
 *       再生ファイルには、手順の表・種・回したステージと、決まった間隔ごとのゲームの状態の指紋を入れる。
 *       再生の時は指紋を比べて、記録した時と動きがずれていないかを確かめる
 */
#pragma once
#include "json.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace engine {
class SmokeTestGame;

/** @brief 再生ファイルに残す、見つけたおかしな所1件 */
struct ReplayIssue {
    std::string kind; ///< 種類
    std::string subject; ///< 相手
    uint64_t frame = 0; ///< 最初に見つけたフレーム
};

/** @brief 再生ファイルの中身（記録する時も読み込んだ時もこの形） */
struct ReplayData {
    std::string scriptPath; ///< 手順の表の場所（報告に出すだけ。中身は script を使う）
    nlohmann::json script; ///< 手順の表の中身（あとで表を書き換えても、記録した時と同じ手順で回せる）
    uint32_t seed = 0; ///< 乱数の元の種
    bool bugHunt = false; ///< --bug-hunt で回したか
    nlohmann::json selection; ///< 回したステージと近くで試した配置物（BugHuntChanges::ExportSelection）
    std::vector<ReplayIssue> issues; ///< 見つけたおかしな所
    std::string failReason; ///< 途中で止まった時の理由（最後まで回ったら空）
    uint64_t failFrame = 0; ///< 止まったフレーム
    uint64_t totalFrames = 0; ///< 回したフレーム数
};

/**
 * @brief 再生ファイルの読み書きと、ゲームの状態の指紋
 */
class SmokeTestReplay {
public:
    static constexpr uint64_t kCheckpointIntervalFrames = 60; ///< 状態の指紋を取る間隔（約1秒）

    /**
     * @brief 決まった間隔のフレームなら、今のゲームの状態の指紋を取って残す（記録の時に毎フレーム呼ぶ）
     * @param frame 今のフレーム
     */
    void RecordCheckpoint(uint64_t frame, const SmokeTestGame& game);
    /**
     * @brief 再生ファイルを書き出す
     * @param path 書き出す場所
     * @param data 中身（指紋は RecordCheckpoint で残した物を足す）
     * @return 書けたら true
     */
    bool Save(const std::string& path, const ReplayData& data) const;

    /**
     * @brief 再生ファイルを読み込み、再生の準備をする
     * @param path 読む場所
     * @return 読めたら true
     */
    bool Load(const std::string& path);
    /** @brief 再生しているか */
    bool IsPlaying() const {
        return playing_;
    }
    /** @brief 読み込んだ中身 */
    const ReplayData& Data() const {
        return loaded_;
    }
    /** @brief 見せたいフレーム（最初に見つけたおかしな所か、止まったフレーム。どちらも無ければ 0） */
    uint64_t TargetFrame() const;
    /**
     * @brief 決まった間隔のフレームなら、今の状態の指紋を記録した物と比べる（再生の時に毎フレーム呼ぶ）
     * @param frame 今のフレーム
     * @return 初めてずれたフレームなら、ずれの様子（ずれていなければ空。一度ずれたら後は比べない）
     */
    std::optional<std::string> VerifyCheckpoint(uint64_t frame, const SmokeTestGame& game);

    /** @brief 状態の指紋1つ */
    struct Checkpoint {
        uint64_t frame = 0; ///< 取ったフレーム
        uint64_t hash = 0; ///< 指紋
        std::string summary; ///< 人が読む短い様子（ずれた時に、記録した時と今を並べて出す）
    };

private:
    std::vector<Checkpoint> recorded_; ///< 記録の時に取った指紋
    ReplayData loaded_; ///< 読み込んだ中身
    std::vector<Checkpoint> expected_; ///< 読み込んだ指紋（再生の時に比べる）
    size_t nextExpected_ = 0; ///< 次に比べる指紋の番号
    bool playing_ = false; ///< 再生しているか
    bool diverged_ = false; ///< 一度ずれたか（ずれた後は比べない）
};

} // namespace engine

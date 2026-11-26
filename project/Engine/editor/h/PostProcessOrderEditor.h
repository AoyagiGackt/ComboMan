/**
 * @file PostProcessOrderEditor.h
 * @brief 画面全体へ掛けるエフェクトの順番と有効・無効を、場面ごとに並べ替えて保存するエディタのファイル
 */
#pragma once
#include "json.hpp"
#include <string>
namespace engine::editor {

/**
 * @brief post_process.json の stacks を編集するエディタ
 * @note 変えた順番はその場で PostProcessStack::SetConfigOverride でゲームに効かせ、保存で設定ファイルに書く。
 *       元に戻すと設定ファイルの中身へ戻る
 */
class PostProcessOrderEditor {
public:
    /**
     * @brief 開いている間だけ画面を描く（Update の中の ImGui の区切りから呼ぶ）
     * @param open 開いているか（閉じるボタンで false になる）
     */
    void Draw(bool* open);

private:
    /** @brief 設定ファイルを読み直し、編集中の中身を捨てる */
    void Reload();
    /** @brief 1つの場面の列を描く（変えたら true） */
    bool DrawStack(const std::string& stackName, nlohmann::json& stack);
    /** @brief 編集中の中身をゲームに効かせる */
    void ApplyPreview();
    /** @brief 編集中の中身を設定ファイルに書く */
    void Save();

    bool loaded_ = false; ///< 設定ファイルを読んだか
    nlohmann::json saved_; ///< 設定ファイルの中身（保存してあるもの）
    nlohmann::json edited_; ///< 編集中の中身
    std::string status_; ///< 保存の結果などの知らせ
};

} // namespace engine::editor

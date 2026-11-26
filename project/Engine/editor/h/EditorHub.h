/**
 * @file EditorHub.h
 * @brief 今のシーンで使えるエディタを一覧にし、キーを覚えなくてもボタンで開け閉めできるようにする入口
 */
#pragma once
#ifdef USE_IMGUI
#include <string>
#include <unordered_set>
#include <vector>

namespace engine::editor {

/**
 * @brief エディタの一覧と開閉の頼みを受け持つ
 * @note 各エディタは毎フレーム Offer で自分の名前と開閉の状態を知らせ、戻り値が true なら開閉を切り替える。
 *       一覧で押したボタンは次のフレームの Offer で受け取るので、呼ぶ順番を気にしなくてよい。
 *       知らせが無かったエディタ（今のシーンに無い物）は一覧に出ない
 */
class EditorHub {
public:
    /** @brief 一覧の1行 */
    struct Entry {
        std::string id; ///< 開閉の頼みを見分ける名前
        std::string title; ///< 一覧に出す名前
        std::string key; ///< 開閉のキーの表示（無ければ空）
        std::string help; ///< 何をするエディタかの説明
        bool open = false; ///< 今開いているか
    };

    static EditorHub* GetInstance();

    /**
     * @brief このフレームの一覧に自分を載せる
     * @return 一覧で開閉のボタンが押されていたら true（呼び出し側で開閉を切り替える）
     */
    bool Offer(const char* id, const char* title, const char* key, const char* help, bool open);

    /** @brief このフレームに知らされたエディタの一覧をボタン付きで描く（ImGui のウィンドウの中で呼ぶ） */
    void DrawList();

    /** @brief フレームの最後に呼び、一覧を空にする（次のフレームでまた知らせてもらう） */
    void EndFrame();

private:
    std::vector<Entry> entries_; ///< このフレームに知らされたエディタ
    std::unordered_set<std::string> pendingToggles_; ///< 前のフレームに一覧で押され、このフレームの Offer で受け取る開閉の頼み
    std::unordered_set<std::string> nextToggles_; ///< このフレームに一覧で押され、次のフレームに渡す開閉の頼み
};

} // namespace engine::editor
#endif // USE_IMGUI

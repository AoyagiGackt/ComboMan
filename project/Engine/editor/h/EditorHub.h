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
 *       知らせが無かったエディタ（今のシーンに無い物）は、前に知らせがあった場面の名前と一緒に、使えない物として薄く出す
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

    /**
     * @brief 今の場面の名前を知らせる（フレームの最初に呼ぶ）
     * @param placeLabel 一覧で、ほかの場面で使えるエディタの横に出す名前（本編・トレーニングなど）
     */
    void SetPlaceLabel(const std::string& placeLabel) {
        placeLabel_ = placeLabel;
    }

private:
    /** @brief 前に知らせがあったエディタと、使えた場面 */
    struct KnownEntry {
        std::string id; ///< 開閉の頼みを見分ける名前
        std::string title; ///< 一覧に出す名前
        std::vector<std::string> places; ///< 知らせがあった場面の名前（知らせがあった順）
    };
    /** @brief 今の場面で使えないエディタを、使える場面と一緒に薄く描く */
    void DrawUnavailableList() const;

    std::vector<Entry> entries_; ///< このフレームに知らされたエディタ
    std::vector<KnownEntry> known_; ///< これまでに知らせがあったエディタ（知らせがあった順）
    std::string placeLabel_; ///< 今の場面の名前
    std::unordered_set<std::string> pendingToggles_; ///< 前のフレームに一覧で押され、このフレームの Offer で受け取る開閉の頼み
    std::unordered_set<std::string> nextToggles_; ///< このフレームに一覧で押され、次のフレームに渡す開閉の頼み
};

} // namespace engine::editor
#endif // USE_IMGUI

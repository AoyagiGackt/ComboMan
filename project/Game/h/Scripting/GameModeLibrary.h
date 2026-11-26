/**
 * @file GameModeLibrary.h
 * @brief ゲームの型（横スクロール・奥へ進む・道に沿って進む など）を Resources/Config/GameModes/*.json から読む表
 * @note レベル JSON の gameMode に型の id を書くと、カメラの付き方・道に沿った自動の進み方・エディタの点検が変わる
 */
#pragma once
#include "CameraRig.h"
#include <string>
#include <vector>

namespace engine::game {

/** @brief 道に沿って自動で進む時の決まり */
struct GameModePathRules {
    bool follow = false; ///< レベルの道に沿って自動で進むか
    float playerHalfWidth = 8.0f; ///< 道の上の今の位置から、プレイヤーが横に離れてよい幅
    float playerHalfHeight = 6.0f; ///< 道の上の今の位置から、プレイヤーが上下に離れてよい幅
    std::string endFlag = "path_end"; ///< 道の終わりに着いた時に立てるフラグ（ノードグラフで受け取れる）
};

/** @brief プレイヤーを奥行き方向にも動かす時の決まり */
struct GameModePlayerRules {
    bool depthMovement = false; ///< 前後のキーで先へ進み、左右のキーで奥行き方向へ動くか
    float depthSpeed = 6.0f; ///< 奥行き方向へ動く速さ（ワールド単位/秒）
    float depthMin = -4.0f; ///< 奥行きの動ける範囲の手前の端（z）
    float depthMax = 4.0f; ///< 奥行きの動ける範囲の奥の端（z）
    bool enemiesFollowDepth = true; ///< 敵がプレイヤーと同じ奥行きへ寄ってくるか
    float enemyDepthSpeed = 3.0f; ///< 敵が奥行きを合わせる速さ（ワールド単位/秒）
    float hitHalfDepth = 1.0f; ///< 当たりの奥行きの半分の幅（この差より奥行きが離れていると当たらない）
};

/** @brief 型の操作の表の1行（キーは {Jump} のような操作名で書き、出す時にその時のキーの名前へ置き換える） */
struct GameModeControl {
    std::string keys; ///< 操作名（例 {ArenaForward}{ArenaBack}）
    std::string label; ///< 何ができるか（例 奥へ進む・下がる）
};

/** @brief 型の点検や部品の棚に使う配置物の種類と、その表示名 */
struct GameModeKind {
    std::string kind; ///< レベル JSON の kind に書く種類名
    std::string label; ///< エディタに出す名前（書いていなければ種類名）
};

/** @brief ゲームの型1つぶんの設定 */
struct GameModeDesc {
    std::string id; ///< レベル JSON の gameMode に書く名前
    std::string name; ///< エディタに出す名前
    std::string description; ///< エディタに出す説明
    std::string cameraType = "side"; ///< カメラの部品（"side" か "behind"）
    bool useStageBoundsCamera = true; ///< 横からの時、今までどおりステージの端でカメラを止めるか
    engine::graphics::CameraRigSettings camera; ///< カメラの距離など
    GameModePathRules path; ///< 道に沿って進む時の決まり
    GameModePlayerRules player; ///< プレイヤーの動き方の決まり
    std::vector<GameModeKind> requiredKinds; ///< この型のステージに必ず置く配置物の種類（エディタの点検に使う）
    std::vector<GameModeKind> recommendedKinds; ///< 部品の棚で先に並べる配置物の種類
    std::vector<std::string> tips; ///< エディタに出す作り方のこつ
    std::vector<GameModeControl> controls; ///< この型の操作の表（空なら今までの操作説明のまま）
};

/**
 * @brief ゲームの型の一覧を持つ
 * @note 最初に使った時にフォルダを読み、Reload で読み直す。知らない id や空の id は既定の型（kDefaultId）を返す
 */
class GameModeLibrary {
public:
    static constexpr const char* kDefaultId = "side_action"; ///< gameMode を書いていないレベルの型
    static constexpr const char* kGameOverRequestFlag = "game_over_request"; ///< ノードがゲームオーバーを頼む時に立てるフラグ

    static GameModeLibrary* GetInstance();

    /** @brief id の型を返す（無ければ既定の型。既定の型のファイルも無ければ組み込みの横スクロール） */
    const GameModeDesc& Find(const std::string& id) const;
    /** @brief 読み込んだ型の一覧（ファイル名の順） */
    const std::vector<GameModeDesc>& All() const {
        return modes_;
    }
    /** @brief フォルダを読み直す */
    void Reload();
    /**
     * @brief 型を Resources/Config/GameModes/<id>.json に書き、一覧のファイルにも足してから読み直す
     * @return 書けたら true
     */
    bool Save(const GameModeDesc& mode);
    /** @brief この id の型がもうあるか */
    bool Contains(const std::string& id) const;

private:
    GameModeLibrary();

    std::vector<GameModeDesc> modes_; ///< 読み込んだ型
    GameModeDesc fallback_; ///< ファイルが1つも無い時に使う横スクロールの型
};

} // namespace engine::game

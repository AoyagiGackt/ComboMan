/**
 * @file GamePlayBugWatcher.h
 * @brief 自動テストプレイの間だけ、遊んでいる様子からおかしな所を探して SmokeTestRunner へ知らせる見張りを定義するファイル
 */
#pragma once
#include "Collision.h"
#include "GamePlaySceneTypes.h"
#include "Vector3.h"
#include <string>
#include <vector>

namespace engine::game {

class AllyBoss;
class EnemyEntity;
class Player;
class StageEditor;

/**
 * @brief 見張りが1回調べるのに使う、シーンの今の様子（すべて借りもの）
 */
struct BugWatchContext {
    const Player* player = nullptr; ///< プレイヤー
    const EnemyEntity* boss = nullptr; ///< ステージボス（いなければ nullptr）
    const AllyBoss* ally = nullptr; ///< 味方のボス
    const StageEditor* editor = nullptr; ///< 地形・敵・扉の条件を持つエディタ
    bool executionActive = false; ///< ボスへのとどめの演出の最中か
    bool finalBossMode = false; ///< ラスボス戦の最中か
};

/**
 * @brief 自動テストプレイの見張り
 * @note 見つけても止めずに記録だけする。一瞬だけの様子（吹き飛んで壁をかすめた等）は数えず、続いた時だけ知らせる。
 *       自動テストプレイでない時は何もしない。
 */
class GamePlayBugWatcher {
public:
    /**
     * @brief 決めた間隔ごとに、プレイヤー・武器・味方・敵・扉の条件を調べる（毎フレーム呼ぶ）
     * @param context シーンの今の様子
     */
    void Run(const BugWatchContext& context);
    /**
     * @brief 床抜けを直前の様子と一緒に知らせる（自動テストプレイの時だけ）
     * @param position 落ちていた位置
     * @param lastSafeGround 最後に立っていた床
     */
    void ReportPlayerFall(const Vector3& position, const Vector3& lastSafeGround) const;
    /** @brief ステージを読み直した時に、前のステージの様子を忘れる */
    void ResetForLevel();

private:
    /** @brief プレイヤーの今の様子を直前の記録に足す */
    void RecordPlayerTrail(const BugWatchContext& context, const Vector3& position);
    /** @brief 直前の様子の記録を、報告に添える1行の文にする */
    std::string FormatPlayerTrail() const;
    /** @brief プレイヤーの位置・体力・先へ進めるかを調べる */
    void CheckPlayer(const BugWatchContext& context, const AABB& stageBounds, bool hasBounds, const std::vector<AABB>& terrain, float elapsed);
    /** @brief 武器の持ち方におかしな所が無いか調べる（選ぶ画面が出たまま・合体の番号が範囲の外など） */
    void CheckWeapons(float elapsed);
    /** @brief 味方のボスにおかしな所が無いか調べる（離れすぎたまま・落ちたまま・体力が範囲の外） */
    void CheckAlly(const BugWatchContext& context, float elapsed);
    /** @brief 生きている敵が届かない所にいないか・生き返っていないかを調べる */
    void CheckEnemies(const BugWatchContext& context, const AABB& stageBounds, bool hasBounds, const std::vector<AABB>& terrain, float elapsed);
    /** @brief 敵グループの全滅を待つ条件（扉など）が、倒し切ったのに成立しない所を探す */
    void CheckEnemyGroupConditions(const BugWatchContext& context, const AABB& stageBounds, bool hasBounds, const std::vector<AABB>& terrain, float elapsed);

    gamePlayScene::BugWatchState state_; ///< フレームをまたいで覚えておく様子
};

} // namespace engine::game

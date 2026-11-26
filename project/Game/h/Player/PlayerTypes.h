/// @file PlayerTypes.h
/// @brief プレイヤーの見た目と動きの状態をまとめた構造体（リグ・ダッシュ・回避・ワープ・覚醒・乱舞・フィニッシャー）を定義するファイル
/// @note Player の宣言を短く保つため外へ出してある。Player の中では同じ名前の別名やメンバとして使う
#pragma once
#include "Animation.h"
#include "Model.h"
#include "SkinnedModel.h"
#include "SkinnedObject3d.h"
#include "Vector3.h"
#include "Weapon.h"
#include <memory>

namespace engine::game::playerTypes {
using engine::graphics::Model;
using engine::graphics::SkinnedModel;
using engine::graphics::SkinnedObject3d;

/** @brief 投げて手放した武器の飛行状態 */
struct WeaponThrow {
    WeaponType type {};
    Vector3 position {};
    float direction = 1.0f;
    float secondsLeft = 0.0f;
    float spin = 0.0f;
    float damage = 0.0f;
    float remainingDistance = 0.0f;
    float previousX = 0.0f; ///< このフレームに飛び始めた横位置
};

/** @brief 固有技で共通の水平ダッシュ（開始位置から目標位置へ短時間でなめらかに動く。当たりは動き終えたフレームで出す） */
struct DashMotion {
    bool active = false; ///< ダッシュ中か
    float timer = 0.0f; ///< 始めてからの秒数
    float startX = 0.0f; ///< 始めた横の位置
    float targetX = 0.0f; ///< 向かう横の位置
};

/** @brief 通常と覚醒の見た目1体ぶんのモデル・アニメーション・武器を付ける骨の名前一式（定義は CharacterVisuals.h の kNormalRigVisual / kAwakenedRigVisual） */
struct CharacterRig {
    Model* staticModel = nullptr; ///< 残像・分身演出用（ボーンなし、本体と同じ見た目）。実体はModelManagerが所有・共有する
    SkinnedModel* skinnedModel = nullptr; ///< 本体描画（ボーンアニメーション付き）。実体はModelManagerが所有・共有する
    std::unique_ptr<SkinnedObject3d> object; ///< 描く実体
    float modelScale = 1.0f; ///< モデルの大きさ
    float modelOffsetY = 0.0f; ///< モデル原点（足元）を AABB 中心の pos_ に合わせる下げ幅
    const char* meleeBoneName = ""; ///< 近接武器のアタッチ先ボーン
    const char* gunBoneName = ""; ///< 銃のアタッチ先ボーン
    const char* motionRigId = ""; ///< 加算モーションの保存キー（リグのモデルファイル名）
    engine::graphics::Animation idleAnim; ///< 待機
    engine::graphics::Animation runAnim; ///< 走り
    engine::graphics::Animation jumpAnim; ///< ジャンプ
    engine::graphics::Animation runningJumpAnim; ///< 移動しながらのジャンプ
    engine::graphics::Animation swimAnim; ///< 水中
    engine::graphics::Animation idleHoldAnim; ///< 武器持ち待機（構え系スタイル）
    engine::graphics::Animation runHoldAnim; ///< 武器持ち走り（構え系スタイル）
    engine::graphics::Animation slashAnim; ///< 斬撃（ソード攻撃・乱舞・フィニッシャー）
    engine::graphics::Animation punchAnim; ///< パンチ（ソード以外のスタイルのコンボ）
    engine::graphics::Animation walkAnim; ///< 歩き（マップを移動する時）
};

/** @brief 回避の状態（前後ステップ。全区間無敵で、コンボから打ち切って出せる） */
struct DodgeState {
    bool active = false; ///< 回避中か
    bool rewardClaimed = false; ///< 1回の回避でジャスト回避報酬を二重に受け取らないためのフラグ
    float timer = 0.0f; ///< 始めてからの秒数
    float startX = 0.0f; ///< 始めた横の位置
    float targetX = 0.0f; ///< 向かう横の位置
    float cooldown = 0.0f; ///< 次に出せるまでの秒数
    float spamTimer = 0.0f; ///< 前回の回避からの経過秒数（連打判定用）
    int spamCount = 0; ///< 続けて連打した回数
    int bufferFrames = 0; ///< 先行入力した回避ボタンの残り有効フレーム数（0なら未入力）
    bool backward = false; ///< 向きを変えずに後ろへ下がる回避か（前転モーションだと滑って見えるので専用の見せ方にする）
    float justDodgeWindowTimer = 0.0f; ///< ジャスト回避直後の強化窓の残り秒数（長さと倍率はCombatTuning側）
};

/** @brief テレポート斬りの状態（向いている方へ短くワープする。全区間無敵） */
struct WarpState {
    bool active = false; ///< ワープ中か
    bool requestedByDodge = false; ///< ロックオン中に敵の方へ倒して回避を押し、回避の代わりにワープを頼んだか
    float timer = 0.0f; ///< 始めてからの秒数
    float startX = 0.0f; ///< 始めた横の位置
    float targetX = 0.0f; ///< 向かう横の位置
    float cooldown = 0.0f; ///< 次に出せるまでの秒数
};

/** @brief 覚醒の状態 */
struct AwakenState {
    float gauge = 0.0f; ///< 覚醒ゲージ
    bool isAwakened = false; ///< 覚醒中か
    float timer = 0.0f; ///< 覚醒してからの秒数（色の脈打ちに使う）
};

/** @brief 覚醒乱舞の途中の値（段階そのものは IRampageState が持つ） */
struct RampageData {
    int juggleSlashCount = 0; ///< 今回の乱舞で何回切ったか
    int juggleAngleIdx = 0; ///< 次のスラッシュ角度インデックス
    float timer = 0.0f; ///< 突進を始めてから、または最後に斬ってからの秒数（着かない・入力が止まった乱舞を打ち切る）
    Vector3 target = { }; ///< 乱舞が向かう相手の位置（SetRampageTarget() で渡された時だけ使う）
    bool hasTarget = false; ///< このフレームの乱舞の相手が渡されているか
};

/** @brief フィニッシャーの溜めの状態（静止して溜めてから一閃） */
struct FinisherState {
    bool charging = false; ///< 静止して溜めている最中か
    float chargeTimer = 0.0f; ///< 残り溜め時間（0以下で解放＝一閃へ）
};

} // namespace engine::game::playerTypes

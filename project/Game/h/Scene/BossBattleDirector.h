/**
 * @file BossBattleDirector.h
 * @brief ステージボス戦の進行（ひるみ値・体力が減った時の第2段階・武器ごとの技）をまとめる
 */
#pragma once
#include "BossMotion.h"
#include "Vector3.h"
#include "Vector4.h"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace engine::game {

class EnemyEntity;

/** @brief ボス1体ぶんの名前と技（boss_battle.json の bosses。武器種別名ごと） */
struct BossProfile {
    std::string name = "ボス"; ///< 体力バーの上に出す名前
    std::vector<std::string> attacks = { "slam" }; ///< ふだん使う技（slam / charge / dash を順に回す）
    std::vector<std::string> rageAttacks = { "slam" }; ///< 第2段階で使う技
};

/** @brief 投げる技1種類ぶんの調整値（boss_battle.json の volleys。槍・斧・鎌などボスごとに中身が違う） */
struct BossVolleyData {
    float telegraphSeconds = 0.6f; ///< 投げる前に身構える秒数
    int count = 3; ///< 一度に投げる数
    float spreadDegrees = 10.0f; ///< 扇状に広げる角度（隣どうしの差）
    float speed = 7.0f; ///< 飛ぶ速さ（山なりの時は横の速さ）
    float riseSpeed = 0.0f; ///< 山なりの時の上向きの速さ
    float gravity = 0.0f; ///< 落ちる加速度（0ならまっすぐ狙って飛ぶ）
    float lifetime = 2.0f; ///< 飛んでいる秒数
    float launchHeight = 0.8f; ///< 足元からこの高さで投げる
    int damage = 2; ///< 当たった時のダメージ
    float recoverSeconds = 0.6f; ///< 投げた後の隙
    std::string warningRing = "boss_charge_warning"; ///< 身構えている間に出す輪
};

/** @brief ボス戦の調整値（boss_battle.json。初期値はJSONに項目が無い時の既定値） */
struct BossBattleTuning {
    // 戦い始め
    float engageRange = 9.0f; ///< プレイヤーがこれより近づいたら戦い始める
    // ひるみ値（ふだんは吹き飛ばず、たまったダメージで大きくひるむ）
    float staggerHpRatio = 0.15f; ///< 最大体力のこの割合のダメージがたまったらひるむ
    float rageStaggerHpRatio = 0.22f; ///< 第2段階でひるむまでに要る割合
    float staggerSeconds = 2.5f; ///< ひるんで吹き飛ばせる長さ（この間は技を出さない）
    std::string staggerImpact = "boss_slam"; ///< ひるんだ瞬間の揺れ
    std::string staggerRing = "boss_slam_outer"; ///< ひるんだ瞬間にボスから広がる輪
    // 第2段階
    float rageHpRatio = 0.5f; ///< 体力がこの割合を切ったら第2段階へ
    float rageSeconds = 1.3f; ///< 咆哮して止まっている長さ
    Vector4 rageColor = { 1.0f, 0.32f, 0.25f, 1.0f }; ///< 第2段階の体の色
    float rageIntervalMult = 0.6f; ///< 第2段階の技の間隔の倍率
    float rageRadiusMult = 1.3f; ///< 第2段階の叩きつけの範囲の倍率
    std::string rageImpact = "boss_roar";
    std::string rageFlash = "boss_roar";
    std::string rageRing = "boss_roar";
    // 技の間隔
    float firstAttackDelay = 1.2f; ///< 名乗りや咆哮の後、最初の技までの長さ
    float attackInterval = 4.5f; ///< 技と技の間隔
    // 突進
    float chargeTelegraphSeconds = 0.8f; ///< 突進の前に身構える長さ
    float chargeSpeed = 16.0f; ///< 突進の速さ
    float chargeDistance = 10.0f; ///< 突進する最大の距離
    float chargeRecoverSeconds = 0.7f; ///< 突進の後の隙
    int chargeDamage = 3;
    std::string chargeWarningRing = "boss_charge_warning"; ///< 身構えている間に出す輪
    std::string chargeTrailSparks = "boss_charge_trail"; ///< 突進中に足元から出す火花
    // 連続ダッシュ
    int dashCount = 3; ///< 続けて跳ぶ回数
    float dashTelegraphSeconds = 0.35f; ///< 1回ごとの身構える長さ
    float dashSeconds = 0.16f; ///< 1回の移動にかける長さ
    float dashOffset = 2.2f; ///< プレイヤーの向こう側へ回り込む距離
    float dashRecoverSeconds = 0.6f; ///< 跳び終えた後の隙
    int dashDamage = 2;
    std::string dashWarningRing = "boss_dash_warning";
    std::string dashTrailSparks = "boss_dash_trail";
    // 地面を走る衝撃波（左右へ1つずつ。ジャンプでかわせる）
    float waveTelegraphSeconds = 0.7f; ///< 叩く前に身構える秒数
    float waveSpeed = 7.0f; ///< 衝撃波が走る速さ
    float waveDistance = 9.0f; ///< 衝撃波が走る最大の距離
    float waveHeight = 0.9f; ///< 足元からこの高さまでに当たる
    float waveHalfWidth = 0.5f; ///< 衝撃波の横幅の半分
    int waveDamage = 2;
    float waveRecoverSeconds = 0.8f; ///< 叩いた後の隙
    std::string waveWarningRing = "slam_warning";
    std::string waveSparks = "gate_dust"; ///< 衝撃波が走る所に出す土煙
    // 跳びかかり（プレイヤーの所へ跳び、着地の周りにダメージ）
    float leapTelegraphSeconds = 0.6f; ///< 跳ぶ前に身構える秒数
    float leapSeconds = 0.7f; ///< 跳んでいる秒数
    float leapHeight = 4.0f; ///< 跳ぶ高さ
    float leapRadius = 2.2f; ///< 着地の周りでダメージを受ける半径
    int leapDamage = 3;
    float leapRecoverSeconds = 0.8f; ///< 着地した後の隙
    std::string leapWarningRing = "boss_dash_warning";
    // 叩きつけ（予告の長さ・範囲・ダメージは enemy_params.json の bossSlam）
    float slamRecoverSeconds = 0.6f; ///< 叩きつけた後の隙
    /** @brief 投げる技の名前ごとの中身（技の並びに名前を書くと、その中身で投げる） */
    std::map<std::string, BossVolleyData> volleys;
    /** @brief 武器種別名ごとのボス（無い武器は既定の名前と叩きつけだけ） */
    std::map<std::string, BossProfile> profiles;
};

/** @brief ボス戦の調整値の置き場（初回に読み、Reload で読み直す） */
class BossBattleTuningStore {
public:
    /** @brief 読み込み済みの調整値を返す */
    static const BossBattleTuning& Get();
    /** @brief boss_battle.json を読み直す */
    static void Reload();

private:
    /** @brief 調整値の実体 */
    static BossBattleTuning& Mutable();
};

/** @brief ボス戦の1フレームに渡す情報 */
struct BossFrameInput {
    Vector3 playerPosition; ///< プレイヤーの位置
    std::function<bool(const Vector3&)> isBlocked; ///< その位置へボスを動かすと壁にめり込むか
    bool canSeePlayer = true; ///< ボスとプレイヤーの間に壁や閉じた扉が無いか（無い間だけ動いて技を出す）
};

/** @brief ボス戦の1フレームで起きたこと（演出と当たり判定はシーンが行う） */
struct BossFrameOutput {
    bool staggerStarted = false; ///< ひるんだ
    bool rageStarted = false; ///< 第2段階へ入った
    bool slamRequested = false; ///< 叩きつけを始めてほしい
    bool chargeTelegraph = false; ///< 突進の身構えの最中
    bool dashTelegraph = false; ///< ダッシュの身構えの最中
    bool rushing = false; ///< 突進かダッシュで動いている最中（体が当たると痛い）
    bool charging = false; ///< 動いているのが突進か（false ならダッシュ）
    int strikeDamage = 0; ///< 動いている最中に体が当たった時のダメージ
    std::string warningRing; ///< 投げ・衝撃波・跳びかかりの身構えの最中に出す輪（空なら身構えていない）
    const BossVolleyData* volley = nullptr; ///< このフレームに投げる技（投げないなら nullptr）
    bool waveStarted = false; ///< 地面を叩いて衝撃波を出した
    bool leapLanded = false; ///< 跳びかかりで着地した
    BossMotionCue motion; ///< 今ボスが何をしているか（技ごとの動きを選ぶ）
};

/**
 * @brief ステージボス戦の進行役
 * @note ボスの位置は、名乗り・咆哮・突進・ダッシュの間だけ台本モードで直接動かす（それ以外はふだんの動き）
 */
class BossBattleDirector {
public:
    /** @brief 戦うボスを設定し、名乗る前の状態に戻す */
    void Bind(EnemyEntity* boss, const std::string& weaponName);
    /** @brief 1フレーム進める */
    void Update(float dt, const BossFrameInput& input, BossFrameOutput& output);
    /** @brief 動いている最中の体当たりが当たったことを伝える（同じ技で2回当てない） */
    void OnStrikeLanded() {
        strikeLanded_ = true;
    }
    /** @brief 戦い始めているか（画面上の体力バーを出す） */
    bool IsEngaged() const {
        return boss_ != nullptr && phase_ != Phase::kWaiting;
    }
    /** @brief 当てた攻撃で吹き飛んだりのけぞったりするか（ひるんでいる間だけ） */
    bool AllowsReaction() const {
        return staggered_;
    }
    /** @brief 第2段階に入ったか */
    bool IsRaging() const {
        return raging_;
    }
    /** @brief 叩きつけの範囲の倍率（第2段階で広がる） */
    float SlamRadiusMult() const;
    /** @brief ボスの種類の名前（武器種別名。技ごとの動きを引く） */
    const std::string& BossKey() const {
        return bossKey_;
    }
    /** @brief 今のボスの名前と技 */
    const BossProfile& Profile() const {
        return profile_;
    }

private:
    /** @brief 戦いの段階 */
    enum class Phase { kWaiting, kFight, kRage };
    /** @brief 技の段階 */
    enum class Move { kNone, kChargeTelegraph, kCharging, kDashTelegraph, kDashing, kVolleyTelegraph, kWaveTelegraph, kLeapTelegraph, kLeaping, kSlamTelegraph, kRecover };

    /** @brief 戦う前: プレイヤーが近づいたら戦い始める */
    void UpdateWaiting(const BossFrameInput& input);
    /** @brief 受けたダメージをひるみ値にため、たまったらひるませる（ひるんでいる間は true を返す） */
    bool UpdateStagger(float dt, const BossFrameInput& input, BossFrameOutput& output);
    /** @brief 戦闘中: ひるみ値と体力を見て、ひるむか咆哮へ移るか、技を進める */
    void UpdateFight(float dt, const BossFrameInput& input, BossFrameOutput& output);
    /** @brief 技の間隔を数え、次の技を選んで始める */
    void StartNextAttack(const BossFrameInput& input, BossFrameOutput& output);
    /** @brief 投げ・衝撃波・跳びかかりの身構えと跳んでいる間を進める（扱った時は true） */
    bool UpdateRangedMove(float dt, const BossFrameInput& input, BossFrameOutput& output);
    /** @brief 突進・ダッシュ・隙を1フレーム進める */
    void UpdateMove(float dt, const BossFrameInput& input, BossFrameOutput& output);
    /** @brief 台本モードを切り替え、向きをプレイヤーへ合わせる */
    void SetScripted(bool scripted, const Vector3& playerPosition);
    /** @brief 技の後の隙に入る */
    void BeginRecover(float seconds);
    /** @brief 時間で区切る段階に入る（身構え・隙。経過割合を出せるよう長さも覚える） */
    void BeginTimedMove(Move move, float seconds);
    /** @brief 今ボスが何をしているか（技・区切り・経過割合） */
    BossMotionCue CurrentMotionCue() const;

    EnemyEntity* boss_ = nullptr;
    BossProfile profile_;
    std::string bossKey_; ///< ボスの種類の名前（武器種別名）
    std::string currentAttack_; ///< 出している技の名前
    float stageSeconds_ = 0.0f; ///< 今の身構えか隙の長さ
    Phase phase_ = Phase::kWaiting;
    Move move_ = Move::kNone;
    bool raging_ = false;
    float phaseTimer_ = 0.0f; ///< 咆哮の残り秒数
    float attackTimer_ = 0.0f; ///< 次の技までの秒数
    float moveTimer_ = 0.0f; ///< 今の技の段階の残り秒数（ダッシュでは経過秒数）
    size_t attackIndex_ = 0; ///< 技の一覧のどれを次に出すか
    float moveDirX_ = 1.0f; ///< 突進の向き
    float chargeTraveled_ = 0.0f; ///< 突進で進んだ距離
    int dashLeft_ = 0; ///< 残りのダッシュ回数
    Vector3 dashFrom_ = {}; ///< ダッシュの出発点
    Vector3 dashTo_ = {}; ///< ダッシュの行き先
    bool strikeLanded_ = false; ///< 今の技の体当たりがもう当たったか
    BossVolleyData volley_ {}; ///< 身構えている投げる技（調整値を読み直しても壊れないよう写して持つ）
    Vector3 leapFrom_ = {}; ///< 跳びかかりの踏み切り位置
    Vector3 leapTo_ = {}; ///< 跳びかかりの着地位置
    bool staggered_ = false; ///< ひるんでいる最中か
    float staggerTimer_ = 0.0f; ///< ひるみの残り秒数
    float poise_ = 0.0f; ///< たまったダメージ
    int lastHp_ = 0; ///< 前のフレームの体力（減った分をひるみ値にためる）
};

} // namespace engine::game

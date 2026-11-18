/**
 * @file AllyBoss.h
 * @brief ボスの武器どうしを合体させている時に呼び出せる味方のボス（自動で戦い、2人目が途中から操作もできる）
 */
#pragma once
#include "CollisionConfig.h"
#include "EnemyEntity.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Weapon.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace engine::graphics {
class ModelCommon;
}

namespace engine::game {

/** @brief 味方のボスの調整値（Resources/Config/ally_boss.json） */
struct AllyBossTuning {
    int maxHp = 24; ///< 体力
    float damage = 0.6f; ///< 1回の攻撃のダメージ（敵の体力は1より下げない）
    float attackInterval = 0.9f; ///< 自動の時に攻撃する間隔（秒）
    float attackHitTime = 0.18f; ///< 攻撃を始めてから当たるまでの秒数
    float attackDuration = 0.42f; ///< 攻撃1段の長さ（秒）
    float comboWindow = 0.35f; ///< 2P の攻撃が次の段へつながる受付の秒数
    int comboSteps = 3; ///< 2P の攻撃が何段まで続くか
    float attackReach = 1.6f; ///< 攻撃が届く前の長さ
    float attackHeight = 1.4f; ///< 攻撃が届く上下の幅
    float hitstunSeconds = 0.25f; ///< 当てた敵ののけぞりの秒数
    float walkSpeed = 0.08f; ///< 1フレームに歩く距離
    float jumpSpeed = 0.32f; ///< 2P のジャンプの初速（1フレームに上がる距離）
    float gravity = 0.018f; ///< 1フレームに落ちる速さへ足す量
    float maxFallSpeed = 0.45f; ///< 1フレームに落ちる距離の上限
    float dodgeSpeed = 0.22f; ///< 回避で飛びのく速さ（1フレームの距離）
    float dodgeSeconds = 0.25f; ///< 回避の長さ（この間は無敵）
    float hurtInvincibleSeconds = 0.6f; ///< 敵の攻撃を受けた後の無敵の秒数
    float aggroRange = 8.0f; ///< 自動の時に敵を狙う距離
    float followDistance = 1.8f; ///< 自動の時にプレイヤーの後ろで待つ距離
    float leashDistance = 12.0f; ///< これ以上プレイヤーから離れたら、プレイヤーの近くへ戻る
    float enemyHitReach = 1.6f; ///< 敵の攻撃が味方に届く横の距離
    float enemyHitHeight = 1.6f; ///< 敵の攻撃が味方に届く上下の距離
    float bulletHitRadius = 0.6f; ///< 敵の弾が味方に当たる半径
    float respawnSeconds = 20.0f; ///< 倒れてから再び呼べるまでの秒数
    float idleSeconds = 12.0f; ///< 2P がこれだけ操作しなければ自動に戻る
    float footOffset = 0.1f; ///< 床の上面から体の位置までの高さ
    float screenMargin = 1.0f; ///< 2P の時に画面の端から内側に止める幅
    Vector4 color = { 0.35f, 0.8f, 1.0f, 1.0f }; ///< 体の色（敵のボスと見分けるため、敵のボスの色とは違う色にする）
    // ラスボス戦の援護（闘技場では味方は出さず、合図で飛び込んで一撃入れて消える）
    float arenaAssistDamage = 2.0f; ///< 援護の一撃のダメージ（敵の体力は1より下げない）
    float arenaAssistCooldown = 6.0f; ///< 援護してから次に援護できるまでの秒数
    float arenaAutoAssistInterval = 10.0f; ///< 2P がいない時に自動で援護する間隔
    float arenaAssistSeconds = 0.6f; ///< 援護で姿を見せている秒数
    float arenaAssistHitTime = 0.25f; ///< 現れてから斬るまでの秒数
    float arenaAssistSide = 1.6f; ///< ボスからプレイヤーの側へ離して現れる距離
    std::string keyLeft = "Left"; ///< 2P のキーボードの左
    std::string keyRight = "Right"; ///< 2P のキーボードの右
    std::string keyJump = "Up"; ///< 2P のキーボードのジャンプ
    std::string keyAttack = "Period"; ///< 2P のキーボードの攻撃
    std::string keyDodge = "Slash"; ///< 2P のキーボードの回避
    std::string keyJoinA = "RShift"; ///< 2P のキーボードの参加と抜ける（同時押しの1つ目）
    std::string keyJoinB = "RCtrl"; ///< 2P のキーボードの参加と抜ける（同時押しの2つ目）
    std::string keySummon = "V"; ///< 1P のキーボードの呼び出しと引っ込め
};

/** @brief 調整値を読む（初回だけファイルを読み、以後は覚えた値を返す） */
const AllyBossTuning& AllyTune();

/** @brief 2P が1フレームに押した操作（シーンが機器から読んで渡す） */
struct AllyControl {
    float moveX = 0.0f; ///< 左右の入力（-1〜1）
    bool jump = false; ///< ジャンプを押した瞬間
    bool attack = false; ///< 攻撃を押した瞬間
    bool dodge = false; ///< 回避を押した瞬間
    bool any = false; ///< 何か操作したか（しばらく操作が無いと自動に戻す）
};

/** @brief 味方のボスが1フレームに知る周りの様子（シーンが集めて渡す） */
struct AllySurroundings {
    Vector3 playerPosition {}; ///< プレイヤーの位置
    std::vector<EnemyEntity*> enemies; ///< 生きている敵（ボスと道中の敵）
    const std::vector<engine::AABB>* terrain = nullptr; ///< 地形の当たり判定（敵の体を除く）
    float cameraX = 0.0f; ///< カメラの横位置（2P の時に画面の外へ出さない）
    float cameraHalfWidth = 0.0f; ///< 画面の半分の横幅（ワールドの長さ）
};

/**
 * @brief 味方のボス（呼ぶと現れて近接で戦う。体力が尽きると消え、しばらく呼べない）
 * @note 体の見た目は敵の体（EnemyEntity）を台本で動かす形で使う。武器の種類ごとに一度だけ作り、ステージを出るまで消さない
 */
class AllyBoss {
public:
    /**
     * @brief 使う前の準備（モデルの共通設定を覚える。体はまだ作らない）
     * @param modelCommon モデルの共通設定
     */
    void Initialize(engine::graphics::ModelCommon* modelCommon);

    /**
     * @brief 呼び出す（主軸の武器の体を出してプレイヤーの近くに置く）
     * @param weapon 主軸の武器の種類
     * @param color 体の色
     * @param position 出す位置
     */
    void Summon(WeaponType weapon, const Vector4& color, const Vector3& position);
    /** @brief 引っ込める（体を隠す。体力はそのまま次に呼んだ時に満タンへ戻す） */
    void Dismiss();
    /** @brief 呼び出しているか */
    bool IsActive() const {
        return active_;
    }
    /** @brief 倒れてから再び呼べるまでの残り秒数（0なら呼べる） */
    float RespawnRemaining() const {
        return respawnTimer_;
    }

    /**
     * @brief 1フレーム進める（自動か 2P の操作で動き、攻撃する）
     * @param around 周りの様子
     * @param control 2P の操作（自動の時は nullptr）
     * @return このフレームに攻撃が当たる瞬間なら true（当たる敵は TakeHitTargets で取る）
     */
    bool Update(const AllySurroundings& around, const AllyControl* control);
    /** @brief 倒れてから再び呼べるまでの時間だけを進める（呼び出していない間も毎フレーム呼ぶ） */
    void TickRespawn(float dt);
    /** @brief 止めている間（演出中など）に、体を隠すか戻すかを決める */
    void SetHidden(bool hidden);

    /**
     * @brief 攻撃の当たる箱に入っている敵を集める
     * @param around 周りの様子
     * @return 当たった敵
     */
    std::vector<EnemyEntity*> CollectHitTargets(const AllySurroundings& around) const;
    /**
     * @brief 敵の攻撃を受ける（無敵の間は受けない）
     * @param amount ダメージ
     * @return 倒れたら true
     */
    bool TakeDamage(int amount);
    /** @brief 指定した位置が体に当たるか（敵の弾の当たり） */
    bool Overlaps(const Vector3& position, float radius) const;

    /**
     * @brief ラスボス戦の援護を始める（主軸の武器の体を一瞬だけ出して斬らせる）
     * @param weapon 主軸の武器の種類
     * @param color 体の色
     * @param position 現れる位置
     * @param facing 向き（右=+1、左=-1）
     */
    void StartArenaAssist(WeaponType weapon, const Vector4& color, const Vector3& position, float facing);
    /** @brief ラスボス戦の援護を1フレーム進める（斬る瞬間なら true。終わったら体を隠す） */
    bool UpdateArenaAssist();
    /** @brief ラスボス戦の援護で姿を見せている最中か */
    bool IsAssisting() const {
        return assistTimer_ >= 0.0f;
    }
    /** @brief 次に援護できるまでの残り秒数を進め、残りを返す */
    float TickAssistCooldown(float dt);
    /** @brief 援護の待ちを決める（援護した直後と、自動の間隔） */
    void SetAssistCooldown(float seconds) {
        assistCooldown_ = seconds;
    }

    /** @brief 体を描く */
    void Draw();
    /** @brief 体の位置 */
    const Vector3& GetPosition() const {
        return position_;
    }
    /** @brief 体力の割合（0〜1） */
    float HpRatio() const;
    /** @brief 主軸の武器の種類 */
    WeaponType GetWeapon() const {
        return weapon_;
    }
    /** @brief 向き（右=+1、左=-1） */
    float GetFacing() const {
        return facing_;
    }

private:
    /** @brief 主軸の武器の体を返す（まだ無ければ作る。作った体はステージを出るまで残す） */
    EnemyEntity* BodyFor(WeaponType weapon);
    /** @brief 自動の時の左右の動きを決める（狙う敵へ近づくか、プレイヤーの後ろへ戻る） */
    float ThinkMoveX(const AllySurroundings& around, bool& wantsAttack) const;
    /** @brief 一番近い生きている敵を返す（狙う距離の外なら nullptr） */
    EnemyEntity* NearestEnemy(const AllySurroundings& around) const;
    /** @brief 重力と床の当たり判定で高さを決める */
    void ApplyGravity(const std::vector<engine::AABB>& terrain);
    /** @brief 攻撃の段を始める */
    void StartAttack();

    engine::graphics::ModelCommon* modelCommon_ = nullptr;
    std::map<WeaponType, std::unique_ptr<EnemyEntity>> bodies_; ///< 武器の種類ごとの体（使い回す）
    EnemyEntity* body_ = nullptr; ///< 今使っている体
    WeaponType weapon_ = WeaponType::kSword;
    Vector3 position_ {};
    float velocityY_ = 0.0f;
    float facing_ = 1.0f;
    bool onGround_ = false;
    bool active_ = false;
    bool hidden_ = false;
    int hp_ = 0;
    float respawnTimer_ = 0.0f; ///< 倒れてから再び呼べるまでの残り秒数
    float invincibleTimer_ = 0.0f; ///< 無敵の残り秒数
    float attackTimer_ = 0.0f; ///< 攻撃の段の経過秒数（攻撃していなければ負）
    bool attackHitDone_ = false; ///< この段の当たりをもう出したか
    int comboStep_ = 0; ///< 2P の攻撃の今の段（0なら攻撃していない）
    bool comboQueued_ = false; ///< 2P が次の段を先に押したか
    float autoAttackCooldown_ = 0.0f; ///< 自動の時に次に攻撃できるまでの秒数
    float dodgeTimer_ = 0.0f; ///< 回避の残り秒数
    float assistTimer_ = -1.0f; ///< ラスボス戦の援護の経過秒数（援護していなければ負）
    bool assistHitDone_ = false; ///< この援護の一撃をもう出したか
    float assistCooldown_ = 0.0f; ///< 次に援護できるまでの残り秒数
};

} // namespace engine::game

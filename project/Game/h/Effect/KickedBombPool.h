/**
 * @file KickedBombPool.h
 * @brief 攻撃で吹き飛ばした爆弾（爆発する樽）を飛ばし、ぶつかった所で爆発させる
 */
#pragma once
#include "CollisionConfig.h"
#include "Vector3.h"
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace engine::graphics {
class Object3d;
class ModelCommon;
class Model;
} // namespace engine::graphics

namespace engine::game {

/** @brief 吹き飛ぶ爆弾の調整値（bomb_kick.json。初期値はJSONに項目が無い時の既定値） */
struct BombKickTuning {
    bool enabled = true; ///< false なら今までどおりその場で爆発する
    std::string model = "Resources/block/gimmick/gimmick_barrel.obj"; ///< 飛んでいる間の見た目
    std::string texture = "Resources/block/gimmick/gimmick_palette.png";
    float speedX = 14.0f; ///< 横へ飛ぶ速さ
    float launchSpeedY = 3.5f; ///< 吹き飛んだ瞬間の上向きの速さ
    float gravity = 18.0f; ///< 落ちる加速度
    float spinSpeed = 14.0f; ///< 飛びながら回る速さ（ラジアン毎秒）
    float hitRadius = 0.45f; ///< 敵や壁にぶつかったとみなす半径
    float maxSeconds = 2.0f; ///< これだけ飛んでも何にも当たらなければ、その場で爆発する
    float launchGraceSeconds = 0.06f; ///< 吹き飛んだ直後は足元の床でいきなり爆発しないよう、壁の判定を待つ長さ
    std::string launchImpact = "bomb_kick"; ///< 吹き飛ばした瞬間の手応え
    std::string trailSparks = "bomb_kick_trail"; ///< 飛んでいる間に出す火の粉
    float trailInterval = 0.05f; ///< 火の粉を出す間隔（秒）
    std::string wallModel = "Resources/block/gimmick/gimmick_wall.obj"; ///< 壊れて吹き飛ぶ壁の見た目
    float wallSpeedX = 16.0f; ///< 壁が横へ飛ぶ速さ
    float wallLaunchSpeedY = 5.0f; ///< 壁が吹き飛んだ瞬間の上向きの速さ
    float wallSpinSpeed = 7.0f; ///< 壁が飛びながら回る速さ（ラジアン毎秒）
    float wallMaxSeconds = 1.6f; ///< 壁が何にも当たらなければ砕けるまでの秒数
    int wallEnemyDamage = 4; ///< 飛んでくる壁に巻き込まれたボスへのダメージ（道中の敵は一撃で倒れる）
    float wallHitRadius = 0.9f; ///< 飛んでいる壁が別の壁や床にぶつかったとみなす半径（中心から）
    float wallGraceSeconds = 0.15f; ///< 壁が吹き飛んだ直後、隣の地形でいきなり砕けないよう判定を待つ長さ
};

/** @brief 吹き飛ばす物の種類 */
enum class KickedObjectKind {
    kBomb, ///< 爆弾。敵か壁にぶつかった所で爆発する
    kWall, ///< 壊せる壁。敵を巻き込みながら飛び、壁か床にぶつかった所で砕ける
};

/** @brief 吹き飛ぶ爆弾の調整値の置き場（初回に読み、Reload で読み直す） */
class BombKickTuningStore {
public:
    /** @brief 読み込み済みの調整値を返す */
    static const BombKickTuning& Get();
    /** @brief bomb_kick.json を読み直す */
    static void Reload();

private:
    /** @brief 調整値の実体 */
    static BombKickTuning& Mutable();
};

/** @brief 爆発した時の威力（元の樽の設定を写しておく） */
struct BombPayload {
    float radius = 0.0f; ///< 爆風の半径
    int playerDamage = 0; ///< 爆風に巻き込まれたプレイヤーへのダメージ
    int enemyDamage = 0; ///< 爆風に巻き込まれた敵へのダメージ
};

/** @brief このフレームに起きた爆発 */
struct BombDetonation {
    Vector3 position; ///< 爆発した位置
    BombPayload payload; ///< 威力
    KickedObjectKind kind = KickedObjectKind::kBomb; ///< 爆発した物の種類（壁なら爆風は出さず砕けるだけ）
};

/**
 * @brief 吹き飛んだ爆弾の見た目と動きを、あらかじめ作った数だけ使い回して扱う
 * @note ゲーム中に描画用の実体を作ったり消したりしないよう、Initialize で全部作っておく
 */
class KickedBombPool {
public:
    /** @brief 敵にぶつかったかを調べる関数（位置と半径を渡す） */
    using EnemyHitTest = std::function<bool(const Vector3& position, float radius)>;
    /**
     * @brief 飛んでいる壁の箱に重なった敵を巻き込む関数
     * @param box 壁の当たり判定の箱
     * @param dirX 壁が飛んでいる向き（正で右）
     * @param alreadyHit この壁がもう巻き込んだ相手（巻き込んだら呼び出し側が足す）
     */
    using WallSweep = std::function<void(const engine::AABB& box, float dirX, std::vector<const void*>& alreadyHit)>;

    KickedBombPool();
    ~KickedBombPool();

    /** @brief 見た目の実体を全部作る（シーンの初期化で一度だけ呼ぶ） */
    void Initialize(engine::graphics::ModelCommon* modelCommon);
    /**
     * @brief 爆弾を吹き飛ばす
     * @param position 吹き飛ぶ前の位置
     * @param dirX 飛ばす向き（正で右）
     * @param scale 元の樽の大きさ
     * @param payload 爆発した時の威力
     * @param kind 吹き飛ばす物の種類
     * @return 空きが無くて飛ばせなかったら false（呼び出し側はその場で爆発させる）
     */
    bool Launch(const Vector3& position, float dirX, const Vector3& scale, const BombPayload& payload,
        KickedObjectKind kind = KickedObjectKind::kBomb);
    /**
     * @brief 飛んでいる爆弾と壁を進め、ぶつかった物をこのフレームの爆発として返す
     * @param dt 経過秒数
     * @param solids 壁・床の当たり判定
     * @param hitsEnemy 爆弾が敵にぶつかったかを調べる関数
     * @param sweepEnemies 飛んでいる壁に重なった敵を巻き込む関数
     * @param trailEmitter 飛んでいる間の火の粉を出す関数（位置を渡す）
     */
    std::vector<BombDetonation> Update(float dt, const std::vector<engine::AABB>& solids,
        const EnemyHitTest& hitsEnemy, const WallSweep& sweepEnemies,
        const std::function<void(const Vector3&)>& trailEmitter);
    /** @brief 飛んでいる爆弾を描く（3Dモデルの描画設定を済ませてから呼ぶ） */
    void Draw();
    /** @brief 飛んでいる爆弾を全部消す（ステージを読み直した時） */
    void Clear();
    /** @brief 飛んでいる爆弾が1つでもあるか */
    bool HasActive() const;

private:
    /** @brief 飛んでいる爆弾1つ */
    struct Bomb {
        bool active = false; ///< 使用中か
        Vector3 position; ///< 今の位置
        Vector3 velocity; ///< 今の速さ
        Vector3 scale; ///< 見た目の大きさ
        float spin = 0.0f; ///< 今の回転角
        float spinDir = 1.0f; ///< 回る向き（飛ぶ向きに合わせる）
        float age = 0.0f; ///< 飛び始めてからの秒数
        float trailTimer = 0.0f; ///< 次に火の粉を出すまでの秒数
        BombPayload payload; ///< 爆発した時の威力
        KickedObjectKind kind = KickedObjectKind::kBomb; ///< 飛んでいる物の種類
        std::vector<const void*> sweptTargets; ///< 壁がもう巻き込んだ敵（同じ敵を何度も飛ばさない）
    };

    /** @brief 飛んでいる壁の当たり判定の箱（回っていても届くよう、横は高さの半分まで広げる） */
    static engine::AABB WallBounds(const Bomb& wall);
    /** @brief 一度に飛ばせる爆弾の数 */
    static constexpr size_t kCapacity = 4;

    /** @brief 壁・床の箱のどれかに、球がめり込んだか */
    static bool TouchesSolid(const Vector3& position, float radius, const std::vector<engine::AABB>& solids);

    std::array<Bomb, kCapacity> bombs_; ///< 爆弾の状態
    std::array<std::unique_ptr<engine::graphics::Object3d>, kCapacity> objects_; ///< 爆弾の見た目（状態と同じ番号を使う）
    engine::graphics::Model* bombModel_ = nullptr; ///< 爆弾の見た目のモデル（非所有）
    engine::graphics::Model* wallModel_ = nullptr; ///< 壁の見た目のモデル（非所有）
};

} // namespace engine::game

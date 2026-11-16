/**
 * @file ThrownWeaponPool.h
 * @brief 投げて戻る斧の状態と、事前に作った描画実体を管理する
 */
#pragma once
#include "Vector3.h"
#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace engine::graphics {
class Object3d;
class ModelCommon;
} // namespace engine::graphics

namespace engine::game {
class EnemyEntity;

/** @brief 投げる時の数値を保持する */
struct ThrownWeaponParams {
    static constexpr float kDefaultSpeed = 16.0f; ///< 既定の飛ぶ速さ
    static constexpr float kDefaultDistance = 7.0f; ///< 既定の行きの距離
    static constexpr float kDefaultSpinSpeed = 18.0f; ///< 既定の回る速さ
    static constexpr float kDefaultHitRadius = 0.8f; ///< 既定の命中半径
    static constexpr float kDefaultMaxSeconds = 3.0f; ///< 既定の寿命
    static constexpr float kDefaultScale = 0.5f; ///< 既定の大きさ

    float speed = kDefaultSpeed; ///< 飛ぶ速さ
    float distance = kDefaultDistance; ///< 行きで進む距離
    float spinSpeed = kDefaultSpinSpeed; ///< 回る速さ（ラジアン毎秒）
    float hitRadius = kDefaultHitRadius; ///< 敵に当たったとみなす半径
    float maxSeconds = kDefaultMaxSeconds; ///< 最長の生存秒数
    float scale = kDefaultScale; ///< 見た目の大きさ
};

/** @brief このフレームに当たった敵1体 */
struct ThrownWeaponHit {
    EnemyEntity* target = nullptr; ///< 当たった敵
    Vector3 position {}; ///< 当たった時の斧の位置
    bool returning = false; ///< 帰りの当たりか
};

/** @brief 2本の斧を事前に用意し、往復移動と貫通命中を管理する */
class ThrownWeaponPool {
public:
    /** @brief 指定した球に重なる生きた敵の一覧を取得する */
    using EnemyQuery = std::function<std::vector<EnemyEntity*>(const Vector3& position, float radius)>;
    /** @brief 空のプールを作る */
    ThrownWeaponPool();
    /** @brief 初期化時に用意した描画実体を破棄する */
    ~ThrownWeaponPool();
    /** @brief シーンの初期化時にモデルと2本分の描画実体を用意する */
    void Initialize(engine::graphics::ModelCommon* modelCommon, const std::string& model, const std::string& texture);
    /** @brief 斧を投げ、空きがない場合や初期化前、調整値が不正の場合は失敗する */
    bool Launch(const Vector3& position, float dirX, const ThrownWeaponParams& params);
    /** @brief 最新の戻り先へ移動し、このフレームの命中一覧を返す */
    std::vector<ThrownWeaponHit> Update(float dt, const Vector3& returnTarget, const EnemyQuery& query);
    /** @brief 3Dモデルの描画設定を済ませてから使用中の斧を描く */
    void Draw();
    /** @brief 描画実体を保持したまま、すべての斧と命中履歴を解除する */
    void Clear();
    /** @brief 使用中の斧があるかを返す */
    bool HasActive() const;

private:
    static constexpr std::size_t kCapacity = 2; ///< 同時に投げられる本数
    static constexpr float kZero = 0.0f; ///< 状態の初期値
    static constexpr float kOne = 1.0f; ///< 正方向と単位倍率

    /** @brief 斧1本の移動状態と往復それぞれの命中履歴 */
    struct Weapon {
        bool active = false;
        bool returning = false;
        Vector3 position {};
        float direction = kOne;
        float traveled = kZero;
        float returnSpeed = kZero;
        float spin = kZero;
        float age = kZero;
        ThrownWeaponParams params;
        std::vector<EnemyEntity*> outwardHits; ///< 行きで命中した敵の識別用ポインタ
        std::vector<EnemyEntity*> returnHits; ///< 帰りで命中した敵の識別用ポインタ
    };

    /** @brief 移動状態を対応する描画実体へ反映する */
    void UpdateVisual(std::size_t index);

    std::array<Weapon, kCapacity> weapons_ {}; ///< 固定数の移動状態
    std::array<std::unique_ptr<engine::graphics::Object3d>, kCapacity> objects_ {}; ///< 初期化時に作る描画実体
};
} // namespace engine::game

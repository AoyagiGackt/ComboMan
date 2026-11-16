/**
 * @file ThrownWeaponPool.cpp
 * @brief 斧を回転させながら投げ、減速と帰還、往復の命中を処理する
 */
#include "ThrownWeaponPool.h"
#include "ModelCommon.h"
#include "ModelManager.h"
#include "Object3d.h"
#include <algorithm>
#include <cmath>

namespace engine::game {
namespace {
constexpr float kSimulationStep = 1.0f / 120.0f; ///< 移動判定1回の最長秒数
constexpr float kBrakeFraction = 0.25f; ///< 終点手前で減速する距離の割合
constexpr float kMinimumSpeedFraction = 0.1f; ///< 終点直前で停止し続けないための最低速度の割合
constexpr float kReturnAccelerationSeconds = 0.15f; ///< 折り返しから最高速度までの秒数
constexpr float kCatchRadius = 0.35f; ///< 戻り先で回収する半径
constexpr float kHitStepFraction = 0.5f; ///< 1回の移動距離に対する命中半径の割合
constexpr float kMinimumStepDistance = 0.01f; ///< 半径がゼロの場合にも時間を進めるための距離
} // namespace

/** @brief 描画実体を作らずに初期状態を用意する */
ThrownWeaponPool::ThrownWeaponPool() = default;

/** @brief 描画実体の定義が見える場所で資源を解放する */
ThrownWeaponPool::~ThrownWeaponPool() = default;

/** @brief モデルを共有し、ゲーム中に使い回す描画実体を先に用意する */
void ThrownWeaponPool::Initialize(engine::graphics::ModelCommon* modelCommon,
    const std::string& model, const std::string& texture) {
    engine::graphics::Model* sharedModel = engine::graphics::ModelManager::GetInstance()->GetOrLoad(modelCommon, model, texture);
    for (auto& object : objects_) {
        // 再初期化でも既存の描画実体を保持する
        if (!object) {
            object = std::make_unique<engine::graphics::Object3d>();
            object->Initialize(modelCommon);
        }
        object->SetModel(sharedModel);
    }
    Clear();
}

/** @brief 空きの状態を投擲時の値で設定する */
bool ThrownWeaponPool::Launch(const Vector3& position, float dirX, const ThrownWeaponParams& params) {
    // 移動が成立しない値と非有限値は描画へ渡さない
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)
        || !std::isfinite(dirX) || !std::isfinite(params.speed) || params.speed <= kZero
        || !std::isfinite(params.distance) || params.distance < kZero
        || !std::isfinite(params.spinSpeed) || !std::isfinite(params.hitRadius) || params.hitRadius < kZero
        || !std::isfinite(params.maxSeconds) || params.maxSeconds <= kZero
        || !std::isfinite(params.scale) || params.scale <= kZero) {
        return false;
    }
    for (std::size_t index {}; index < weapons_.size(); ++index) {
        Weapon& weapon = weapons_[index];
        if (weapon.active || !objects_[index]) {
            continue;
        }
        weapon = Weapon {};
        weapon.active = true;
        weapon.position = position;
        weapon.direction = dirX >= kZero ? kOne : -kOne;
        weapon.params = params;
        weapon.returning = params.distance <= kZero;
        UpdateVisual(index);
        return true;
    }
    return false;
}

/** @brief 細かい時間刻みで往復移動し、各方向につき敵1体あたり1回の命中を返す */
std::vector<ThrownWeaponHit> ThrownWeaponPool::Update(float dt, const Vector3& returnTarget, const EnemyQuery& query) {
    std::vector<ThrownWeaponHit> hits;
    if (!std::isfinite(dt) || dt <= kZero || !std::isfinite(returnTarget.x) || !std::isfinite(returnTarget.y)) {
        return hits;
    }
    for (std::size_t index {}; index < weapons_.size(); ++index) {
        Weapon& weapon = weapons_[index];
        if (!weapon.active) {
            continue;
        }
        // フレームの長さと飛ぶ速さに合わせて判定の刻み幅を小さくする
        const float lifetimeRemaining = weapon.params.maxSeconds - weapon.age;
        const bool expiresThisFrame = dt >= lifetimeRemaining;
        float remainingTime = (std::min)(dt, lifetimeRemaining);
        const float stepDistance = (std::max)(weapon.params.hitRadius * kHitStepFraction, kMinimumStepDistance);
        const float stepSeconds = (std::min)(kSimulationStep, stepDistance / weapon.params.speed);
        while (weapon.active && remainingTime > kZero) {
            const float step = (std::min)(remainingTime, stepSeconds);
            const bool returning = weapon.returning;
            bool caught = false;
            if (!returning) {
                // 行きの高さと奥行きを固定し、終点手前では残り距離に応じて減速する
                const float remainingDistance = (std::max)(kZero, weapon.params.distance - weapon.traveled);
                const float brakeDistance = weapon.params.distance * kBrakeFraction;
                const float speedRatio = brakeDistance > kZero
                    ? std::sqrt((std::min)(kOne, remainingDistance / brakeDistance)) : kOne;
                const float speed = weapon.params.speed * (std::max)(kMinimumSpeedFraction, speedRatio);
                const float movement = (std::min)(remainingDistance, speed * step);
                weapon.position.x += weapon.direction * movement;
                weapon.traveled += movement;
                if (movement >= remainingDistance) {
                    weapon.returning = true;
                }
            } else {
                // 折り返しでは加速し、最新の戻り先へ高さを含めて追従する。奥行きは変えない
                const float dx = returnTarget.x - weapon.position.x;
                const float dy = returnTarget.y - weapon.position.y;
                const float distance = std::sqrt(dx * dx + dy * dy);
                weapon.returnSpeed = (std::min)(weapon.params.speed,
                    weapon.returnSpeed + weapon.params.speed * step / kReturnAccelerationSeconds);
                const float movement = (std::min)(distance, weapon.returnSpeed * step);
                if (distance > kZero) {
                    weapon.position.x += dx / distance * movement;
                    weapon.position.y += dy / distance * movement;
                }
                caught = distance - movement <= kCatchRadius;
            }
            weapon.spin -= weapon.direction * weapon.params.spinSpeed * step;
            weapon.age += step;
            remainingTime -= step;

            // 敵は貫通し、同じ更新内でも往復の履歴を混ぜない。ポインタは比較にだけ使う
            if (query) {
                auto& history = returning ? weapon.returnHits : weapon.outwardHits;
                for (EnemyEntity* target : query(weapon.position, weapon.params.hitRadius)) {
                    if (target != nullptr && std::find(history.begin(), history.end(), target) == history.end()) {
                        history.push_back(target);
                        hits.push_back({ target, weapon.position, returning });
                    }
                }
            }
            if (caught || weapon.age >= weapon.params.maxSeconds) {
                weapon.active = false;
            }
        }
        // 時間の加算誤差があっても、寿命を越えたフレームでは必ず終了する
        if (expiresThisFrame) {
            weapon.active = false;
        }
        if (weapon.active) {
            UpdateVisual(index);
        }
    }
    return hits;
}

/** @brief 状態を描画用の位置、回転、大きさへ反映する */
void ThrownWeaponPool::UpdateVisual(std::size_t index) {
    const Weapon& weapon = weapons_[index];
    auto& object = objects_[index];
    object->SetPosition(weapon.position);
    object->SetRotation({ kZero, kZero, weapon.spin });
    object->SetScale({ weapon.params.scale, weapon.params.scale, weapon.params.scale });
    object->Update();
}

/** @brief 使用中の描画実体だけを描く */
void ThrownWeaponPool::Draw() {
    for (std::size_t index {}; index < weapons_.size(); ++index) {
        if (weapons_[index].active && objects_[index]) {
            objects_[index]->Draw();
        }
    }
}

/** @brief 描画実体を作り直さずに状態と命中履歴を消す */
void ThrownWeaponPool::Clear() {
    for (Weapon& weapon : weapons_) {
        weapon = Weapon {};
    }
}

/** @brief 使用中の斧が1本でもあるかを調べる */
bool ThrownWeaponPool::HasActive() const {
    return std::any_of(weapons_.begin(), weapons_.end(), [](const Weapon& weapon) {
        return weapon.active;
    });
}
} // namespace engine::game

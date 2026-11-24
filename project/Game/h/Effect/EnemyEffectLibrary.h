/**
 * @file EnemyEffectLibrary.h
 * @brief 敵の種類ごとの攻撃とやられの GPU パーティクル演出（Resources/Config/enemy_effects.json）
 * @note 層の書き方は element_effects.json と同じ（ParticleLayerList）。種類に無い項目は default を使う
 */
#pragma once
#include "ElementEffect.h"
#include "Vector3.h"
#include "Vector4.h"
#include <map>
#include <random>
#include <string>

namespace engine::graphics {
class ParticleManager;
}

namespace engine::game {

class EnemyEntity;

/** @brief 敵1種類ぶんの演出 */
struct EnemyEffectSet {
    Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f }; ///< 層の color が weapon の時の色（その敵らしい色）
    ParticleLayerList attack; ///< 攻撃を出した瞬間の層
    ParticleLayerList defeat; ///< やられた瞬間の層
    std::string attackField; ///< 攻撃を出した瞬間に置く場（particle_fields.json の名前。空なら置かない）
    std::string defeatField; ///< やられた瞬間に置く場
};

/** @brief 敵の種類ごとの攻撃とやられの演出 */
class EnemyEffectLibrary {
public:
    /** @brief 演出を出す先（シーンの部品を借りる。非所有） */
    struct Target {
        engine::graphics::ParticleManager* particles = nullptr;
        std::mt19937* rng = nullptr;
    };

    /** @brief 敵の種類の名前（モンスターはその種類、それ以外は Knight） */
    static std::string KindOf(const EnemyEntity& enemy);
    /**
     * @brief 攻撃を出した瞬間の演出を出す
     * @param kind     敵の種類の名前（KindOf の結果。ボスは Boss）
     * @param position 出す位置
     * @param facingX  攻撃の向き（+1 で +X）
     */
    static void EmitAttack(const Target& target, const std::string& kind, const Vector3& position, float facingX);
    /** @brief やられた瞬間の演出を出す */
    static void EmitDefeat(const Target& target, const std::string& kind, const Vector3& position);
    /** @brief enemy_effects.json を読み直す */
    static void Reload();

private:
    /** @brief 種類の演出を返す（無ければ default、それも無ければ nullptr） */
    static const EnemyEffectSet* Find(const std::string& kind);
    /** @brief 読み込み済みの一覧（初回に読む） */
    static std::map<std::string, EnemyEffectSet>& Sets();
    /** @brief 層と場を出す */
    static void Emit(const Target& target, const EnemyEffectSet& set, const ParticleLayerList& layers,
        const std::string& field, const Vector3& position, float facingX);
};

} // namespace engine::game

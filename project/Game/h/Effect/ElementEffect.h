/**
 * @file ElementEffect.h
 * @brief 武器属性ごとの命中演出と振り中オーラを出し分ける属性エフェクト群
 */
#pragma once
#include "Vector3.h"
#include "Vector4.h"
#include <random>
#include <string>
namespace engine::graphics {
class ParticleManager;
}
namespace engine::game {

/** @brief 属性エフェクトが粒子を出すときに使う共有状態 */
struct ElementEffectContext {
    engine::graphics::ParticleManager& particles; ///< 粒子の発生先
    std::mt19937& rng; ///< ばらつき用の乱数
    float facingX; ///< プレイヤーの向き（右=+1, 左=-1）
    bool hasBlade = false; ///< 刃の根元と先端が取れたか（alongBlade の層が使う）
    Vector3 bladeBase = { }; ///< 刃の根元（ワールド座標）
    Vector3 bladeTip = { }; ///< 刃の先端（ワールド座標）
};

/** @brief 属性で敵に残る状態の設定（element_effects.json の status） */
struct ElementStatusDef {
    float duration = 0.0f; ///< 続く秒数（0なら状態を残さない）
    float tickInterval = 0.0f; ///< 継続ダメージの間隔（0なら継続ダメージなし）
    float tickDamage = 0.0f; ///< 1回の継続ダメージ（端数は持ち越し）
    float speedScale = 1.0f; ///< 状態の間、歩く速さに掛ける倍率
    float emitInterval = 0.05f; ///< 状態の粒を出す間隔
};

/**
 * @brief 1属性分の演出を持つ基底クラス
 * @note 属性名からの取得は Find() を使い、呼び出し側で属性名を比較しない
 */
class IElementEffect {
public:
    /** @brief 派生クラスを基底クラスから正しく破棄できるようにする仮想デストラクタ */
    virtual ~IElementEffect() = default;

    /**
     * @brief 敵への命中点に属性演出を出す
     * @param ctx   粒子の発生先と乱数
     * @param pos   命中位置
     * @param color 武器の演出色
     * @param scale コンボ段や連続ヒットを反映した拡大率
     */
    virtual void EmitHit(const ElementEffectContext& ctx, const Vector3& pos, const Vector4& color, float scale) const = 0;

    /**
     * @brief 振り中の武器から毎フレーム少量の属性粒子を零す
     * @param ctx   粒子の発生先と乱数
     * @param pos   武器位置（ばらつき適用済み）
     * @param color 武器の演出色
     */
    virtual void EmitAura(const ElementEffectContext& ctx, const Vector3& pos, const Vector4& color) const = 0;

    /**
     * @brief 武器を構えているだけの間、毎フレーム武器にまとわせる粒を出す
     * @param ctx   粒子の発生先と乱数（刃の位置が入っていれば刃に沿って出せる）
     * @param pos   武器位置
     * @param color 武器の演出色
     */
    virtual void EmitIdle(const ElementEffectContext& ctx, const Vector3& pos, const Vector4& color) const = 0;

    /**
     * @brief 状態が残っている敵の体から粒を出す（燃えている・凍っているなど）
     * @param ctx   粒子の発生先と乱数
     * @param pos   敵の中心
     * @param color 武器の演出色
     */
    virtual void EmitStatus(const ElementEffectContext& ctx, const Vector3& pos, const Vector4& color) const = 0;

    /**
     * @brief 武器を手に入れた瞬間の粒を出す
     * @param ctx   粒子の発生先と乱数
     * @param pos   プレイヤーの胸の高さ
     * @param color 武器の演出色
     */
    virtual void EmitAcquire(const ElementEffectContext& ctx, const Vector3& pos, const Vector4& color) const = 0;

    /** @brief 敵に残る状態の設定 */
    virtual const ElementStatusDef& Status() const = 0;

    /** @brief 武器名の前に付ける属性の呼び名（炎の・氷の など。無ければ空） */
    virtual const std::wstring& DisplayName() const = 0;

    /**
     * @brief 属性名に対応するエフェクトを取得する
     * @param element 武器データの属性名（Water, Fire など）
     * @return 対応する属性が無ければ nullptr
     */
    static const IElementEffect* Find(const std::string& element);
};

} // namespace engine::game

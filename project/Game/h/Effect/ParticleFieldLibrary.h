/**
 * @file ParticleFieldLibrary.h
 * @brief 名前で呼べる粒の場（衝撃で押し広げる・渦へ吸い込む・突進に引きずる）の置き場（Resources/Config/particle_fields.json）
 * @note 場の計算そのものは GPU パーティクルの更新（ParticleUpdate.CS.hlsl）が行う。ここは名前から中身を引いて置くだけ
 */
#pragma once
#include "Vector3.h"
#include <map>
#include <string>

namespace engine::graphics {
class ParticleManager;
}

namespace engine::game {

/** @brief 場1つぶんの調整値（particle_fields.json の1項目） */
struct ParticleFieldPreset {
    std::string type = "radial"; ///< radial=中心から押す・引く directional=向いている方へ流す
    float radius = 3.0f; ///< 効く半径
    float strength = 10.0f; ///< 加速度（radial は正で押し広げ、負で引き寄せる。directional は流す強さ）
    float swirl = 0.0f; ///< 中心のまわりを回す加速度（radial の時だけ）
    float drag = 0.0f; ///< 範囲の中で速さを抜く強さ
    float seconds = 0.0f; ///< 残る秒数（0 なら置いたフレームだけ。続けて効かせる時は毎フレーム置く）
    float height = 0.0f; ///< 置く位置を足元から上げる高さ
    float back = 0.0f; ///< directional の時、向いている方の逆へずらす距離（突進の後ろに引きずる）
};

/** @brief 名前で呼べる粒の場 */
class ParticleFieldLibrary {
public:
    /**
     * @brief 名前の場を置く（無い名前なら何もしない）
     * @param particles 置く先
     * @param name      particle_fields.json の名前
     * @param position  置く位置（足元。preset の height だけ上げる）
     * @param dirX      向いている方（+1 で +X。directional の流す向きと back のずらす向きに使う）
     */
    static void Emit(engine::graphics::ParticleManager* particles, const std::string& name, const Vector3& position, float dirX = 1.0f);
    /** @brief particle_fields.json を読み直す */
    static void Reload();

private:
    /** @brief 読み込み済みの一覧（初回に読む） */
    static std::map<std::string, ParticleFieldPreset>& Presets();
};

} // namespace engine::game

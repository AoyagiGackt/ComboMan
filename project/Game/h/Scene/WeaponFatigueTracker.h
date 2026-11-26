/**
 * @file WeaponFatigueTracker.h
 * @brief 同じ武器で当て続けると効きが落ちる仕組み（武器ごとの疲労）を定義するファイル
 */
#pragma once
#include "Vector4.h"
#include <vector>

namespace engine::game {

/**
 * @brief 武器ごとの疲労（0〜1）を数え、ダメージ倍率と命中演出の弱め方を決める
 * @note 添字は近接武器が WeaponManager::GetList() の順、その後ろに銃、さらに後ろにボスの枠の武器。
 *       1フレームに何体へ当てても、加算は Update() でまとめて1回だけ行う。
 *       調整値は Resources/Config/combat_tuning.json（CombatTuning）が持つ。
 */
class WeaponFatigueTracker {
public:
    /** @brief 銃の添字（近接武器の後ろ） */
    static int GunIndex();
    /** @brief ボスの枠の武器の添字（銃の次） */
    static int BossIndex();
    /** @brief 手に持っている近接武器の添字（素手なら -1。ボスの枠を選んでいればボスの枠の添字） */
    static int HeldMeleeIndex();

    /**
     * @brief 指定した武器の疲労から、今のダメージ倍率を返す
     * @param index 武器の添字（範囲の外なら 1.0）
     * @return 1.0（万全）〜 weaponFatigueMinDamageMult
     */
    float DamageMult(int index) const;
    /**
     * @brief このフレームに当てた武器を記録する
     * @param index 当てた武器の添字（負なら記録しない）
     * @param amount たまる疲労（ボスの枠の武器は倍率を掛ける）
     */
    void RegisterHit(int index, float amount);
    /** @brief 当てた武器へ疲労を足し、それ以外の武器の疲労を抜く（ヒット判定の後に毎フレーム呼ぶ） */
    void Update();

    /**
     * @brief 疲労で効きが落ちた命中の弾けを、威力に合わせて小さく灰色に寄せる
     * @param damageMult DamageMult() で求めた倍率
     * @param color 弾けの色（書き換える）
     * @param strength 弾けの大きさ（書き換える）
     */
    static void ApplyToHitEffect(float damageMult, Vector4& color, float& strength);

    /** @brief 最後に当てた武器の添字（まだ当てていなければ -1） */
    int LastIndex() const {
        return lastIndex_;
    }
    /** @brief 数えている武器の数 */
    int Count() const {
        return static_cast<int>(fatigue_.size());
    }
    /**
     * @brief 指定した武器の疲労（0〜1）
     * @param index 武器の添字（0 〜 Count() - 1）
     */
    float At(int index) const {
        return fatigue_[index];
    }

private:
    std::vector<float> fatigue_; ///< 武器ごとの疲労（0〜1）
    int lastIndex_ = -1; ///< 最後に当てた武器の添字（この武器だけは疲労が抜けない）
    int pendingIndex_ = -1; ///< このフレームに当てた武器の添字（複数の敵に同時に当てても1回ぶんにする）
    float pendingAmount_ = 0.0f; ///< このフレームにたまる疲労
    float idleTimer_ = 0.0f; ///< 最後に何かへ当ててからの経過秒数
};

} // namespace engine::game

/**
 * @file WeaponFinisherTuning.h
 * @brief 武器ごとの締めの動作（打ち上げて空中へ・突き飛ばし・衝撃波など）の調整値（weapon_finishers.json）
 */
#pragma once
#include "Weapon.h"
#include <array>
#include <string>

namespace engine::game {

/** @brief 地上コンボの最後の段が当たった時に出す、武器ごとの動作の種類 */
enum class WeaponFinishAction {
    kNone, ///< 何もしない（打ち上げて空中へ行く武器）
    kWarpBehind, ///< 一番近い敵の背後へ回り込む
    kThrust, ///< 前へ長く突き、当たった敵を横へ突き飛ばす
    kShockwave, ///< 地面を叩き、周りの地上の敵を衝撃波で崩す
    kSlamDown, ///< 前へ振り下ろし、浮いている敵は地面へ叩きつけ、地上の敵は長く硬直させる
    kPullSpin, ///< 回りながら周りの敵を手元へ引き寄せる
    kThrow, ///< 武器を投げ、貫通して戻ってくる
};

/** @brief 1武器ぶんの締めの動作の調整値（使う項目は動作の種類ごとに違う） */
struct WeaponFinisherData {
    bool launcher = true; ///< 下＋攻撃の打ち上げ技を使うか（false なら空中へは行かず、地上の締めで戦う）
    WeaponFinishAction action = WeaponFinishAction::kNone;
    std::string guide; ///< コンボ案内に出す、締めの動作の説明
    float damageMult = 1.0f; ///< 武器の威力に掛ける倍率
    float length = 0.0f; ///< 前へ届く長さ（突き・振り下ろし）
    float radius = 0.0f; ///< 届く半径（衝撃波・引き寄せ・回り込む相手を探す距離）
    float halfHeight = 1.0f; ///< 上下に届く半分の高さ
    float knockX = 0.0f; ///< 横へ飛ばす強さ
    float knockY = 0.0f; ///< 上へ浮かせる強さ（小さいと浮かない）
    float slamSpeed = 0.0f; ///< 浮いている敵を地面へ叩きつける速さ
    float stunSeconds = 0.0f; ///< 当たった敵を止めておく長さ
    float offset = 0.0f; ///< 回り込んだ時に敵から離れる距離・引き寄せた敵を置く距離
    // 投げる武器
    float throwSpeed = 16.0f; ///< 飛ぶ速さ
    float throwDistance = 7.0f; ///< 行きで進む距離
    float throwSpinSpeed = 18.0f; ///< 回る速さ（ラジアン毎秒）
    float throwHitRadius = 0.8f; ///< 敵に当たったとみなす半径
    float throwMaxSeconds = 3.0f; ///< これを過ぎたら消える
    float throwScale = 0.5f; ///< 見た目の大きさ
    // 演出（hit_effects.json のレシピ名。空なら出さない）
    std::string impact; ///< 出した瞬間の揺れと止め
    std::string ring; ///< 出した所から広がる輪
    std::string sparks; ///< 出した所の火花
};

/** @brief 武器ごとの締めの動作の置き場（初回に読み、Reload で読み直す） */
class WeaponFinisherTuning {
public:
    /** @brief その武器の調整値を返す */
    static const WeaponFinisherData& Get(WeaponType type);
    /** @brief weapon_finishers.json を読み直す（打ち上げ技の有無はコンボの表を作る時に一度だけ見る） */
    static void Reload();

private:
    /** @brief 武器の種類の並び順に持つ調整値 */
    static std::array<WeaponFinisherData, kWeaponTypeCount>& Mutable();
};

} // namespace engine::game

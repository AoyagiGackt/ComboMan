/**
 * @file MonsterTraitTuning.h
 * @brief モンスターごとの個性の動き（火を吐く・骨を投げる・跳ねる・急降下）の調整値（monster_traits.json）
 */
#pragma once
#include <map>
#include <string>

namespace engine::game {

/** @brief モンスターの個性の動きの種類 */
enum class MonsterTrait {
    kNone, ///< 個性の動きなし（ふつうの敵と同じ）
    kFireBreath, ///< 身構えてから、扇状に火の玉を吐く
    kBoneThrow, ///< 身構えてから、山なりに骨を投げる
    kHop, ///< 跳ねながら近づく
    kSwoop, ///< 身構えてから、プレイヤーへ急降下して元の高さへ戻る
};

/** @brief モンスター1種類ぶんの個性の調整値（使う項目は動きの種類ごとに違う） */
struct MonsterTraitData {
    MonsterTrait trait = MonsterTrait::kNone;
    float interval = 3.0f; ///< 次の動きまでの秒数
    float range = 8.0f; ///< プレイヤーがこの距離より近い時だけ動く
    float telegraphSeconds = 0.6f; ///< 動く前に身構える秒数（跳ねるは使わない）
    int damage = 1; ///< 当たった時のダメージ
    // 火の玉・骨
    int count = 1; ///< 一度に出す数
    float spreadDegrees = 0.0f; ///< 扇状に広げる角度（隣どうしの差）
    float speed = 6.0f; ///< 火の玉の速さ・骨を投げる横の速さ
    float riseSpeed = 0.0f; ///< 骨を投げる上向きの速さ
    float gravity = 0.0f; ///< 骨が落ちる加速度
    float lifetime = 2.0f; ///< 飛んでいる秒数
    float launchHeight = 0.6f; ///< 足元からこの高さで出す
    // 跳ねる
    float hopX = 0.0f; ///< 横へ跳ぶ強さ
    float hopY = 0.0f; ///< 上へ跳ぶ強さ
    // 急降下
    float swoopSeconds = 0.45f; ///< プレイヤーの所まで降りる秒数
    float returnSeconds = 0.6f; ///< 元の高さへ戻る秒数
    // 演出（hit_effects.json のレシピ名。空なら出さない）
    std::string warningRing; ///< 身構えている間に出す輪
    std::string actSparks; ///< 動いた瞬間の火花
    std::string attackSparks; ///< 近接攻撃を出した瞬間の粒
    std::string attackRing; ///< 近接攻撃を出した瞬間の輪
    std::string attackDrops; ///< 近接攻撃で高く飛び散る大きな粒
    std::string landSparks; ///< 着地した瞬間の粒
    std::string landRing; ///< 着地した瞬間の輪
};

/** @brief モンスターの個性の置き場（初回に読み、Reload で読み直す） */
class MonsterTraitTuning {
public:
    /** @brief モンスターの種類の個性を返す（書いていない種類は個性なし） */
    static const MonsterTraitData& Get(const std::string& monsterKind);
    /** @brief monster_traits.json を読み直す */
    static void Reload();

private:
    /** @brief モンスターの種類の名前ごとの調整値 */
    static std::map<std::string, MonsterTraitData>& Mutable();
};

} // namespace engine::game

/**
 * @file Weapon.h
 * @brief 武器・戦闘スタイルのデータ構造を定義するファイル（実データは Resources/Config/weapons.json）
 */
#pragma once
#include <cstddef>
#include <string>
#include <vector>
namespace engine::game {
/** @brief 近接武器の種類（WeaponManagerが読み込むJSONの"type"と対応） */
enum class WeaponType { kSword,
    kSpear,
    kHammer,
    kDagger,
    kBall,
    kGreatsword,
    kScythe,
    kAxe };

/** @brief WeaponType の要素数（種別ごとの表はこの長さで WeaponType の並び順に持つ） */
inline constexpr std::size_t kWeaponTypeCount = static_cast<std::size_t>(WeaponType::kAxe) + 1;

/** @brief 近接武器種と設定ファイル内の識別名の対応 */
struct NamedWeaponType {
    const char* name;
    WeaponType type;
    const wchar_t* label; ///< 画面に出す日本語の名前
};

/** @brief 武器種別名の一覧（weapons.jsonの"type"・レベルJSONのweaponType・game_rules.jsonと同じ文字列規約） */
inline constexpr NamedWeaponType kWeaponTypeNames[] = {
    { "Sword", WeaponType::kSword, L"剣" },
    { "Spear", WeaponType::kSpear, L"槍" },
    { "Hammer", WeaponType::kHammer, L"ハンマー" },
    { "Dagger", WeaponType::kDagger, L"短剣" },
    { "Ball", WeaponType::kBall, L"ボール" },
    { "Greatsword", WeaponType::kGreatsword, L"大剣" },
    { "Scythe", WeaponType::kScythe, L"鎌" },
    { "Axe", WeaponType::kAxe, L"斧" },
};

/**
 * @brief 武器種別名をWeaponTypeへ変換する
 * @param type "Sword" "Spear" "Hammer" "Dagger" "Ball" "Greatsword" "Scythe" "Axe" のいずれか
 * @return 対応する種別。未知の文字列はSword
 */
inline WeaponType ParseWeaponTypeName(const std::string& type) {
    for (const NamedWeaponType& entry : kWeaponTypeNames) {
        if (type == entry.name) {
            return entry.type;
        }
    }
    return WeaponType::kSword;
}

/** @brief WeaponTypeを武器種別名へ変換する（ParseWeaponTypeName の逆） */
inline const char* WeaponTypeName(WeaponType type) {
    for (const NamedWeaponType& entry : kWeaponTypeNames) {
        if (type == entry.type) {
            return entry.name;
        }
    }
    return kWeaponTypeNames[0].name;
}

/** @brief WeaponTypeを画面に出す日本語の名前へ変換する（剣・槍など） */
inline const wchar_t* WeaponTypeLabel(WeaponType type) {
    for (const NamedWeaponType& entry : kWeaponTypeNames) {
        if (type == entry.type) {
            return entry.label;
        }
    }
    return kWeaponTypeNames[0].label;
}

/** @brief 操作説明HUDに表示する1コマンドぶんのキー表示と説明 */
struct WeaponCommand {
    std::string key; ///< ASCII キー表示（例: "Space"）
    std::wstring desc; ///< 日本語説明
};

/**
 * @brief 覚醒中にその武器がどう強化されるかの倍率（weapons.jsonの"awakened"）
 * @note 覚醒中は武器ごとに手触りが変わる、というコンセプトの最小単位。数値だけで武器ごとの個性を出す
 */
struct AwakenedWeaponMods {
    float damageMult = 1.4f; ///< 近接・固有技・銃ダメージの倍率
    float skillRadiusMult = 1.5f; ///< 固有技の判定範囲の倍率
    float skillCooldownMult = 0.5f; ///< 固有技クールタイムの倍率（<1で短くなる）
    float knockbackMult = 1.3f; ///< ノックバック倍率（覚醒中は敵が大きく飛ぶ）
};

/** @brief 1スタイルぶんの近接武器データ */
struct WeaponData {
    std::string name;
    WeaponType type;
    float damage;
    float range;
    float attackInterval;
    std::string description;
    float styleColor[4]; ///< RGBA 0.0~1.0
    float knockbackMult = 1.0f; ///< ノックバック倍率（武器ごとの差別化）
    std::string element = "None"; ///< 炎や氷など演出と追加効果を識別する属性名
    float effectColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f }; ///< ヒット時の属性色
    int effectBurstCount = 0; ///< ヒット時に放出する属性粒子数
    float effectRingRadius = 0.0f; ///< ヒット時に広げる属性リング半径
    std::string hitStyle; ///< 当たった時の演出と音の種類（hit_effects.json / sounds.json の 名前_hitStyle を使う。空なら共通の演出だけ）
    AwakenedWeaponMods awakened; ///< 覚醒中の強化倍率
    std::vector<WeaponCommand> commands;
};

/** @brief 射撃武器の種類（GunComboが読み込むJSONの"type"と対応） */
enum class GunType { kPistol,
    kMagnum,
    kSMG,
    kShotgun,
    kRailgun };

// 銃の種類を設定名へ変換する
inline const char* GunTypeName(GunType type) {
    switch (type) {
    case GunType::kPistol: return "Pistol";
    case GunType::kMagnum: return "Magnum";
    case GunType::kSMG: return "SMG";
    case GunType::kShotgun: return "Shotgun";
    case GunType::kRailgun: return "Railgun";
    }
    return "Pistol";
}

/** @brief GunType の要素数（銃種ごとの表はこの長さで GunType の並び順に持つ） */
inline constexpr std::size_t kGunTypeCount = static_cast<std::size_t>(GunType::kRailgun) + 1;

/** @brief 1丁ぶんの射撃武器データ（Gキーで切り替え、コンボは GunCombo.cpp のテーブルが持つ） */
struct RangedWeaponData {
    std::string name;
    std::wstring nameJp; ///< HUD表示用の日本語名
    GunType type;
    float damage;
    float range;
    float attackInterval;
    std::string description;
    float color[4]; ///< RGBA 0.0~1.0（マズルフラッシュ・弾エフェクトの色）
    std::vector<WeaponCommand> commands;
    std::string element = "None"; ///< 着弾演出の属性名（近接武器のelementと同じ規約）
    float effectColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f }; ///< 着弾演出の属性色（未指定ならcolorを使う）
};

} // namespace engine::game

/**
 * @file MeleeBossCombo.cpp
 * @brief ボスの武器を自分で振る時の専用のコンボの表（ボスがその武器で使っていた技を元にした、地上・空中・打ち上げ）
 * @note 段の動き（combo_motion.json）は repos/tools/combo_motion/forms_boss.js の型から gen.js で作る。
 *       技の ID の頭（bhm bsp bdg bgs bsc bax）で gen.js が武器のリズムを選ぶので、ID を変える時は gen.js の WEAPON_OF_PREFIX も合わせる
 */
#include "MeleeCombo.h"
#include "GameConstants.h"
#include "Weapon.h"
#include "WeaponFinisherTuning.h"
#include <array>

namespace engine::game {
namespace {
constexpr float kDeg = gameConstants::kDegToRad;

// 並びは MeleeAttackDef と同じ:
// id, damageMult, duration, hitTime, cancelTime, lungeDist, rangeMult, knockX, knockY, launcher, hitStop, animSpeed, slashAnim,
// swingFrom, swingTo, bodyLeanFrom, bodyLeanTo, bodyTurns

// ── ボスのハンマー  担ぎ落とし→旋回打ち→地割り。空中は振り下ろしと横殴り ──
constexpr MeleeAttackDef kBossHammerGround[] = {
    { "bhm1", 1.1f, 0.62f, 0.28f, 0.36f, 0.50f, 1.05f, 0.25f, 0.08f, false, 9, 1.0f, false, { -160 * kDeg, 0, 0 }, { 70 * kDeg, 0, 0 }, { -30 * kDeg, 0, 0 }, { 36 * kDeg, 0, 0 } },
    { "bhm2", 1.4f, 0.80f, 0.34f, 0.52f, 0.60f, 1.30f, 0.45f, 0.12f, false, 12, 0.95f, false, { 0, 0, 160 * kDeg }, { 0, 0, -160 * kDeg }, { -8 * kDeg, 0, 22 * kDeg }, { 14 * kDeg, 0, -26 * kDeg }, 1.0f },
    { "bhm3", 2.4f, 1.00f, 0.50f, 0.74f, 0.90f, 1.45f, 0.55f, 0.30f, false, 18, 0.9f, false, { -175 * kDeg, 0, 0 }, { 100 * kDeg, 0, 0 }, { -36 * kDeg, 0, 0 }, { 46 * kDeg, 0, 0 } },
};
constexpr MeleeAttackDef kBossHammerAir[] = {
    { "bhm_a2", 1.2f, 0.52f, 0.22f, 0.36f, 0.0f, 1.20f, 0.30f, 0.10f, false, 9, 1.0f, false, { 0, 0, 150 * kDeg }, { 0, 0, -150 * kDeg }, { 0, 0, 18 * kDeg }, { 0, 0, -18 * kDeg } },
    { "bhm_a1", 1.6f, 0.60f, 0.24f, 0.44f, 0.0f, 1.15f, 0.15f, -0.35f, false, 12, 1.0f, false, { -175 * kDeg, 0, 0 }, { 95 * kDeg, 0, 0 }, { -18 * kDeg, 0, 0 }, { 34 * kDeg, 0, 0 } },
};
constexpr MeleeAttackDef kBossHammerLauncher = { "bhm_lau", 1.3f, 0.78f, 0.36f, 0.56f, 0.45f, 1.10f, 0.08f, 0.44f, true, 12, 1.0f, false, { 95 * kDeg, 0, 0 }, { -150 * kDeg, 0, 0 }, { -28 * kDeg, 0, 0 }, { 32 * kDeg, 0, 0 } };

// ── ボスの槍  速突き2段→低空突進→旋回薙ぎ→天穿ち。空中は薙ぎと突き落とし ──
constexpr MeleeAttackDef kBossSpearGround[] = {
    { "bsp1", 0.9f, 0.26f, 0.09f, 0.13f, 0.60f, 1.25f, 0.10f, 0.02f, false, 2, 2.1f, true, { -80 * kDeg, 0, 0 }, { 75 * kDeg, 0, 0 }, { -12 * kDeg, 0, 0 }, { 20 * kDeg, 0, 0 } },
    { "bsp2", 0.9f, 0.26f, 0.09f, 0.13f, 0.60f, 1.25f, 0.10f, 0.02f, false, 2, 2.1f, true, { -90 * kDeg, 0, 0 }, { 70 * kDeg, 0, 0 }, { -14 * kDeg, 0, 0 }, { 22 * kDeg, 0, 0 } },
    { "bsp3", 1.3f, 0.44f, 0.18f, 0.28f, 2.20f, 1.40f, 0.30f, 0.04f, false, 6, 1.6f, true, { -100 * kDeg, 0, 0 }, { 85 * kDeg, 0, 0 }, { -30 * kDeg, 0, 0 }, { 34 * kDeg, 0, 0 } },
    { "bsp4", 1.3f, 0.48f, 0.18f, 0.30f, 0.30f, 1.70f, 0.30f, 0.10f, false, 6, 1.5f, true, { 0, 0, 165 * kDeg }, { 0, 0, -165 * kDeg }, { 0, 0, 26 * kDeg }, { 0, 0, -26 * kDeg }, 1.0f },
    { "bsp5", 2.2f, 0.62f, 0.30f, 0.44f, 2.60f, 1.90f, 0.65f, 0.10f, false, 12, 1.3f, true, { -120 * kDeg, 0, 0 }, { 90 * kDeg, 0, 0 }, { -34 * kDeg, 0, 0 }, { 40 * kDeg, 0, 0 } },
};
constexpr MeleeAttackDef kBossSpearAir[] = {
    { "bsp_a1", 1.1f, 0.30f, 0.10f, 0.19f, 0.0f, 1.25f, 0.10f, 0.12f, false, 3, 1.9f, true, { 0, 0, 140 * kDeg }, { 0, 0, -140 * kDeg }, { 0, 0, 18 * kDeg }, { 0, 0, -18 * kDeg } },
    { "bsp_a2", 1.5f, 0.42f, 0.14f, 0.30f, 0.0f, 1.35f, 0.20f, -0.28f, false, 6, 1.7f, true, { -40 * kDeg, 0, 0 }, { 115 * kDeg, 0, 0 }, { 10 * kDeg, 0, 0 }, { 36 * kDeg, 0, 0 } },
};
constexpr MeleeAttackDef kBossSpearLauncher = { "bsp_lau", 1.3f, 0.48f, 0.16f, 0.32f, 0.60f, 1.25f, 0.04f, 0.32f, true, 8, 1.6f, true, { 60 * kDeg, 0, 0 }, { -115 * kDeg, 0, 0 }, { -20 * kDeg, 0, 0 }, { 30 * kDeg, 0, 0 } };

// ── ボスの短剣  逆手斬り3段→旋風（回りながら駆け抜ける）→宙返り突き。空中は回し斬りと突き落とし ──
constexpr MeleeAttackDef kBossDaggerGround[] = {
    { "bdg1", 0.9f, 0.18f, 0.05f, 0.07f, 0.40f, 1.0f, 0.07f, 0.03f, false, 1, 2.6f, true, { 0, 0, 75 * kDeg }, { 0, 0, -65 * kDeg }, { 0, 0, 16 * kDeg }, { 0, 0, -12 * kDeg } },
    { "bdg2", 0.9f, 0.18f, 0.05f, 0.07f, 0.40f, 1.0f, 0.07f, 0.03f, false, 1, 2.6f, true, { 0, 0, -75 * kDeg }, { 0, 0, 65 * kDeg }, { 0, 0, -16 * kDeg }, { 0, 0, 12 * kDeg } },
    { "bdg3", 0.9f, 0.18f, 0.05f, 0.07f, 0.40f, 1.05f, 0.07f, 0.03f, false, 1, 2.6f, true, { -45 * kDeg, 0, 0 }, { 55 * kDeg, 0, 0 }, { 10 * kDeg, 0, 0 }, { 24 * kDeg, 0, 0 } },
    { "bdg4", 1.3f, 0.36f, 0.14f, 0.22f, 2.00f, 1.15f, 0.20f, 0.06f, false, 4, 2.0f, true, { 0, 0, -150 * kDeg }, { 0, 0, 150 * kDeg }, { 16 * kDeg, 0, 0 }, { -6 * kDeg, 0, 0 }, 2.0f },
    { "bdg5", 2.0f, 0.46f, 0.20f, 0.32f, 1.00f, 1.25f, 0.40f, 0.12f, false, 9, 1.8f, true, { -120 * kDeg, 0, 0 }, { 80 * kDeg, 0, 0 }, { -40 * kDeg, 0, 0 }, { 50 * kDeg, 0, 0 } },
};
constexpr MeleeAttackDef kBossDaggerAir[] = {
    { "bdg_a1", 1.0f, 0.18f, 0.05f, 0.10f, 0.0f, 1.0f, 0.05f, 0.10f, false, 2, 2.4f, true, { 0, 0, 70 * kDeg }, { 0, 0, -60 * kDeg }, { 0, 0, 14 * kDeg }, { 0, 0, -11 * kDeg }, 1.0f },
    { "bdg_a2", 1.6f, 0.32f, 0.10f, 0.22f, 0.0f, 1.05f, 0.12f, -0.30f, false, 6, 2.0f, true, { -95 * kDeg, 0, 0 }, { 75 * kDeg, 0, 0 }, { 12 * kDeg, 0, 0 }, { 34 * kDeg, 0, 0 } },
};
constexpr MeleeAttackDef kBossDaggerLauncher = { "bdg_lau", 1.2f, 0.34f, 0.10f, 0.22f, 0.50f, 1.0f, 0.03f, 0.30f, true, 6, 2.1f, true, { 75 * kDeg, 0, 0 }, { -115 * kDeg, 0, 0 }, { -18 * kDeg, 0, 0 }, { 24 * kDeg, 0, 0 } };

// ── ボスの大剣  正眼斬り→すくい上げ→大薙ぎ→跳び割り。空中は横薙ぎと叩き落とし ──
constexpr MeleeAttackDef kBossGreatswordGround[] = {
    { "bgs1", 1.1f, 0.60f, 0.28f, 0.36f, 0.60f, 1.15f, 0.30f, 0.08f, false, 9, 1.0f, true, { -165 * kDeg, 0, 0 }, { 75 * kDeg, 0, 0 }, { -28 * kDeg, 0, 0 }, { 36 * kDeg, 0, 0 } },
    { "bgs2", 1.25f, 0.62f, 0.28f, 0.42f, 0.50f, 1.20f, 0.18f, 0.24f, false, 10, 1.0f, true, { 75 * kDeg, 0, -60 * kDeg }, { -135 * kDeg, 0, 80 * kDeg }, { 16 * kDeg, 0, -20 * kDeg }, { -26 * kDeg, 0, 22 * kDeg } },
    { "bgs3", 1.4f, 0.80f, 0.34f, 0.54f, 0.60f, 1.45f, 0.40f, 0.10f, false, 12, 0.95f, true, { 0, 0, 170 * kDeg }, { 0, 0, -170 * kDeg }, { 0, 0, 28 * kDeg }, { 0, 0, -28 * kDeg }, 1.0f },
    { "bgs4", 2.6f, 1.05f, 0.52f, 0.78f, 1.40f, 1.40f, 0.65f, 0.20f, false, 18, 0.9f, true, { -178 * kDeg, 0, 0 }, { 100 * kDeg, 0, 0 }, { -38 * kDeg, 0, 0 }, { 46 * kDeg, 0, 0 } },
};
constexpr MeleeAttackDef kBossGreatswordAir[] = {
    { "bgs_a1", 1.2f, 0.50f, 0.20f, 0.34f, 0.0f, 1.25f, 0.15f, 0.10f, false, 8, 1.0f, true, { 0, 0, 150 * kDeg }, { 0, 0, -150 * kDeg }, { 0, 0, 20 * kDeg }, { 0, 0, -20 * kDeg } },
    { "bgs_a2", 1.7f, 0.60f, 0.24f, 0.44f, 0.0f, 1.20f, 0.25f, -0.24f, false, 11, 1.0f, true, { -165 * kDeg, 0, 0 }, { 95 * kDeg, 0, 0 }, { -16 * kDeg, 0, 0 }, { 30 * kDeg, 0, 0 } },
};
constexpr MeleeAttackDef kBossGreatswordLauncher = { "bgs_lau", 1.5f, 0.70f, 0.32f, 0.52f, 0.50f, 1.20f, 0.10f, 0.38f, true, 12, 0.95f, true, { 85 * kDeg, 0, 0 }, { -145 * kDeg, 0, 0 }, { -26 * kDeg, 0, 0 }, { 32 * kDeg, 0, 0 } };

// ── ボスの鎌  刈り寄せ2段（手前へ引き寄せる）→頭上回し→二回転の薙ぎ払い。空中は刈り寄せと振り下ろし ──
constexpr MeleeAttackDef kBossScytheGround[] = {
    { "bsc1", 1.0f, 0.42f, 0.15f, 0.21f, 0.50f, 1.40f, -0.12f, 0.06f, false, 4, 1.6f, true, { -40 * kDeg, 0, 130 * kDeg }, { 50 * kDeg, 0, -110 * kDeg }, { 0, 0, 26 * kDeg }, { 10 * kDeg, 0, -22 * kDeg } },
    { "bsc2", 1.0f, 0.42f, 0.15f, 0.21f, 0.40f, 1.40f, -0.12f, 0.06f, false, 4, 1.6f, true, { 40 * kDeg, 0, -130 * kDeg }, { -30 * kDeg, 0, 110 * kDeg }, { 0, 0, -26 * kDeg }, { -6 * kDeg, 0, 22 * kDeg } },
    { "bsc3", 1.3f, 0.58f, 0.22f, 0.36f, 0.30f, 1.55f, 0.10f, 0.18f, false, 7, 1.4f, true, { 170 * kDeg, 0, 0 }, { -170 * kDeg, 0, 0 }, { -14 * kDeg, 0, 0 }, { 18 * kDeg, 0, 0 } },
    { "bsc4", 2.3f, 0.90f, 0.36f, 0.62f, 0.40f, 1.75f, 0.50f, 0.16f, false, 12, 1.2f, true, { 0, 0, 178 * kDeg }, { 0, 0, -178 * kDeg }, { 0, 0, 32 * kDeg }, { 0, 0, -32 * kDeg }, 2.0f },
};
constexpr MeleeAttackDef kBossScytheAir[] = {
    { "bsc_a1", 1.1f, 0.36f, 0.12f, 0.22f, 0.0f, 1.40f, -0.08f, 0.12f, false, 4, 1.6f, true, { 0, 0, 125 * kDeg }, { 0, 0, -105 * kDeg }, { 0, 0, 24 * kDeg }, { 0, 0, -20 * kDeg }, 1.0f },
    { "bsc_a2", 1.6f, 0.52f, 0.18f, 0.38f, 0.0f, 1.50f, 0.18f, -0.22f, false, 8, 1.4f, true, { -155 * kDeg, 0, 0 }, { 105 * kDeg, 0, 0 }, { -14 * kDeg, 0, 0 }, { 32 * kDeg, 0, 0 } },
};
constexpr MeleeAttackDef kBossScytheLauncher = { "bsc_lau", 1.2f, 0.42f, 0.13f, 0.28f, 0.50f, 1.35f, 0.02f, 0.44f, true, 9, 1.6f, true, { 170 * kDeg, 0, 0 }, { -170 * kDeg, 0, 0 }, { -22 * kDeg, 0, 0 }, { 28 * kDeg, 0, 0 } };

// ── ボスの斧  叩き割り2段→突撃斬り→ひねり大回転。空中は兜割りと二段割り ──
constexpr MeleeAttackDef kBossAxeGround[] = {
    { "bax1", 1.1f, 0.48f, 0.20f, 0.26f, 0.80f, 1.05f, 0.28f, 0.06f, false, 7, 1.3f, true, { -125 * kDeg, 0, 60 * kDeg }, { 65 * kDeg, 0, -40 * kDeg }, { -18 * kDeg, 0, 14 * kDeg }, { 28 * kDeg, 0, -10 * kDeg } },
    { "bax2", 1.2f, 0.48f, 0.18f, 0.30f, 0.60f, 1.05f, 0.28f, 0.06f, false, 7, 1.3f, true, { -125 * kDeg, 0, -60 * kDeg }, { 65 * kDeg, 0, 40 * kDeg }, { -18 * kDeg, 0, -14 * kDeg }, { 28 * kDeg, 0, 10 * kDeg } },
    { "bax3", 1.5f, 0.56f, 0.24f, 0.38f, 2.00f, 1.15f, 0.40f, 0.10f, false, 9, 1.2f, true, { -150 * kDeg, 0, 0 }, { 90 * kDeg, 0, 0 }, { -30 * kDeg, 0, 0 }, { 40 * kDeg, 0, 0 } },
    { "bax4", 2.3f, 0.98f, 0.38f, 0.68f, 1.20f, 1.35f, 0.55f, 0.16f, false, 12, 1.1f, true, { 0, 0, -180 * kDeg }, { 0, 0, 180 * kDeg }, { 10 * kDeg, 0, -22 * kDeg }, { 10 * kDeg, 0, 22 * kDeg }, -2.0f },
};
constexpr MeleeAttackDef kBossAxeAir[] = {
    { "bax_a1", 1.2f, 0.42f, 0.15f, 0.28f, 0.0f, 1.05f, 0.14f, 0.10f, false, 5, 1.4f, true, { -165 * kDeg, 0, 0 }, { 85 * kDeg, 0, 0 }, { -16 * kDeg, 0, 0 }, { 28 * kDeg, 0, 0 } },
    { "bax_a2", 1.7f, 0.55f, 0.20f, 0.40f, 0.0f, 1.15f, 0.28f, -0.20f, false, 9, 1.2f, true, { -145 * kDeg, 0, 0 }, { 105 * kDeg, 0, 0 }, { -16 * kDeg, 0, 0 }, { 30 * kDeg, 0, 0 } },
};
constexpr MeleeAttackDef kBossAxeLauncher = { "bax_lau", 1.3f, 0.55f, 0.20f, 0.38f, 0.55f, 1.05f, 0.06f, 0.34f, true, 9, 1.2f, true, { 85 * kDeg, 0, 0 }, { -145 * kDeg, 0, 0 }, { -22 * kDeg, 0, 0 }, { 28 * kDeg, 0, 0 } };

/** @brief 表の配列から、コンボの見方（地上・空中・打ち上げ）を作る */
template <size_t GroundCount, size_t AirCount>
constexpr MeleeComboSet MakeBossSet(const MeleeAttackDef (&ground)[GroundCount], const MeleeAttackDef (&air)[AirCount], const MeleeAttackDef& launcher) {
    return { { ground, static_cast<int>(GroundCount) }, { air, static_cast<int>(AirCount) }, &launcher };
}

constexpr MeleeComboSet kBossHammerSet = MakeBossSet(kBossHammerGround, kBossHammerAir, kBossHammerLauncher);
constexpr MeleeComboSet kBossSpearSet = MakeBossSet(kBossSpearGround, kBossSpearAir, kBossSpearLauncher);
constexpr MeleeComboSet kBossDaggerSet = MakeBossSet(kBossDaggerGround, kBossDaggerAir, kBossDaggerLauncher);
constexpr MeleeComboSet kBossGreatswordSet = MakeBossSet(kBossGreatswordGround, kBossGreatswordAir, kBossGreatswordLauncher);
constexpr MeleeComboSet kBossScytheSet = MakeBossSet(kBossScytheGround, kBossScytheAir, kBossScytheLauncher);
constexpr MeleeComboSet kBossAxeSet = MakeBossSet(kBossAxeGround, kBossAxeAir, kBossAxeLauncher);

// WeaponType の並び順。ボスの武器が無い種類（刀など）は nullptr で、ふつうのコンボを使う
constexpr std::array<const MeleeComboSet*, kWeaponTypeCount> kBossSourceSets = {
    nullptr, // Sword（ラスボスの刀は奪わない）
    &kBossSpearSet, // Spear
    &kBossHammerSet, // Hammer
    &kBossDaggerSet, // Dagger
    nullptr, // Ball
    &kBossGreatswordSet, // Greatsword
    &kBossScytheSet, // Scythe
    &kBossAxeSet, // Axe
};
} // namespace

/** @brief ボスの武器を自分で振る時のコンボを返す（その種類に専用のコンボが無ければ nullptr） */
const MeleeComboSet* GetBossMeleeComboSet(WeaponType type) {
    // 打ち上げて空中へ行かない武器（weapon_finishers.json の launcher=false）は、ふつうのコンボと同じく打ち上げ技を持たない
    static const std::array<MeleeComboSet, kWeaponTypeCount> kSets = [] {
        std::array<MeleeComboSet, kWeaponTypeCount> sets {};
        for (size_t i = 0; i < kWeaponTypeCount; ++i) {
            if (kBossSourceSets[i] == nullptr) {
                continue;
            }
            sets[i] = *kBossSourceSets[i];
            if (!WeaponFinisherTuning::Get(static_cast<WeaponType>(i)).launcher) {
                sets[i].launcher = nullptr;
            }
        }
        return sets;
    }();
    const size_t index = static_cast<size_t>(type);
    if (index >= kBossSourceSets.size() || kBossSourceSets[index] == nullptr) {
        return nullptr;
    }
    return &kSets[index];
}

/** @brief 武器の種類に合ったコンボの定義を返す（ボスの武器を振っていて専用のコンボがあれば、そちら） */
const MeleeComboSet& GetMeleeComboSet(WeaponType type, bool bossWeapon) {
    if (bossWeapon) {
        if (const MeleeComboSet* boss = GetBossMeleeComboSet(type)) {
            return *boss;
        }
    }
    return GetMeleeComboSet(type);
}

} // namespace engine::game

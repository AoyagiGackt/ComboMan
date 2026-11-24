/**
 * @file ActionFeelTuning.cpp
 * @brief アクションの手応えの調整値の読み込み
 */
#include "ActionFeelTuning.h"
#include "JsonFieldTable.h"
#include "JsonHelper.h"

namespace engine::game {
namespace {
constexpr const char* kTuningPath = "Resources/Config/action_feel.json";

/** @brief action_feel.json の項目 */
constexpr JsonField<ActionFeelTuning> kFields[] = {
    { "collide", "enabled", &ActionFeelTuning::collideEnabled },
    { "collide", "minSpeed", &ActionFeelTuning::collideMinSpeed },
    { "collide", "damage", &ActionFeelTuning::collideDamage },
    { "collide", "knockY", &ActionFeelTuning::collideKnockY },
    { "collide", "maxChain", &ActionFeelTuning::collideMaxChain },
    { "collide", "flyerSpeedKeep", &ActionFeelTuning::collideFlyerSpeedKeep },
    { "collide", "styleGain", &ActionFeelTuning::collideStyleGain },
    { "bounce", "enabled", &ActionFeelTuning::bounceEnabled },
    { "bounce", "wallMinSpeed", &ActionFeelTuning::wallBounceMinSpeed },
    { "bounce", "wallFactor", &ActionFeelTuning::wallBounceFactor },
    { "bounce", "wallLift", &ActionFeelTuning::wallBounceLift },
    { "bounce", "wallCount", &ActionFeelTuning::wallBounceCount },
    { "bounce", "wallDamage", &ActionFeelTuning::wallBounceDamage },
    { "bounce", "floorMinFallSpeed", &ActionFeelTuning::floorBounceMinFallSpeed },
    { "bounce", "floorFactor", &ActionFeelTuning::floorBounceFactor },
    { "bounce", "floorCount", &ActionFeelTuning::floorBounceCount },
    { "bounce", "styleGain", &ActionFeelTuning::bounceStyleGain },
    { "roomClear", "enabled", &ActionFeelTuning::roomClearEnabled },
    { "roomClear", "slowSeconds", &ActionFeelTuning::roomClearSlowSeconds },
    { "roomClear", "slowScale", &ActionFeelTuning::roomClearSlowScale },
    { "dodgeSlow", "enabled", &ActionFeelTuning::dodgeSlowEnabled },
    { "dodgeSlow", "seconds", &ActionFeelTuning::dodgeSlowSeconds },
    { "dodgeSlow", "scale", &ActionFeelTuning::dodgeSlowScale },
    { "dodgeSlow", "radius", &ActionFeelTuning::dodgeSlowRadius },
    { "rumble", "perShake", &ActionFeelTuning::rumblePerShake },
    { "rumble", "highRatio", &ActionFeelTuning::rumbleHighRatio },
    { "rumble", "minSeconds", &ActionFeelTuning::rumbleMinSeconds },
    { "rumble", "scale", &ActionFeelTuning::rumbleScale },
};
} // namespace

/** @brief アクションの手応えの調整値を返す（初回だけファイルを読み、以後は覚えた値を返す） */
const ActionFeelTuning& ActionFeel() {
    static const ActionFeelTuning kTuning = ReadJsonFields(JsonHelper::Load(kTuningPath), kFields);
    return kTuning;
}

} // namespace engine::game

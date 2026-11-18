/**
 * @file GamePlaySceneParticles.cpp
 * @brief 戦闘と武器のパーティクル演出
 * @note 数・色・寿命などの見た目は Resources/Config/hit_effects.json のレシピ、
 *       連続ヒットで盛る度合いなどの手触りは combat_tuning.json が持つ
 */
#include "GamePlayScene.h"
#include "GameSounds.h"
#include "AudioBridge.h"
#include "CombatTuning.h"
#include "ElementEffect.h"
#include "EnemyTuning.h"
#include "GameConstants.h"
#include "GameFlags.h"
#include "GameRules.h"
#include "GamePlaySceneInitializer.h"
#include "GrayscaleEffect.h"
#include "HitEffectLibrary.h"
#include "HsvFilter.h"
#include "ImGuiControlPanel.h"
#include "ImageFilter.h"
#include "ParticleManager.h"
#include "PipelineStateGuard.h"
#include "PlayerBridge.h"
#include "PostEffectRenderTarget.h"
#include "RunData.h"
#include "SaveData.h"
#include "SceneManager.h"
#include "ScreenFlash.h"
#include "SlashMark.h"
#include "StageEditor.h"
#include "StringUtility.h"
#include "TextureManager.h"
#include "WeaponManager.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <random>
#include <span>
using namespace engine::graphics;

// スタイルゲージ・覚醒ゲージ・ヒット判定範囲の調整値はResources/Config/combat_tuning.json（CombatTuning）で持つ
namespace engine::game {

namespace {

/** @brief 固有技の発動ごとに出す見た目（どの出来事で、どのレシピを出すか） */
struct SkillCastEffect {
    bool (Player::*happened)() const;
    bool impact; ///< 胸の高さで大きな一撃を出すか
    const char* ring; ///< 足元に広げるリング（無ければ nullptr）
    const char* slash; ///< 前後2方向へ重ねる斬線（無ければ nullptr）
    const char* trail; ///< 手元に残す残像（無ければ nullptr）
};
constexpr SkillCastEffect kSkillCastEffects[] = {
    { &Player::JustGreatswordSlam, true, "skill_slam_base", nullptr, nullptr }, // 大地砕き: 足元に衝撃波
    { &Player::JustSpinShot, false, "spin_shot", nullptr, nullptr }, // スピン連射: 全周へ銃口色の輪
    { &Player::JustScytheSpin, true, nullptr, "scythe_spin", nullptr }, // 回転斬り: 前後の斬線で円を描く
    { &Player::JustAxeCharge, true, nullptr, nullptr, "axe_charge" }, // バーサーク突進: 強めの残像
};

} // namespace

/** @brief UpdateParticles()の下請け 格闘/射撃/瞬歩/覚醒ゲージ・覚醒発動・ランク上昇のスタイル演出パーティクルを更新する */
void GamePlayScene::UpdateStyleTechniqueParticles(float dt) {
    const Vector3& ppos = player_->GetPosition();

    EmitComboHitParticles(ppos);
    EmitGunFireParticles(ppos);
    EmitWeaponSkillCastParticles(ppos);
    EmitAwakenParticles(ppos, dt);
    EmitStyleRankUpParticles(ppos);
    // 乱舞で相手へ突進している間は、足元に覚醒色の土煙を残して走っていることを見せる
    if (player_->IsRampageDashing()) {
        const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
        fx.Sparks("rampage_dash", ppos);
    }
}

/** @brief 敵に攻撃が当たった位置へ星・リング・火花を出す（どの攻撃が敵に入ったかを敵側で見せる） */
void GamePlayScene::EmitEnemyHitEffect(const Vector3& enemyPos, const Vector4& color, float strength,
    int extraBurstCount, float ringRadius) {
    const auto& visuals = HitEffectLibrary::GetInstance()->SceneParticles();
    // 当て続けるほど盛る（途切れずに当てた手応えを命中演出の量で返す）
    const CombatTuningData& tune = CombatTune();
    ++hitEscalationCount_;
    hitEscalationTimer_ = tune.hitEscalationKeepSeconds;
    const float escalation = HitEscalationRatio();
    strength *= 1.0f + tune.hitEscalationStrengthGain * escalation;
    extraBurstCount += static_cast<int>(static_cast<float>(extraBurstCount) * tune.hitEscalationExtraBurstGain * escalation);
    const int extraSparks = static_cast<int>(static_cast<float>(tune.hitEscalationExtraSparks) * escalation);

    const Vector3& ppos = player_->GetPosition();
    const Vector3 hitPos = {
        enemyPos.x + (ppos.x - enemyPos.x) * visuals.surfaceLerp,
        enemyPos.y + (ppos.y - enemyPos.y) * visuals.surfaceLerp,
        0.0f
    };

    // 白い芯→属性色の星→広がるリング→散る火花の順に重ねて、当たった一点をはっきり見せる
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Particle("enemy_hit_core", hitPos, { .scale = strength });
    fx.Star("enemy_hit", hitPos, { .color = color });
    if (escalation >= tune.hitEscalationMaxThreshold) {
        fx.Star("enemy_hit_core", hitPos);
    }
    fx.Ring("enemy_hit", hitPos, { .scale = strength, .color = color });
    if (escalation >= tune.hitEscalationOuterRingThreshold) {
        fx.Ring("enemy_hit_outer", hitPos, { .scale = strength, .color = color });
    }
    fx.Sparks("enemy_hit", hitPos, { .scale = strength, .color = color, .extraCount = extraSparks });

    // 武器ごとのweapons.json effect設定（burstCount/ringRadius）を追加の弾け・リングとして重ね、
    // 武器が変わると当たった瞬間の見た目量も変わるようにする
    fx.Sparks("enemy_hit_extra", hitPos, { .scale = strength, .color = color, .extraCount = extraBurstCount });
    if (ringRadius > 0.0f) {
        fx.Ring("enemy_hit_extra", hitPos,
            { .scale = strength, .speed = ringRadius, .color = color, .extraCount = extraBurstCount / visuals.extraRingCountDivisor });
    }
}

/** @brief 連続ヒット数に応じた命中演出の盛り度合い（0.0=初撃〜1.0=上限） */
float GamePlayScene::HitEscalationRatio() const {
    const int hits = (std::max)(hitEscalationCount_ - 1, 0);
    return (std::min)(static_cast<float>(hits) / static_cast<float>((std::max)(CombatTune().hitEscalationMaxHits, 1)), 1.0f);
}

/** @brief 属性名ごとの追加命中演出を出す（近接・固有技・銃で共通） */
void GamePlayScene::EmitElementalHitEffect(const WeaponData& weapon, const Vector3& enemyPos, int comboStep) {
    EmitElementalHitEffect(weapon.element, ColorFromArray(weapon.effectColor), enemyPos,
        1.0f + CombatTune().elementScalePerComboStep * static_cast<float>((std::max)(comboStep - 1, 0)));
}

/** @brief 属性名ごとの追加命中演出を出す（近接・固有技・銃で共通） */
void GamePlayScene::EmitElementalHitEffect(const std::string& element, const Vector4& color, const Vector3& enemyPos, float scale) {
    const float stepScale = scale * (1.0f + CombatTune().elementEscalationScaleGain * HitEscalationRatio());
    if (const IElementEffect* effect = IElementEffect::Find(element)) {
        effect->EmitHit(ElementContext(), enemyPos, color, stepScale);
    }
}

/** @brief 敵が予備動作に入った瞬間に警告リングを出す（攻撃が来ることを事前に伝え、回避を狙えるようにする） */
void GamePlayScene::EmitEnemyTelegraphCue(const EnemyEntity* enemy) {
    if (enemy == nullptr || !enemy->JustStartedTelegraph()) {
        return;
    }
    // 予備動作に入った瞬間、足元に輪と頭上に明滅する印を出して攻撃が来ることを知らせる
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Ring("enemy_telegraph", enemy->GetPosition());
    fx.Particle("enemy_telegraph_mark", enemy->GetPosition());
}

/** @brief UpdateStyleTechniqueParticles()の下請け 格闘コンボヒット時の斬撃・属性パーティクルを発生させる */
void GamePlayScene::EmitComboHitParticles(const Vector3& ppos) {
    const auto& visuals = HitEffectLibrary::GetInstance()->SceneParticles();
    if (!player_->JustComboHit()) {
        return;
    }
    auto* wm = WeaponManager::GetInstance();
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);

    // 段が進むほど大きな斬線をスタイル色で出し、締めの段では輪も出す
    const int step = player_->GetComboStep();
    const float dir = player_->GetLastDirX();
    const auto& styleColor = wm->GetList()[wm->GetIndex()].styleColor;
    const Vector3 styleRgb = { styleColor[0], styleColor[1], styleColor[2] };
    // 斬線の形は武器ごとのレシピ（combo_step_<hitStyle>）があればそれを使う
    const std::string hitStyle = wm->HasEquippedWeapon() ? wm->GetCurrent().hitStyle : std::string();
    const std::string styledSlash = "combo_step_" + hitStyle;
    const bool hasStyledSlash = !hitStyle.empty() && HitEffectLibrary::GetInstance()->Has("slashes", styledSlash);
    fx.Slash(hasStyledSlash ? styledSlash : "combo_step", ppos, dir,
        { .scale = 1.0f + static_cast<float>(step - 1) * visuals.comboSlashGrowthPerStep, .tint = styleRgb });
    if (step == visuals.comboFinisherStep) {
        fx.Ring("combo_step_finisher", ppos, { .tint = styleRgb });
    }

    // 属性演出はConfig/weapons.jsonの共通プリセットから生成する
    // 武器追加時にシーン側へtype分岐を増やさず色と密度を調整できるようにする
    const WeaponData& weapon = wm->GetCurrent();
    const Vector4 effectColor = ColorFromArray(weapon.effectColor);
    for (int i = 0; i < weapon.effectBurstCount; ++i) {
        const float spread = static_cast<float>((i % visuals.burstColumns) - visuals.burstCenterColumn) * visuals.burstSpreadZ;
        const Vector3 velocity = { dir * (visuals.burstSpeedBase + i * visuals.burstSpeedStep), visuals.burstRiseBase + (i % visuals.burstRiseSteps) * visuals.burstRiseStep, spread };
        fx.Particle("weapon_burst", ppos, { .color = effectColor, .velocity = velocity });
    }
    if (weapon.effectRingRadius > 0.0f) {
        fx.Ring("weapon_effect", ppos, { .speed = weapon.effectRingRadius, .color = effectColor, .extraCount = weapon.effectBurstCount });
    }

    // 持ち替え直後の一撃は輪と揺れで強調する
    if (player_->JustWeaponSwitchHit()) {
        fx.Ring("weapon_switch", ppos, { .tint = styleRgb });
        fx.Impact("weapon_switch");
    }
    // 段が進むほど揺れを大きくする
    fx.Impact("combo_step", { .scale = static_cast<float>(step) });
}

/** @brief 近接が当たった時の、武器ごとの演出と音 */
void GamePlayScene::EmitWeaponStyleHit(const Vector3& position, bool finisher) {
    const auto* wm = WeaponManager::GetInstance();
    const std::string hitStyle = wm->HasEquippedWeapon() ? wm->GetCurrent().hitStyle : std::string();
    gameSounds::PlayStyled(finisher ? "meleeFinisher" : "meleeHit", hitStyle);
    if (finisher) {
        EmitComboFinisherAccent(position);
    }
    if (hitStyle.empty()) {
        return;
    }
    // 書いてあるレシピだけを出す（武器ごとに、輪・火花・きらめき・揺れ・画面の閃光のどれを使うかが違う）
    const auto* library = HitEffectLibrary::GetInstance();
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    const std::string name = std::string(finisher ? "weapon_finisher_" : "weapon_hit_") + hitStyle;
    if (library->Has("rings", name)) {
        fx.Ring(name, position);
    }
    if (library->Has("sparks", name)) {
        fx.Sparks(name, position);
    }
    if (library->Has("stars", name)) {
        fx.Star(name, position);
    }
    if (library->Has("impacts", name)) {
        fx.Impact(name);
    }
    if (library->Has("flashes", name)) {
        fx.Flash(name);
    }
}

/** @brief コンボの締めが当たった瞬間に、足元から床を這って広がる衝撃と、武器の色の残り火を出す（画面を光らせずに重さを出す） */
void GamePlayScene::EmitComboFinisherAccent(const Vector3& position) {
    const auto& visuals = HitEffectLibrary::GetInstance()->SceneParticles();
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    // 地上で当てた時だけ、当てた敵の真下（プレイヤーの足元の高さ）から左右へ砂ぼこりの波を走らせる
    if (player_->IsOnGround()) {
        const Vector3 ground = { position.x, player_->GetPosition().y, position.z };
        fx.Sparks("finisher_ground_wave", ground);
        fx.Sparks("finisher_ground_puff", ground);
    }
    // 武器の色の小さな火の粉を、当てた所の周りからゆっくり立ちのぼらせる
    const auto* wm = WeaponManager::GetInstance();
    EffectStyle emberStyle;
    if (wm->HasEquippedWeapon()) {
        emberStyle.tint = ColorRgb(wm->GetCurrent().effectColor);
    }
    std::uniform_real_distribution<float> offset(-visuals.finisherEmberSpread, visuals.finisherEmberSpread);
    std::uniform_real_distribution<float> rise(visuals.finisherEmberRiseMin, visuals.finisherEmberRiseMax);
    std::uniform_real_distribution<float> drift(-visuals.finisherEmberDriftX, visuals.finisherEmberDriftX);
    for (int i = 0; i < visuals.finisherEmberCount; ++i) {
        emberStyle.velocity = Vector3 { drift(rng_), rise(rng_), 0.0f };
        fx.Particle("finisher_ember", { position.x + offset(rng_), position.y + offset(rng_), position.z }, emberStyle);
    }
}

/** @brief UpdateStyleTechniqueParticles()の下請け 銃発射時の弾煙パーティクルを発生させる */
void GamePlayScene::EmitGunFireParticles(const Vector3& ppos) {
    if (!player_->JustFired()) {
        return;
    }
    // 弾数・拡散・色は選択中の銃と段の定義に従う（散弾は扇状に、単発は直線に飛ぶ）
    auto* wm = WeaponManager::GetInstance();
    const GunShotDef* shot = player_->GetActiveGunShot();
    const RangedWeaponData& gun = wm->GetRanged();
    const float dir = player_->GetLastDirX();
    const float rangeX = gun.range * ((shot != nullptr) ? shot->rangeMult : 1.0f);
    // 弾は的（銃口に一番近い相手）で止まる。的が無ければ射程の端まで飛ぶ
    const float travel = gunShotPicker_.HasTarget() ? gunShotPicker_.GetHitDistance() : rangeX;
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    sceneShared::EmitGunShotParticles(fx, ppos, { ppos.x + dir * travel, ppos.y, ppos.z }, shot, ColorFromArray(gun.color));
}

/** @brief UpdateStyleTechniqueParticles()の下請け 武器固有技（ダガー以外）発動時に技ごとの見た目を出す */
void GamePlayScene::EmitWeaponSkillCastParticles(const Vector3& ppos) {
    const auto& visuals = HitEffectLibrary::GetInstance()->SceneParticles();
    // 固有技は発動モーションに合わせた見た目をここで技ごとに出す（命中時の共通バーストはApplyWeaponSkillStyleHit側）
    // 演出の色は今持っている武器の色に合わせる
    const auto* wm = WeaponManager::GetInstance();
    const WeaponData& weapon = wm->GetCurrent();
    const Vector4 color = ColorFromArray(weapon.effectColor);
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    // 衝撃は足元ではなく胸の高さから出す
    const Vector3 chest = { ppos.x, ppos.y + visuals.chestHeight, ppos.z };

    if (player_->IsSkillSequenceActive() && player_->JustComboHit()) {
        // 固有技の連撃: 段ごとに向きを変えた斬線を重ね、回転斬りの段は輪を広げる。締めは大きな一閃で打ち飛ばしを見せる
        const MeleeAttackDef* step = player_->GetActiveMeleeAttack();
        // 締めの段は大きな衝撃だけを出す
        if (step != nullptr && step->finisher) {
            EmitWeaponSkillImpact(chest, color, true);
        } else {
            // 段の番号ぶん斬線の角度をずらし、同じ向きが重ならないようにする
            const float angle = static_cast<float>(player_->GetComboStep()) * visuals.sequenceSlashAngleStep;
            fx.Slash("skill_sequence", ppos, 0.0f, { .angle = angle, .color = color });
            // 体を回す段だけ輪を足す
            if (step != nullptr && step->bodyTurns != 0.0f) {
                fx.Ring("skill_sequence_turn", ppos, { .color = color });
            }
        }
    }

    // このフレームに出た固有技の、表に書かれた演出（衝撃・輪・左右の斬線・軌跡）を出す
    for (const SkillCastEffect& cast : kSkillCastEffects) {
        // このフレームに発動していない技は飛ばす
        if (!((*player_).*cast.happened)()) {
            continue;
        }
        // 表で指定された演出だけを出す（空欄の項目は出さない）
        if (cast.impact) {
            EmitWeaponSkillImpact(chest, color, false);
        }
        if (cast.ring) {
            fx.Ring(cast.ring, ppos, { .color = color });
        }
        // 斬線は左右対称に2本出す
        if (cast.slash) {
            fx.Slash(cast.slash, ppos, 1.0f, { .color = color });
            fx.Slash(cast.slash, ppos, -1.0f, { .color = color });
        }
        if (cast.trail) {
            fx.Trail(cast.trail, ppos, color);
        }
    }
}

/** @brief 固有技が決まった瞬間の共通演出（閃光・停止・揺れ・刃の明滅・大きな弾け）を当たり外れに関係なく出す */
void GamePlayScene::EmitWeaponSkillImpact(const Vector3& center, const Vector4& color, bool slashLine) {
    const auto& visuals = HitEffectLibrary::GetInstance()->SceneParticles();
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Flash("skill_impact", { .tint = ColorRgb(color) });
    fx.Impact("skill_impact");
    bladeFlash_.Emit(center, visuals.bladeCount, visuals.bladeRadius, visuals.bladeSpeedMin, visuals.bladeSpeedMax);

    fx.Particle("skill_impact_core", center);
    fx.Star("skill_impact_core", center);
    fx.Star("skill_impact", center, { .color = color });
    fx.Ring("skill_impact_white", center);
    fx.Ring("skill_impact_color", center, { .color = color });
    fx.Sparks("skill_impact", center, { .color = color });

    if (slashLine) {
        // 突進の跡を丸ごと一閃で覆う
        const Vector3& cam = camera_->GetTranslate();
        sceneShared::SpawnSlashMarkWorld({ center.x - visuals.slashLineHalfLength, center.y },
            { center.x + visuals.slashLineHalfLength, center.y },
            cam.x, cam.y, visuals.slashLineColor, visuals.slashLineThickness, visuals.slashLineSeconds);
    }
}

/** @brief UpdateStyleTechniqueParticles()の下請け 覚醒中の継続オーラと発動瞬間の衝撃波を発生させる */
void GamePlayScene::EmitAwakenParticles(const Vector3& ppos, float dt) {
    const auto& visuals = HitEffectLibrary::GetInstance()->SceneParticles();
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);

    // 覚醒中オーラ（連続）
    if (player_->IsAwakened()) {
        auraTimer_ += dt;
        if (auraTimer_ >= visuals.auraInterval) {
            auraTimer_ = 0.0f;
            fx.Particle("awaken_aura", ppos);
        }
    } else {
        auraTimer_ = 0.0f;
    }

    // 覚醒発動の瞬間（衝撃波バースト）
    if (player_->JustAwakened()) {
        fx.Ring("awaken_outer", ppos);
        fx.Ring("awaken_inner", ppos);
        fx.Star("awaken", ppos);
        fx.Impact("awaken");
    }
}

/** @brief UpdateStyleTechniqueParticles()の下請け styleRankHud_のランクが上がった瞬間にリング・火花・カメラシェイク・画面フラッシュを出す */
void GamePlayScene::EmitStyleRankUpParticles(const Vector3& ppos) {
    // ランク閾値をここで別管理すると表示側(styleRankHud_)とズレるため、判定はHUD自身に問い合わせる
    if (!styleRankHud_.JustRankedUp()) {
        return;
    }
    const Vector4 rankColor = styleRankHud_.GetRankColor();
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Ring("rank_up", ppos, { .color = rankColor });
    fx.Star("rank_up", ppos, { .color = rankColor });
    fx.Impact("rank_up");
    fx.Flash("rank_up", { .tint = ColorRgb(rankColor) });
}

/** @brief 近接コンボのモーション中、装備武器の色で手元にトレイル残像を発生させ続ける */
void GamePlayScene::UpdateWeaponTrail() {
    // 近接コンボのモーション中、装備武器の手元に色付きの残像を撒いて振りの軌跡を見せる
    const auto* wm = WeaponManager::GetInstance();
    if (!wm->HasEquippedWeapon()) {
        return;
    }
    const WeaponData& weapon = wm->GetCurrent();
    const Vector4 color = ColorFromArray(weapon.effectColor);
    const Vector3 weaponPos = player_->GetActiveWeaponWorldPosition();
    // 合体している間は、構えている時も振っている時も相棒の属性を武器にまとわせる
    if (const WeaponData* partner = wm->GetCurrentFusionPartner()) {
        const Vector4 partnerColor = ColorFromArray(partner->effectColor);
        if (const IElementEffect* partnerEffect = IElementEffect::Find(partner->element)) {
            partnerEffect->EmitIdle(ElementContext(), weaponPos, partnerColor);
        }
        if (player_->IsMeleeAttacking()) {
            EmitWeaponElementAura(partner->element, partnerColor, weaponPos);
        }
    }
    if (!player_->IsMeleeAttacking()) {
        // 攻撃していない間は、覚醒中かスタイルランクが高い時だけ刃に属性をまとわせる（炎の剣なら刃が燃え続ける）
        if (!ShouldEmitIdleAura()) {
            return;
        }
        if (const IElementEffect* effect = IElementEffect::Find(weapon.element)) {
            effect->EmitIdle(ElementContext(), weaponPos, color);
        }
        return;
    }
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Trail("weapon_swing", weaponPos, color);
    EmitWeaponElementAura(weapon.element, color, weaponPos);
}

/** @brief 刃の軌跡・締め演出・F6調整パネルを更新する */
void GamePlayScene::UpdateComboFeel(bool gameAdvanced) {
    const auto* wm = WeaponManager::GetInstance();
    const Vector4 weaponColor = wm->HasEquippedWeapon()
        ? ColorFromArray(wm->GetCurrent().effectColor)
        : HitEffectLibrary::GetInstance()->Color("unarmed_trail");
    comboFeel_.SetAwakened(player_->IsAwakened());
    comboFeel_.Update(*player_, weaponColor, camera_.get(), gameAdvanced);
    comboFeel_.UpdateDebugPanel(input_);
}

/** @brief 近接モーション中、武器の先から属性ごとの粒子を零す（火の粉・水滴・冷気など） */
void GamePlayScene::EmitWeaponElementAura(const std::string& element, const Vector4& color, const Vector3& weaponPos) {
    const auto& visuals = HitEffectLibrary::GetInstance()->SceneParticles();
    // 毎フレーム1〜2粒だけ零し、振りの軌跡に属性の手触りを添える（命中演出の邪魔をしない量に抑える）
    std::uniform_real_distribution<float> jitterD(-visuals.auraJitter, visuals.auraJitter);
    const IElementEffect* effect = IElementEffect::Find(element);
    if (effect == nullptr) {
        return;
    }
    const Vector3 pos = { weaponPos.x + jitterD(rng_), weaponPos.y + jitterD(rng_), weaponPos.z };
    effect->EmitAura(ElementContext(), pos, color);
}

/** @brief 属性エフェクトに渡す粒子の発生先・乱数・プレイヤーの向き・刃の位置をまとめる */
ElementEffectContext GamePlayScene::ElementContext() {
    ElementEffectContext ctx { *pm_, rng_, player_->GetLastDirX() };
    ctx.hasBlade = comboFeel_.GetBladeWorld(*player_, ctx.bladeBase, ctx.bladeTip);
    return ctx;
}

/** @brief UpdateParticles()の下請け ボスの予告円→着弾ダメージのAoEスラム攻撃を処理する */
void GamePlayScene::UpdateBossSlamAttack(float dt) {
    const auto& visuals = HitEffectLibrary::GetInstance()->SceneParticles();
    // ボスがいない・倒れた・見えない時は、予告の途中でも取り消す（予告の円が残らないように）
    if (!enemy_ || enemy_->IsDefeated() || !enemy_->IsVisible()) {
        bossSlamWarningActive_ = false;
        bossSlamRequested_ = false;
        return;
    }

    if (!bossSlamWarningActive_) {
        // 叩きつけを出す時機はボス戦の進行役が決める（名乗る前や他の技の最中には頼まれない）
        if (bossSlamRequested_) {
            bossSlamRequested_ = false;
            bossSlamWarningActive_ = true;
            bossSlamWarningTimer_ = EnemyTuning::GetInstance()->BossSlam().warningDuration;
            bossSlamTargetPos_ = player_->GetPosition(); // 着弾地点は予告開始時点のプレイヤー位置に固定する（避けられるように）
        }
        return;
    }

    // 予告円の警告演出（円形グロー画像を着弾範囲の見かけ半径まで拡大し、点滅させながら見せる）
    bossSlamWarningTimer_ -= dt;
    const float progress = std::clamp(1.0f - bossSlamWarningTimer_ / EnemyTuning::GetInstance()->BossSlam().warningDuration, 0.0f, 1.0f);

    const Vector3& cam = camera_->GetTranslate();
    float sx, sy;
    sceneShared::WorldToScreen(bossSlamTargetPos_.x, bossSlamTargetPos_.y, cam.x, cam.y, sx, sy);
    const float pxPerWorldUnit = gameConstants::kScreenCenterX / gameConstants::kCameraHalfW;
    const float diameterPx = BossSlamRadius() * 2.0f * pxPerWorldUnit
        * (visuals.warningMinScale + (1.0f - visuals.warningMinScale) * progress);
    const float blink = 0.5f + 0.5f * std::sin(bossSlamWarningTimer_ * visuals.warningBlinkSpeed);
    bossSlamWarningSprite_->SetColor({ 1.0f, visuals.warningGreenBase + progress * visuals.warningGreenGrowth, visuals.warningBlue,
        visuals.warningAlphaBase + blink * visuals.warningAlphaBlink });
    bossSlamWarningSprite_->SetPosition({ sx - diameterPx * 0.5f, sy - diameterPx * 0.5f });
    bossSlamWarningSprite_->SetSize({ diameterPx, diameterPx });
    bossSlamWarningSprite_->Update();

    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Ring("slam_warning", bossSlamTargetPos_,
        { .speed = BossSlamRadius(),
            .tint = Vector3 { 1.0f, visuals.warningRingGreenBase - progress * visuals.warningRingGreenFade, visuals.warningRingBlueBase - progress * visuals.warningRingBlueFade } });

    if (bossSlamWarningTimer_ > 0.0f) {
        return;
    }

    // 着弾判定
    bossSlamWarningActive_ = false;

    const Vector3& ppos = player_->GetPosition();
    const float dx = ppos.x - bossSlamTargetPos_.x;
    const float dy = ppos.y - bossSlamTargetPos_.y;
    const float slamRadius = BossSlamRadius();
    if (dx * dx + dy * dy <= slamRadius * slamRadius && !TryJustDodge(bossSlamTargetPos_) && !player_->IsInvincible()) {
        RunData::GetInstance()->TakeDamage(EnemyTuning::GetInstance()->BossSlam().damage);
        player_->OnHit();
        fx.Impact("boss_slam");
    }

    // 内側と外側（半径の倍率はレシピの speed）の2重のリングと火花
    fx.Ring("boss_slam_inner", bossSlamTargetPos_, { .speed = slamRadius });
    fx.Ring("boss_slam_outer", bossSlamTargetPos_, { .speed = slamRadius });
    fx.Sparks("boss_slam", bossSlamTargetPos_);
}

/** @brief 爆風の有無で演出表を選び、破壊時の粒子と手応えを出す */
void GamePlayScene::EmitBreakableDestructionEffect(const Vector3& position, const Vector3& footPosition, float radius) {
    /** @brief レシピの出力方法 */
    enum class EffectKind { kImpact, kFlash, kParticle, kRing, kSparks, kDust };
    /** @brief レシピ名と出力方法の組 */
    struct DestructionEffect {
        const char* recipe;
        EffectKind kind;
    };
    // 爆発は中心光、炎、煙、破片を重ね、崩落は破片と足元の煙を出す
    constexpr DestructionEffect kExplosionEffects[] = {
        { "explosion", EffectKind::kImpact },
        { "explosion", EffectKind::kFlash },
        { "explosion_core", EffectKind::kParticle },
        { "explosion_fire", EffectKind::kRing },
        { "explosion_smoke", EffectKind::kRing },
        { "explosion_fire", EffectKind::kSparks },
        { "explosion_debris", EffectKind::kSparks },
    };
    constexpr DestructionEffect kRubbleEffects[] = {
        { "rubble", EffectKind::kImpact },
        { "rubble_debris", EffectKind::kSparks },
        { "rubble_dust", EffectKind::kDust },
    };
    const bool explosive = radius > 0.0f;
    const std::span<const DestructionEffect> effects = explosive
        ? std::span<const DestructionEffect>(kExplosionEffects)
        : std::span<const DestructionEffect>(kRubbleEffects);
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    // 輪の速度倍率はレシピ側で調整する
    for (const DestructionEffect& effect : effects) {
        switch (effect.kind) {
        case EffectKind::kImpact: fx.Impact(effect.recipe); break;
        case EffectKind::kFlash: fx.Flash(effect.recipe); break;
        case EffectKind::kParticle: fx.Particle(effect.recipe, position); break;
        case EffectKind::kRing: fx.Ring(effect.recipe, position, { .speed = radius }); break;
        case EffectKind::kSparks: fx.Sparks(effect.recipe, position); break;
        case EffectKind::kDust: fx.Sparks(effect.recipe, footPosition); break;
        }
    }
    gameSounds::Play(explosive ? SoundCue::kExplosion : SoundCue::kRubble);
}

} // namespace engine::game

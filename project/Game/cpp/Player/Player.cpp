/**
 * @file Player.cpp
 * @brief プレイヤー（Player）の初期化と1フレームの更新の実装
 */
#include "Player.h"
#include "EngineContext.h"
#include "GameActions.h"
#include "TestCoverage.h"
#include "GroundSupport.h"
#include "CharacterVisuals.h"
#include "Easing.h"
#include "GameConstants.h"
#include "GameRules.h"
#include "GravityBody.h"
#include "Input.h"
#include "ModelCommon.h"
#include "ModelManager.h"
#include "OutlineEffect.h"
#include "PipelineStateGuard.h"
#include "PlayerTuning.h"
#include "Weapon.h"
#include "WeaponManager.h"
#include "ThrownWeaponPool.h"
#include "WeaponFinisherTuning.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
using namespace engine::graphics;

namespace engine::game {
namespace {
/** @brief 読み込み済みの調整値を取得する */
const PlayerTuning& Tuning() {
    return PlayerTuning::Get();
}

}

// ══════════════════════════════════════════════════════
// 初期化
// ══════════════════════════════════════════════════════

/** @brief 描画基盤・アニメーション・手持ち武器とプレイヤー状態を初期化する */
void Player::Initialize(ModelCommon* modelCommon) {
    weaponThrow_ = {};
    // 利用できる攻撃の段を登録する。
    if (testCoverage::IsEnabled()) {
        // 操作の種類を登録する
        for (const char* action : { "dodge", "just_dodge", "warp", "awaken", "rampage", "finisher", "steal", "ally" }) {
            testCoverage::Declare("player_action", action);
        }
        const auto declareMelee = [](const MeleeComboSet& set) {
            for (int i = 0; i < set.ground.count; ++i) {
                testCoverage::Declare("melee_step", set.ground[i].id);
            }
            for (int i = 0; i < set.air.count; ++i) {
                testCoverage::Declare("melee_step", set.air[i].id);
            }
            if (set.launcher != nullptr) {
                testCoverage::Declare("melee_step", set.launcher->id);
            }
        };
        // 本編で手に入る武器（武器の表に読み込んだ種類）の段だけを、試せる物として数える
        for (const auto& weapon : WeaponManager::GetInstance()->GetList()) {
            declareMelee(GetMeleeComboSet(weapon.type));
            if (const auto* boss = GetBossMeleeComboSet(weapon.type)) {
                declareMelee(*boss);
            }
        }
        // 武器と固有技は、武器の表にある種類の全部を試せる物として数える
        for (const auto& weapon : WeaponManager::GetInstance()->GetList()) {
            testCoverage::Declare("weapon", WeaponTypeName(weapon.type));
            testCoverage::Declare("weapon_skill", WeaponTypeName(weapon.type));
        }
        // 銃の持ち替えを切っている時は最初の銃しか使わないので、その銃と段だけを試せる物として数える
        const auto declareGun = [](GunType gun) {
            testCoverage::Declare("gun", GunTypeName(gun));
            const auto& set = GetGunComboSet(gun);
            for (int step = 0; step < set.count; ++step) {
                testCoverage::Declare("gun_shot", set[step].id);
            }
        };
        if (GameRules::GetInstance()->Get().gunSwitchEnabled) {
            for (size_t i = 0; i < kGunTypeCount; ++i) {
                declareGun(static_cast<GunType>(i));
            }
        } else {
            declareGun(WeaponManager::GetInstance()->GetRanged().type);
        }
    }
    modelCommon_ = modelCommon;

    // スキンメッシュ共通設定（両フォームのリグで共有）
    skinCommon_ = SkinnedObject3d::CreateSharedCommon(modelCommon);

    // 見た目リグの共通セットアップ（静的モデル + スキンモデル + アニメーション一式）
    auto initRig = [&](CharacterRig& rig, const RigVisualDef& def) {
        // 残像・分身演出用の静的モデル（ボーンなし、ボーン付きモデルと同じ見た目）
        rig.staticModel = engine::Services().models->GetOrLoad(modelCommon, def.staticModelPath, def.texture);

        // 本体（ボーンアニメーション付き）
        std::string modelPath = std::string(def.dir) + "/" + def.file;
        rig.skinnedModel = engine::Services().models->GetOrLoadSkinned(modelCommon->GetDxCommon(), modelPath, def.texture);

        rig.modelScale = def.scale;
        rig.modelOffsetY = def.offsetY;
        rig.meleeBoneName = def.meleeBone;
        rig.gunBoneName = def.gunBone;
        rig.motionRigId = def.file;

        rig.idleAnim = LoadAnimationFile(def.dir, def.file, def.idle);
        rig.runAnim = LoadAnimationFile(def.dir, def.file, def.run);
        rig.jumpAnim = LoadAnimationFile(def.dir, def.file, def.jump);
        rig.runningJumpAnim = LoadAnimationFile(def.dir, def.file, def.runningJump);
        LockClipVerticalRootMotion(rig.jumpAnim);
        LockClipVerticalRootMotion(rig.runningJumpAnim);
        rig.swimAnim = LoadAnimationFile(def.dir, def.file, def.swim);
        rig.idleHoldAnim = LoadAnimationFile(def.dir, def.file, def.idleHold);
        // 待機時に体が上下しないよう、装備あり・なしの両方の待機の高さを固定する
        LockClipVerticalRootMotion(rig.idleAnim);
        LockClipVerticalRootMotion(rig.idleHoldAnim);
        rig.runHoldAnim = LoadAnimationFile(def.dir, def.file, def.runHold);
        rig.slashAnim = LoadAnimationFile(def.dir, def.file, def.slash);
        rig.punchAnim = LoadAnimationFile(def.dir, def.file, def.punch);
        rig.walkAnim = LoadAnimationFile(def.dir, def.file, def.walk);

        rig.object = std::make_unique<SkinnedObject3d>();
        rig.object->Initialize(skinCommon_.get());
        rig.object->SetModel(rig.skinnedModel);
        rig.object->SetSkeleton(Skeleton::Create(LoadNodeHierarchyFromFile(def.dir, def.file)));
        rig.object->SetPoseLayer(&motionLayer_);
        rig.object->SetAnimation(rig.idleAnim);
        // ライティング有効 + リムライトで背景からシルエットを分離させる
        rig.object->SetEnableLighting(true);
        rig.object->SetRimColor(kRimColor);
        rig.object->SetRimPower(kRimPower);
        rig.object->SetRimIntensity(kRimIntensity);
        rig.object->SetEnableRim(true);
        rig.object->SetScale({ def.scale, def.scale, def.scale });
        rig.object->SetPosition({ pos_.x, pos_.y + def.offsetY + groundVisualCorrection_, pos_.z });
        rig.object->Update();
    };
    initRig(normalRig_, kNormalRigVisual);
    initRig(awakenedRig_, kAwakenedRigVisual);
    rig_ = &normalRig_;
    animState_ = &IdleAnim();

    afterImageRenderer_.Initialize(modelCommon, rig_->staticModel, rig_->modelScale);

    // 右手の近接武器・合体の素材・左手の銃を用意する（表示する物の選択は毎フレームの付け直しが行う）
    weaponVisuals_.Initialize(modelCommon, kRimColor, kRimPower, kRimIntensity);
}

/** @brief 武器奪取の刺突モーションを頭から再生する（外部トリガー用、既存の斬撃を流用） */
void Player::PlayStealStab() {
    // 武器奪取の刺突（専用素材が無いため斬撃を流用）
    PlayAttackAnim(rig_->slashAnim, Tuning().stealStabAnimSpeed);
}

/** @brief 持っている武器が砕けた時に、振っている途中のコンボと技を打ち切る */
void Player::CancelAttacksForWeaponBreak() {
    meleeCombo_.Reset();
    ResetWeaponSkills();
}

/** @brief 装備を次の武器に切り替え、手放した武器を正面へ飛ばす */
void Player::UpdateWeaponThrow(Input* input, const std::vector<AABB>& blocks) {
    const auto& axe = WeaponFinisherTuning::Get(WeaponType::kAxe);
    if (weaponThrow_.remainingDistance <= 0.0f) {
        weaponThrow_.secondsLeft = 0.0f;
    }
    if (!HasWeaponInFlight()) {
        if (!input->TriggerAction(GameAction::kThrowWeapon) || explorationMode_ || weaponsDropped_ || IsRampaging()) {
            return;
        }
        auto* weapons = WeaponManager::GetInstance();
        if (weapons->HasPendingWeapon() || weapons->HasPendingBossChoice()) {
            return;
        }
        const auto& weapon = weapons->GetCurrent();
        if (axe.throwSpeed <= 0.0f || axe.throwDistance <= 0.0f || axe.throwMaxSeconds <= 0.0f
            || BehaviorFor(weapon.type).IsDisarmed() || !weapons->TryBreakCurrentWeapon()) {
            return;
        }
        CancelAttacksForWeaponBreak();
        const Vector3 start = { pos_.x, pos_.y + ThrownWeaponParams::kLaunchHeight, pos_.z };
        weaponThrow_ = { weapon.type, start, lastDirX_, axe.throwMaxSeconds, 0.0f, weapon.damage, axe.throwDistance };
    }
    const float dt = gameConstants::kFrameDeltaTime;
    const float movement = ThrownWeaponParams::OutwardStep(axe.throwSpeed, axe.throwDistance, weaponThrow_.remainingDistance, dt);
    weaponThrow_.remainingDistance -= movement;
    weaponThrow_.previousX = weaponThrow_.position.x;
    weaponThrow_.position.x += weaponThrow_.direction * movement;
    weaponThrow_.spin -= weaponThrow_.direction * axe.throwSpinSpeed * dt;
    weaponThrow_.secondsLeft -= dt;
    for (const AABB& block : blocks) {
        constexpr float kTerrainRadius = 0.2f;
        if (ConsumeWeaponThrowHit(block, kTerrainRadius) > 0.0f) {
            break;
        }
    }
}

/** @brief 地形または敵に触れたら飛行を終え、命中した時だけ威力を返す */
float Player::ConsumeWeaponThrowHit(const AABB& target) {
    return ConsumeWeaponThrowHit(target, WeaponFinisherTuning::Get(WeaponType::kAxe).throwHitRadius);
}

/** @brief このフレームの飛行区間全体で当たりを取る */
float Player::ConsumeWeaponThrowHit(const AABB& target, float radius) {
    const Vector3& p = weaponThrow_.position;
    if (!HasWeaponInFlight() || (std::max)(weaponThrow_.previousX, p.x) < target.min.x - radius
        || (std::min)(weaponThrow_.previousX, p.x) > target.max.x + radius
        || p.y < target.min.y - radius || p.y > target.max.y + radius
        || p.z < target.min.z - radius || p.z > target.max.z + radius) {
        return 0.0f;
    }
    weaponThrow_.secondsLeft = 0.0f;
    return weaponThrow_.damage;
}

/** @brief 床を抜けて落ちた時に、指定した所へ置き直す（落ちる速さ・前のフレームの位置・技の途中の動きも消す） */
void Player::PlaceAfterFallRescue(const Vector3& position) {
    pos_ = position;
    // 前のフレームの位置も合わせないと、床の上面を上から越えた判定や壁の押し戻しが、落ちていた所との差で働いてしまう
    prevFrameX_ = position.x;
    prevFrameY_ = position.y;
    velocityY_ = 0.0f;
    onGround_ = false;
    InterruptWeaponSkills();
    meleeCombo_.Reset();
}

/** @brief 被弾時の無敵時間を開始する */
void Player::OnHit() {
    invincibleTimer_ = Tuning().invincibleDuration;
}

/** @brief スキル補正込みのコンボ最大数を返す（現在の武器の地上コンボ段数基準） */
int Player::GetComboMax() const {
    if (!WeaponManager::GetInstance()->HasEquippedWeapon()) {
        return 0;
    }
    // 現在の武器の地上コンボ段数を基準にする（HUDのx段目/最大表示用）
    const MeleeComboSet& set = GetMeleeComboSet(WeaponManager::GetInstance()->GetCurrent().type, WeaponManager::GetInstance()->IsBossSlotSelected());
    return set.ground.count + skillMods_.comboMaxBonus;
}

/** @brief 現在の武器と覚醒状態から攻撃ダメージ倍率を返す（覚醒中はweapons.jsonのawakened.damageMult） */
float Player::GetAwakenedDamageMult() const {
    const auto* wm = WeaponManager::GetInstance();
    return (awaken_.isAwakened && wm->HasEquippedWeapon()) ? wm->GetCurrent().awakened.damageMult : 1.0f;
}

/** @brief 現在の武器と覚醒状態から固有技の判定範囲倍率を返す（非覚醒時は1.0） */
float Player::GetAwakenedSkillRadiusMult() const {
    const auto* wm = WeaponManager::GetInstance();
    return (awaken_.isAwakened && wm->HasEquippedWeapon()) ? wm->GetCurrent().awakened.skillRadiusMult : 1.0f;
}

/** @brief 現在の武器と覚醒状態からノックバック倍率を返す（非覚醒時は1.0） */
float Player::GetAwakenedKnockbackMult() const {
    const auto* wm = WeaponManager::GetInstance();
    return (awaken_.isAwakened && wm->HasEquippedWeapon()) ? wm->GetCurrent().awakened.knockbackMult : 1.0f;
}

/** @brief 回避ボタンの押下を先行入力として記憶する */
void Player::BufferDodgeInput(Input* input) {
    if (input->TriggerAction(GameAction::kDodge)) {
        dodge_.bufferFrames = Tuning().dodgeBufferFrames;
    }
}

/** @brief 回避入力の受付と回避移動の進行を処理する */
void Player::HandleDodge(Input* input) {
    dodge_.cooldown = (std::max)(dodge_.cooldown - gameConstants::kFrameDeltaTime, 0.0f);
    dodge_.spamTimer += gameConstants::kFrameDeltaTime;

    // 先行入力の残りフレームを消費しつつ、このフレームの押下も同じバッファに載せる
    // （シーンがBufferDodgeInput()を呼んでいなくても直押しで回避できるように）
    BufferDodgeInput(input);
    const bool dodgeRequested = dodge_.bufferFrames > 0;
    dodge_.bufferFrames = (std::max)(dodge_.bufferFrames - 1, 0);

    // 進行中の回避を進める（イージングで滑らかに移動し、終わったら無敵を解く）
    if (dodge_.active) {
        if (AdvanceHorizontalMove(dodge_.timer, Tuning().dodgeDuration, dodge_.startX, dodge_.targetX)) {
            dodge_.active = false;
            dodge_.cooldown = Tuning().dodgeCooldown;
        }
        return;
    }

    // ロックオン中に敵の方へ倒して回避を押したら、回避の代わりにワープする（パッドのボタン数を減らすため）
    if (dodgeRequested && IsWarpChordHeld(input) && CanStartWarp()) {
        dodge_.bufferFrames = 0;
        warp_.requestedByDodge = true;
        return;
    }

    const bool blocked = inWater_ || finisher_.charging || IsRampaging()
        || dodge_.cooldown > 0.0f || warp_.active;
    if (blocked || !dodgeRequested) {
        return;
    }
    dodge_.bufferFrames = 0; // 消費した先行入力で二重に回避しない

    // 方向は移動入力優先で、その方向を向いて前転する
    // 無ければ向いている方向の逆へバックステップ（向き自体は変えない＝敵を見たまま退く）
    const float heldDirX = GetHeldMoveDirection(input);
    dodge_.backward = heldDirX == 0.0f;
    const float dirX = dodge_.backward ? -lastDirX_ : heldDirX;
    if (!dodge_.backward) {
        lastDirX_ = dirX;
    }

    // 短い間隔で繰り返した回避は連打として数える（危険の無い場面での乱用をスタイル評価で咎めるため）
    dodge_.spamCount = dodge_.spamTimer < Tuning().dodgeSpamWindow ? dodge_.spamCount + 1 : 0;
    dodge_.spamTimer = 0.0f;

    dodge_.active = true;
    // 自動テストの網羅のまとめに数える
    testCoverage::Cover("player_action", "dodge");
    SetFrameEvent(FrameEvent::kDodged);
    dodge_.rewardClaimed = false;
    dodge_.timer = 0.0f;
    dodge_.startX = pos_.x;
    dodge_.targetX = std::clamp(pos_.x + dirX * Tuning().dodgeDistance, minX_, maxX_);

    // 回避はコンボの逃げ道。振りかけの段は打ち切り、踏み込み系の技も止める
    meleeCombo_.Reset();
    InterruptWeaponSkills();
    // バックステップは立ちジャンプ、前転は走りジャンプのモーションを速めて見せる
    const PlayerTuning& t = Tuning();
    PlayAttackAnim(dodge_.backward ? rig_->jumpAnim : rig_->runningJumpAnim,
        dodge_.backward ? t.backDodgeAnimSpeed : t.dodgeAnimSpeed);
}

/**
 * @brief 減速補間で進む量を求め、今の位置に足してから共通のステージ境界に収め、終了を判定する
 * @note 位置を開始点からの補間で上書きすると、壁の押し戻しが毎フレーム消えて壁の向こうへ抜けるため、差分だけを足す
 */
bool Player::AdvanceHorizontalMove(float& timer, float duration, float startX, float targetX) {
    const float prevProgress = std::clamp(timer / duration, 0.0f, 1.0f);
    timer += gameConstants::kFrameDeltaTime;
    const float progress = std::clamp(timer / duration, 0.0f, 1.0f);
    const float step = (targetX - startX) * (easing::EaseOutQuad(progress) - easing::EaseOutQuad(prevProgress));
    pos_.x = std::clamp(pos_.x + step, minX_, maxX_);
    return progress >= 1.0f;
}

/** @brief テレポート斬り入力の受付とワープ移動の進行を処理する */
void Player::HandleWarp(Input* input) {
    warp_.cooldown = (std::max)(warp_.cooldown - gameConstants::kFrameDeltaTime, 0.0f);

    // 進行中のワープを進める（イージングで滑らかに移動し、終わったら無敵を解く）
    if (warp_.active) {
        if (AdvanceHorizontalMove(warp_.timer, Tuning().warpDuration, warp_.startX, warp_.targetX)) {
            warp_.active = false;
            warp_.cooldown = awaken_.isAwakened ? Tuning().warpAwakenedCooldown : Tuning().warpCooldown;
        }
        return;
    }

    // 専用ボタンか、回避ボタンとの組み合わせ（HandleDodge で受け付け済み）で始める
    const bool requested = input->TriggerAction(GameAction::kWarp) || warp_.requestedByDodge;
    warp_.requestedByDodge = false;
    if (!requested || !CanStartWarp()) {
        return;
    }

    if (!awaken_.isAwakened) {
        awaken_.gauge -= Tuning().warpGaugeCost;
    }

    // 自動テストの網羅のまとめに数える
    testCoverage::Cover("player_action", "warp");
    warp_.active = true;
    warp_.timer = 0.0f;
    warp_.startX = pos_.x;
    const float distance = awaken_.isAwakened ? Tuning().warpAwakenedDistance : Tuning().warpDistance;
    warp_.targetX = std::clamp(pos_.x + lastDirX_ * distance, minX_, maxX_);

    // ワープはコンボの逃げ道兼割り込み技。振りかけの段は打ち切り、踏み込み系の技も止める
    meleeCombo_.Reset();
    InterruptWeaponSkills();
    PlayAttackAnim(rig_->slashAnim, Tuning().warpAnimSpeed);
}

/** @brief 今ワープを始められるか（妨げる状態でなく、ゲージかクールタイムの条件を満たしているか） */
bool Player::CanStartWarp() const {
    const bool blocked = inWater_ || finisher_.charging || IsRampaging()
        || warp_.cooldown > 0.0f || dodge_.active || warp_.active;
    const bool hasGauge = awaken_.isAwakened || awaken_.gauge >= Tuning().warpGaugeCost;
    return !blocked && hasGauge;
}

/** @brief ロックオンを押しながら、向いている方（ロック中は敵の方）へ移動入力しているか */
bool Player::IsWarpChordHeld(Input* input) const {
    if (!input->PushAction(GameAction::kLockOn)) {
        return false;
    }
    // 敵から離れる方向やニュートラルなら、普通の回避で逃げられるように残す
    const float heldDirX = GetHeldMoveDirection(input);
    return heldDirX * lastDirX_ > 0.0f;
}

/** @brief 攻撃モーションを頭から再生し、再生し切るまで状態遷移をロックする */
void Player::PlayAttackAnim(const engine::graphics::Animation& anim, float speed) {
    rig_->object->SetAnimation(anim);
    rig_->object->SetAnimSpeed(speed);
    animState_ = &AttackAnim();
    attackAnimTimer_ = anim.duration / speed;
}

/** @brief 近接の段を始める（土台は構えの待機アニメにし、振りの動きは段ごとの加算モーションだけで付ける） */
void Player::PlayMeleeStepAnim(const MeleeAttackDef& attack) {
    // 加算モーションは combo_motion.json を作る時に、この待機アニメを同じ速さで頭から流した姿勢を基準にしている
    rig_->object->SetAnimation(rig_->idleHoldAnim);
    rig_->object->SetAnimSpeed(attack.animSpeed);
    animState_ = &AttackAnim();
    attackAnimTimer_ = attack.duration;
}

// ══════════════════════════════════════════════════════
// フレーム更新
// ══════════════════════════════════════════════════════

/** @brief 入力・移動・戦闘・アニメーションなどプレイヤーの状態を1フレーム分更新する */
void Player::Update(Input* input, const Vector3& enemyPos, bool enemySolid, bool useStageFloor) {
    useStageFloor_ = useStageFloor;
    ResetFrameFlags();
    // 外から引っ張られた分（吸い込みなど）を、前のフレームの位置を覚えた後にずらす（壁の当たりは入ってきた側へ戻せる）
    pos_.x = std::clamp(pos_.x + externalShiftX_, minX_, maxX_);
    externalShiftX_ = 0.0f;

    if (invincibleTimer_ > 0.0f) {
        invincibleTimer_ -= gameConstants::kFrameDeltaTime;
    }

    dodge_.justDodgeWindowTimer = (std::max)(dodge_.justDodgeWindowTimer - gameConstants::kFrameDeltaTime, 0.0f);

    // 歩くだけの状態では、移動とジャンプと見た目だけを進める（戦いの入力は受け付けない）
    if (explorationMode_) {
        GetPhysicsState(inWater_).Update(*this, input);
        pos_.x = std::clamp(pos_.x, minX_, maxX_);
        UpdateWaterState();
        UpdateVisualState(input);
        AttachActiveWeapons();
        return;
    }

    HandleStyleSwitch(input);
    HandleDodge(input);
    HandleWarp(input);

    GetPhysicsState(inWater_).Update(*this, input);
    pos_.x = std::clamp(pos_.x, minX_, maxX_);
    if (onGround_) {
        NotifyWeaponSkillsLanded(); // 空中ダッシュなどは着地で回復する
    }

    // 乱舞はシーンが選んだ近くの相手へ向かう（選ばれていなければ渡された敵の位置）
    const Vector3 rampageTarget = rampageData_.hasTarget ? rampageData_.target : enemyPos;
    rampageData_.hasTarget = false;

    HandleRangedCombat(input);
    HandleMeleeCombat(input, rampageTarget);
    HandleFinisherSlash(input);
    HandleWeaponSkill(input);
    UpdateRampagePhysics(rampageTarget);
    UpdateAwakenState(input);
    if (enemySolid) {
        ResolveEnemyOverlap(enemyPos);
    }
    UpdateWaterState();
    UpdateVisualState(input);
    AttachActiveWeapons();
}

/** @brief 指定した位置の真下にある床へ立った時の位置を返す（足元より下で一番高いブロックの上面に足を合わせる） */
Vector3 Player::FindStandingPosition(const Vector3& position, const std::vector<AABB>& blocks) const {
    const auto floor = FindSupportingFloor(position, kBodyHalfExtent, kBodyHalfExtent, kBodyHalfExtent, blocks);
    return floor ? Vector3 { position.x, *floor + kBodyHalfExtent, position.z } : position;
}

/** @brief ステージ上のsolidブロックとの当たり判定を解決する（Update()の後に毎フレーム呼ぶ） */
void Player::ResolveBlockCollision(const std::vector<AABB>& blocks) {
    groundVisualCorrection_ = 0.0f;
    if (blocks.empty()) {
        if (pos_.y > Tuning().groundY && velocityY_ <= 0.0f) {
            onGround_ = false;
        }
        return;
    }

    // プレイヤーの当たり判定はダミー等と同じpos_を中心とした1x1x1規約に合わせる
    constexpr float kHalf = kBodyHalfExtent;

    // 上昇中に頭がブロック下面を横切った場合は、下面の直下へ戻して上昇速度を止める。
    if (velocityY_ > 0.0f) {
        const float previousHeadY = prevFrameY_ + kHalf;
        const float currentHeadY = pos_.y + kHalf;
        float nearestCeiling = (std::numeric_limits<float>::max)();
        for (const auto& b : blocks) {
            const bool overlapXZ = (pos_.x + kHalf) > b.min.x && (pos_.x - kHalf) < b.max.x
                && (pos_.z + kHalf) > b.min.z && (pos_.z - kHalf) < b.max.z;
            if (!overlapXZ) {
                continue;
            }
            if (previousHeadY <= b.min.y && currentHeadY >= b.min.y) {
                nearestCeiling = (std::min)(nearestCeiling, b.min.y);
            }
        }
        if (nearestCeiling != (std::numeric_limits<float>::max)()) {
            pos_.y = nearestCeiling - kHalf;
            velocityY_ = 0.0f;
            onGround_ = false;
        }
    }

    // 1フレームの落下量は側面判定の余白より大きくなり得るため、今フレームで上面を
    // 上から跨いだブロックは着地扱いにする（側面押し出しで横へ弾くと着地できない）
    // 移動前の高さは、フレームの頭で覚えた値を使う（今の速さから逆算すると、空中攻撃で落下を弱めた分だけ
    // 実際より低く出て、速く落ちてきたフレームに上面を越えたと分からず床の中へ沈む）
    const float feetYBeforeResolve = pos_.y - kHalf;
    const float prevFeetY = prevFrameY_ - kHalf;
    // 上昇する床は1フレームで足元へ少し食い込むことがある。
    // その食い込みを側面衝突として横へ押し出さず、上面への着地として扱う。
    // 60fps時の移動床の最大移動量を少しだけ上回る値に留める。
    // 大きすぎる許容値は、床から離れた後も上面へ吸着して浮いて見える原因になる。
    constexpr float kMovingFloorTopTolerance = 0.04f;
    auto landedOnTop = [&](const AABB& b) {
        const bool crossedTopWhileFalling = prevFeetY >= b.max.y - kTopCrossTolerance;
        const bool shallowMovingFloorPenetration = onGround_
            && feetYBeforeResolve >= b.max.y - kMovingFloorTopTolerance
            && feetYBeforeResolve <= b.max.y + kMovingFloorTopTolerance;
        return velocityY_ <= 0.0f && (crossedTopWhileFalling || shallowMovingFloorPenetration);
    };
    // 壁のように同じX/Zに積み重なったブロックの途中の継ぎ目に、ジャンプで壁へ突っ込んだ際に
    // 乗ってしまわないよう、頭上（立った時に体が収まる高さ）に別のブロックが無いことも確認する
    auto hasClearanceAbove = [&](const AABB& candidate) {
        // 足元とこの床が実際に重なる範囲だけを見る。隣の壁に肩が入っていても床は足を支えられる。
        const float left = (std::max)(pos_.x - kHalf, candidate.min.x);
        const float right = (std::min)(pos_.x + kHalf, candidate.max.x);
        const float nearZ = (std::max)(pos_.z - kHalf, candidate.min.z);
        const float farZ = (std::min)(pos_.z + kHalf, candidate.max.z);
        for (const auto& other : blocks) {
            if (&other == &candidate) {
                continue;
            }
            const bool otherOverlapXZ = left < other.max.x && right > other.min.x
                && nearZ < other.max.z && farZ > other.min.z;
            if (!otherOverlapXZ) {
                continue;
            }
            // すぐ上に積まれたブロック（壁の継ぎ目）だけを見る。床から少し浮いた低いブロックの下でも床に立てないと、
            // 床の中へ沈み、体が床に隠れて透明に見えたり埋まったりする
            if (other.min.y <= candidate.max.y + kStackedBlockGap && other.max.y > candidate.max.y) {
                return false;
            }
        }
        return true;
    };
    // 支持判定は体の幅よりかなり狭くし、見た目上ブロックの外（空中）なのに乗れてしまうのを防ぐ
    constexpr float kSupportHalf = 0.15f;
    // 床の中に沈んでいたら上面へ戻す（横の押し出しより前に行う。押し出すと床の中を横へずれて戻れなくなる）
    // 戻すのは、移動前も今もそのブロックの真上か中にいて、足が上面から体の高さ以内に沈んでいる時だけ
    // （横から壁へ入った時は戻さない。戻すと壁を登れてしまう）
    if (velocityY_ <= 0.0f) {
        const float sunkFeetY = pos_.y - kHalf;
        float recoverTop = (std::numeric_limits<float>::lowest)();
        for (const auto& b : blocks) {
            const bool overlapNow = (pos_.x + kSupportHalf) > b.min.x && (pos_.x - kSupportHalf) < b.max.x
                && (pos_.z + kSupportHalf) > b.min.z && (pos_.z - kSupportHalf) < b.max.z;
            const bool overlapBefore = (prevFrameX_ + kSupportHalf) > b.min.x && (prevFrameX_ - kSupportHalf) < b.max.x;
            const bool feetInside = sunkFeetY > b.min.y && sunkFeetY < b.max.y - kGroundStepTolerance;
            const bool shallow = b.max.y - sunkFeetY <= kHalf * 2.0f;
            if (overlapNow && overlapBefore && feetInside && shallow && b.max.y > recoverTop && hasClearanceAbove(b)) {
                recoverTop = b.max.y;
            }
        }
        if (recoverTop != (std::numeric_limits<float>::lowest)()) {
            pos_.y = recoverTop + kHalf;
            velocityY_ = 0.0f; // 足が上面ちょうどに来るので、この後の着地の判定でそのまま床に立つ
        }
    }

    // 水平方向  側面から重なっているブロックがあれば侵入量が小さい側へ押し出す
    for (const auto& b : blocks) {
        bool overlapY = (pos_.y + kHalf) > b.min.y + kSideOverlapMargin && (pos_.y - kHalf) < b.max.y - kSideOverlapMargin;
        if (!overlapY) {
            continue;
        } // 乗っているだけの上面はここでは無視する

        bool overlapX = (pos_.x + kHalf) > b.min.x && (pos_.x - kHalf) < b.max.x;
        bool overlapZ = (pos_.z + kHalf) > b.min.z && (pos_.z - kHalf) < b.max.z;
        if (!overlapX || !overlapZ) {
            continue;
        }
        // 露出した上面は着地で処理する。積んだ壁の継ぎ目は側面として入ってきた側へ押し戻す。
        if (landedOnTop(b) && hasClearanceAbove(b)) {
            continue;
        }

        float pushLeft = b.min.x - (pos_.x + kHalf); // 負値＝左へ押し出す量
        float pushRight = b.max.x - (pos_.x - kHalf); // 正値＝右へ押し出す量

        // 横スクロールなので左右だけに押し出す。奥行き（Z）へ押し出すと、突進などで深くめり込んだ時に
        // ステージの奥や手前の列へずれ、壁や床の中に埋まったまま戻れなくなる
        // 移動前にブロックの左右どちらにいたかが分かれば、入ってきた側へ戻す
        // （突進やワープで一気に動いて壁の中心を越えても、壁の向こうへ抜けないように）
        const bool cameFromLeft = prevFrameX_ + kHalf <= b.min.x + kSideOverlapMargin;
        const bool cameFromRight = prevFrameX_ - kHalf >= b.max.x - kSideOverlapMargin;
        if (cameFromLeft) {
            pos_.x += pushLeft;
        } else if (cameFromRight) {
            pos_.x += pushRight;
        } else {
            pos_.x += (std::abs(pushLeft) < std::abs(pushRight)) ? pushLeft : pushRight;
        }
    }

    // 垂直方向  足元付近に上面があるブロックのうち一番高いものへ着地させる
    float feetY = pos_.y - kHalf;
    float bestTop = useStageFloor_ ? (std::numeric_limits<float>::lowest)() : Tuning().groundY;
    for (const auto& b : blocks) {
        bool overlapXZ = (pos_.x + kSupportHalf) > b.min.x && (pos_.x - kSupportHalf) < b.max.x
            && (pos_.z + kSupportHalf) > b.min.z && (pos_.z - kSupportHalf) < b.max.z;
        if (!overlapXZ) {
            continue;
        }

        // X/Z が重なっている床のうち、足元より上に出ていない一番高い面を採用する
        // 床をエディタで上下させたときも、古い高さに張り付かないようにする
        // 上から跨いで落ちてきたフレームは、めり込み量が段差許容を超えていても着地を成立させる
        const bool reachable = b.max.y <= feetY + kGroundStepTolerance || landedOnTop(b);
        if (reachable && (b.max.y + kHalf) > bestTop && hasClearanceAbove(b)) {
            bestTop = b.max.y + kHalf;
        }
    }
    if (velocityY_ <= 0.0f && pos_.y <= bestTop + kLandingSnapTolerance) {
        pos_.y = bestTop;
        velocityY_ = 0.0f;
        onGround_ = true;
        SetFrameEvent(FrameEvent::kLanded, !prevOnGround_);
        NotifyWeaponSkillsLanded();
    } else if (velocityY_ <= 0.0f) {
        onGround_ = false;
        SetFrameEvent(FrameEvent::kLanded, false);
    }

    pos_.x = std::clamp(pos_.x, minX_, maxX_);
}

// ══════════════════════════════════════════════════════
// 表示更新と描画
// ══════════════════════════════════════════════════════

/** @brief 現在位置から worldDeltaX だけ先（移動範囲内に収める）へのダッシュを開始する */
void Player::BeginDash(DashMotion& dash, float worldDeltaX) {
    dash.active = true;
    dash.timer = 0.0f;
    dash.startX = pos_.x;
    dash.targetX = std::clamp(pos_.x + worldDeltaX, minX_, maxX_);
}

/** @brief ダッシュを1フレーム進める（pos_.xへ今フレームの進み分を足す）。ちょうど完了したフレームだけtrueを返す */
bool Player::AdvanceDash(DashMotion& dash) {
    if (!dash.active) {
        return false;
    }
    const float prevT = std::clamp(dash.timer / Tuning().dashDuration, 0.0f, 1.0f);
    dash.timer += gameConstants::kFrameDeltaTime;
    const float t = std::clamp(dash.timer / Tuning().dashDuration, 0.0f, 1.0f);
    pos_.x += (dash.targetX - dash.startX) * (easing::EaseOutQuad(t) - easing::EaseOutQuad(prevT));
    if (t >= 1.0f) {
        dash.active = false;
        return true;
    }
    return false;
}

} // namespace engine::game

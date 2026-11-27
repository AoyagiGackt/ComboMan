/**
 * @file EnemyEntity.cpp
 * @brief 武器を持つ敵（EnemyEntity）の性格・攻撃の状態・被弾・描画の実装
 */
#include "EnemyEntity.h"
#include "EngineContext.h"
#include "BossExecutionTuning.h"
#include "CharacterVisuals.h"
#include "GameConstants.h"
#include "GravityBody.h"
#include "GroundSupport.h"
#include "ModelManager.h"
#include "TimeManager.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <optional>
using namespace engine::graphics;

namespace engine::game {
namespace {
constexpr const char* kAnimatedKnightPath = "Resources/Knight/glTF/KnightCharacter.gltf";
constexpr const char* kAnimatedKnightDirectory = "Resources/Knight/glTF";
constexpr const char* kAnimatedKnightFile = "KnightCharacter.gltf";
constexpr const char* kKnightTexture = "Resources/Knight/OBJ/KnightCharacterPalette.png";

/** @brief 敵が持つ武器種ごとの攻撃の性質（時間とダメージは EnemyTuning のどの項目を使うか） */
struct EnemyAttackProfile {
    bool melee; ///< 近接で殴るか（false なら投擲＝遠隔）
    float BasicEnemyTuning::* telegraph; ///< 予備動作の長さ
    float BasicEnemyTuning::* recovery; ///< 攻撃後の隙
    int BasicEnemyTuning::* damage; ///< 与えるダメージ
};
constexpr EnemyAttackProfile kStandardAttack = { true, &BasicEnemyTuning::attackTelegraph, &BasicEnemyTuning::attackInterval, &BasicEnemyTuning::attackDamage };
constexpr EnemyAttackProfile kDaggerAttack = { true, &BasicEnemyTuning::daggerTelegraph, &BasicEnemyTuning::daggerRecovery, &BasicEnemyTuning::attackDamage };
constexpr EnemyAttackProfile kSpearAttack = { false, &BasicEnemyTuning::spearTelegraph, &BasicEnemyTuning::spearRecovery, &BasicEnemyTuning::attackDamage };
constexpr EnemyAttackProfile kHeavyAttack = { true, &BasicEnemyTuning::heavyTelegraph, &BasicEnemyTuning::heavyRecovery, &BasicEnemyTuning::heavyAttackDamage };
constexpr EnemyAttackProfile kThrowAttack = { false, &BasicEnemyTuning::attackTelegraph, &BasicEnemyTuning::attackInterval, &BasicEnemyTuning::attackDamage };
// WeaponType の並び順
constexpr EnemyAttackProfile kAttackProfiles[] = {
    kStandardAttack, // Sword
    kSpearAttack, // Spear
    kHeavyAttack, // Hammer
    kDaggerAttack, // Dagger
    kThrowAttack, // Ball
    kStandardAttack, // Greatsword
    kStandardAttack, // Scythe
    kHeavyAttack, // Axe
};

/** @brief 武器の種類に合った敵の攻撃パラメーターを表から返す */
const EnemyAttackProfile& AttackProfileOf(WeaponType type) {
    const size_t index = static_cast<size_t>(type);
    return index < std::size(kAttackProfiles) ? kAttackProfiles[index] : kStandardAttack;
}


}

/** @brief 性格を基本の敵にし、攻撃状態を待機に戻して初期化する */
EnemyEntity::EnemyEntity()
    : archetype_(&FindArchetype("basic")) {
    ResetAttackState();
}

/** @brief 近接で殴ってくる敵か（槍とボールは投擲＝遠隔扱い） */
bool EnemyEntity::IsMeleeAttacker() const {
    return AttackProfileOf(weaponType_).melee;
}

/** @brief 攻撃がヒットした際に与えるダメージ量を返す */
int EnemyEntity::GetAttackDamage() const {
    // 倍率を掛けても、当たったのに無傷にはならないよう最低1にする
    const int base = EnemyTuning::GetInstance()->Basic().*AttackProfileOf(weaponType_).damage;
    return (std::max)(1, static_cast<int>(std::round(static_cast<float>(base) * damageScale_)));
}

//  Archetype（敵の性格）


/** @brief 地上で武器を使って攻撃する基本の敵の性格 */
class EnemyEntity::BasicArchetype : public IArchetype {
public:
    /** @brief 基本の敵の色を返す */
    Vector4 Color() const override {
        return kDefaultArchetypeColor;
    }
    /** @brief 装備の色に代えて性格固有の色を使用するか返す */
    bool HasOwnColor() const override {
        return false;
    }
};
/** @brief 持ち場の高さで浮遊する敵の性格 */
class EnemyEntity::FlyingArchetype : public IArchetype {
public:
    /** @brief 飛ぶ敵の色を返す */
    Vector4 Color() const override {
        return kFlyingColor;
    }
    /** @brief 持ち場の高さで浮遊する性格か返す */
    bool Hovers() const override {
        return true;
    }
};
/** @brief 自分から攻撃せず周囲の味方を回復する敵の性格 */
class EnemyEntity::HealerArchetype : public IArchetype {
public:
    /** @brief 回復役の敵の色を返す */
    Vector4 Color() const override {
        return kHealerColor;
    }
    /** @brief 自分から攻撃を開始する性格か返す */
    bool Attacks() const override {
        return false;
    }
    /** @brief 周囲の味方を回復する性格か返す */
    bool HealsAllies() const override {
        return true;
    }
};


/** @brief 名前に対応する性格を返す（未知の名前はbasic） */
const EnemyEntity::IArchetype& EnemyEntity::FindArchetype(const std::string& name) {
    static const BasicArchetype kBasic;
    static const FlyingArchetype kFlying;
    static const HealerArchetype kHealer;
    static const std::pair<const char*, const IArchetype*> kArchetypes[] = {
        { "basic", &kBasic },
        { "flying", &kFlying },
        { "healer", &kHealer },
    };
    for (const auto& [archetypeName, archetype] : kArchetypes) {
        if (name == archetypeName) {
            return *archetype;
        }
    }
    return kBasic;
}

/** @brief 性格名（レベルJSONの spawnType）が空を飛ぶ性格か（エディタの配置チェック用） */
bool EnemyEntity::IsFlyingArchetype(const std::string& archetype) {
    return FindArchetype(archetype).Hovers();
}

/** @brief 敵の性格（"basic" "flying" "healer"）を設定し、性格ごとの色に塗る */
void EnemyEntity::SetArchetype(const std::string& archetype) {
    archetype_ = &FindArchetype(archetype);
    SetColor(archetype_->Color());
}

/** @brief モデルを生成し、ワールド座標を設定して初期化する */
void EnemyEntity::Initialize(ModelCommon* modelCommon, const Vector3& startPos, WeaponType weaponType) {
    pos_ = startPos;
    // 出てきたフレームに Update() を通らず壁の判定へ来ても、原点から動いてきたと見なさないよう、動く前の位置もここにそろえる
    previousX_ = startPos.x;
    spawnX_ = startPos.x;
    spawnY_ = startPos.y;
    weaponType_ = weaponType;
    ResetAttackState();
    attackTimer_ = 0.0f;
    justFiredAttack_ = false;

    model_ = engine::Services().models->GetOrLoad(modelCommon,
        "Resources/Knight/OBJ/KnightCharacter.obj",
        "Resources/Knight/OBJ/KnightCharacterPalette.png");

    skinCommon_ = SkinnedObject3d::CreateSharedCommon(modelCommon);
    animatedModel_ = engine::Services().models->GetOrLoadSkinned(
        modelCommon->GetDxCommon(), kAnimatedKnightPath, kKnightTexture);
    object_ = std::make_unique<SkinnedObject3d>();
    object_->Initialize(skinCommon_.get());
    object_->SetModel(animatedModel_);
    object_->SetSkeleton(Skeleton::Create(
        LoadNodeHierarchyFromFile(kAnimatedKnightDirectory, kAnimatedKnightFile)));
    idleAnimation_ = LoadAnimationFile(
        kAnimatedKnightDirectory, kAnimatedKnightFile, "Idle_swordRight");
    runAnimation_ = LoadAnimationFile(
        kAnimatedKnightDirectory, kAnimatedKnightFile, "Run_swordRight");
    attackAnimation_ = LoadAnimationFile(
        kAnimatedKnightDirectory, kAnimatedKnightFile, "Run_swordAttack");
    // ボスの技の動きが使うアニメも、ここで先に読んでおく
    PreloadKnightAnimations();
    // 最初は待機のアニメ（AI を止めた開始演出の間に、走るアニメのまま足踏みしないように）
    object_->SetAnimation(idleAnimation_);
    animationState_ = VisualAnim::kIdle;
    object_->SetEnableLighting(true);
    object_->SetScale({ kBodyScale, kBodyScale, kBodyScale });
    object_->SetPosition(VisualPosition());
    object_->Update();

    // 手に持たせる武器の見た目は、プレイヤーが手に入れた時と同じモデルにする（落とした物と手に入る物の見た目をそろえる）
    // 表に無い種類は表の先頭（剣）を使う
    const HeldWeaponVisual* visual = &kHeldWeaponVisuals[0];
    for (const HeldWeaponVisual& candidate : kHeldWeaponVisuals) {
        if (candidate.type == weaponType) {
            visual = &candidate;
            break;
        }
    }
    // 大きさはプレイヤーの手に持つ大きさに比率を掛け、向きの補正もプレイヤーと同じにする
    weaponScale_ = { visual->gripScale.x * kEnemyWeaponScaleRatio, visual->gripScale.y * kEnemyWeaponScaleRatio,
        visual->gripScale.z * kEnemyWeaponScaleRatio };
    weaponBaseYaw_ = visual->gripRotate.y;
    weaponModel_ = engine::Services().models->GetOrLoad(modelCommon, visual->modelPath, visual->texturePath);
    weaponObject_ = std::make_unique<Object3d>();
    weaponObject_->Initialize(modelCommon);
    weaponObject_->SetModel(weaponModel_);
    weaponObject_->SetEnableLighting(true);
    weaponObject_->SetScale(weaponScale_);
    weaponObject_->SetPosition({ pos_.x + facingSign_ * kWeaponOffsetX, pos_.y + kWeaponOffsetY, pos_.z + kWeaponOffsetZ });
    weaponObject_->SetRotation({ 0.0f, (facingSign_ >= 0.0f ? gameConstants::kHalfPi : -gameConstants::kHalfPi) + weaponBaseYaw_, kWeaponRestTilt });
    weaponObject_->Update();
}

/** @brief 物理・アニメーションを毎フレーム更新する */
void EnemyEntity::Update(float playerX) {
    // 吹き飛んで壁を突き抜けたかを ResolveBlownAway で調べるため、動く前の横位置を残す
    previousX_ = pos_.x;
    // ヒットストップ中はプレイヤーと同じく世界ごと止める（止めないと吹き飛びだけが先に進んで手応えが薄れる）
    // 敵の時間の通り道だけが止まっている間（斬った後の間）も、同じように止まった見た目にする
    if (engine::Services().time->IsChannelStopped(timeChannel::kEnemies)) {
        UpdateDuringHitStop();
        return;
    }
    hitStopShakeFrame_ = 0;
    if (IsScripted()) {
        UpdateScripted();
        return;
    }
    // この敵の時間だけ遅い間は、進めないフレームを止まった時と同じ見た目の更新だけにする
    if (SkipFrameForTimeSlow()) {
        UpdateDuringHitStop();
        return;
    }

    // 各タイマーを1フレーム進める（減速とのけぞりは0で止める）
    const BasicEnemyTuning& tuning = EnemyTuning::GetInstance()->Basic();
    archetypeTimer_ += gameConstants::kFrameDeltaTime;
    justLanded_ = false;
    slowTimer_ = (std::max)(slowTimer_ - gameConstants::kFrameDeltaTime, 0.0f);
    hitstunTimer_ = (std::max)(hitstunTimer_ - gameConstants::kFrameDeltaTime, 0.0f);

    // 撃破後は倒れた時の向きを保ち、プレイヤーの移動に反応しない。
    if (!defeated_) {
        facingSign_ = (playerX >= pos_.x) ? 1.0f : -1.0f;
    }

    // 横移動: ノックバック中はその勢いで滑り、そうでなければ条件付きでプレイヤーへ歩く
    bool walkedThisFrame = false;
    walkDirX_ = 0.0f;
    if (std::abs(knockVelX_) > kKnockbackStopSpeed) {
        pos_.x += knockVelX_ * (slowTimer_ > 0.0f ? tuning.knockbackSlowMultiplier : 1.0f);
        knockVelX_ *= tuning.knockbackDecay;
    } else if (!defeated_ && !isLaunched_ && !IsInHitstun() && attackState_->IsIdle()) {
        // 予備動作/攻撃中やノックバック中は歩かせない。プレイヤーが持ち場に近づいてくるまでは待機し、
        // 近づいてきたら間合いの外にいる間だけ追うが、持ち場から離れすぎたら止まる（全員が団子にならないように）
        // 持ち場に縛らない敵（戦いが始まったステージのボス）は、どこまでも追う（壁と足場の端はシーンが止める）
        const bool playerNearPost = chaseUnleashed_ || std::abs(playerX - spawnX_) <= tuning.aggroRange;
        const bool withinLeash = chaseUnleashed_ || std::abs(pos_.x - spawnX_) < tuning.leashDistance;
        const float dx = playerX - pos_.x;
        // プレイヤーが高い所にいる時は、壁を登れるよう間合いの中でも近づく
        const bool playerAbove = hasTargetHeight_ && targetY_ - pos_.y > tuning.climbChaseHeight;
        const float stopDistance = playerAbove ? tuning.climbChaseStopDistance : tuning.engageRange;
        if (playerNearPost && withinLeash && std::abs(dx) > stopDistance) {
            const float speed = tuning.approachSpeed * speedScale_ * statusSpeedScale_;
            walkDirX_ = dx > 0.0f ? 1.0f : -1.0f;
            pos_.x += walkDirX_ * speed;
            walkedThisFrame = true;
        }
    }

    // 打ち上げ中だけ重力で落とし、着地したら着地した瞬間として知らせる
    if (isLaunched_) {
        airComboTimer_ = (std::max)(airComboTimer_ - gameConstants::kFrameDeltaTime, 0.0f);
        const float gravity = airComboTimer_ > 0.0f ? tuning.gravity * tuning.airComboGravityScale : tuning.gravity;
        // 床に着く直前の落ちる速さ（速く落ちてきた吹き飛びは床で跳ねる）
        const float fallSpeed = -(velY_ - gravity);
        if (ApplyGravityAndClampY(pos_.y, velY_, gravity, launchOriginY_, tuning.ceilingY, kCeilingBounceFactor)
            && !TryFloorBounce(fallSpeed)) {
            isLaunched_ = false;
            justLanded_ = true;
        }
    }
    // 非打ち上げ中の高さは UpdateTerrain() が地形に合わせる

    // 飛ぶ種類は出現位置の高さを中心に上下にゆらゆらさせる
    // （打ち上げや急降下でずれた高さから一気に戻すと瞬間移動に見えるので、のけぞり中はその場に留め、明けたら少しずつ戻す）
    if (archetype_->Hovers() && !isLaunched_ && !defeated_ && !IsInHitstun()) {
        const float hoverY = spawnY_ + std::sin(archetypeTimer_ * tuning.flyingBobSpeed) * tuning.flyingBobAmplitude;
        pos_.y += (hoverY - pos_.y) * kHoverReturnRate;
    }

    // 本体・落とした武器・手持ちの武器の位置を反映し、攻撃できる種類なら攻撃の状態を進める
    const Vector3 visualPos = VisualPosition();
    object_->SetPosition(visualPos);
    UpdateDroppedWeapon();
    PlaceWeapon(visualPos);

    if (climbing_) {
        // 登っている間は攻撃を始めない（壁に向かって振らないように）
        justFiredAttack_ = false;
        justStartedTelegraph_ = false;
    } else if (archetype_->Attacks()) {
        UpdateAttack(playerX);
    } else {
        ResetAttackState();
        justFiredAttack_ = false;
    }

    // 攻撃動作が最優先、次に接近歩行の走り、どちらでもなければ立ち姿勢
    // （歩行中にIdleのままだと棒立ちで滑って見える）
    const VisualAnim desiredAnimationState = !attackState_->IsIdle() ? VisualAnim::kAttack
        : walkedThisFrame                                                      ? VisualAnim::kRun
                                                                               : VisualAnim::kIdle;
    // ボスの技の動きが下のアニメを決めている間は切り替えない（外れた時は掛け直す）
    const bool reapplyAnimation = desiredAnimationState != animationState_ || motionAnimOverride_;
    if (!defeated_ && !MotionOwnsAnimation() && reapplyAnimation) {
        motionAnimOverride_ = false;
        object_->SetAnimation(desiredAnimationState == VisualAnim::kAttack ? attackAnimation_
                : desiredAnimationState == VisualAnim::kRun                ? runAnimation_
                                                                          : idleAnimation_);
        animationState_ = desiredAnimationState;
    }

    // 体の傾きと武器の角度: 撃破後は倒れた姿勢、のけぞり中は反らせ、それ以外は攻撃の状態に合わせる
    // 倒れたら寝かせる（膝をつくボスは少し前へ傾けるだけ）
    const float defeatedLean = IsKneeling() ? BossExecutionTuningStore::Get().kneel.bodyLean : kDefeatedBodyLean;
    float bodyLean = defeated_ ? facingSign_ * defeatedLean : 0.0f;
    float weaponSwing = defeated_ ? facingSign_ * kDefeatedWeaponTilt : kWeaponRestTilt;
    if (!defeated_ && !isLaunched_) {
        if (IsInHitstun()) {
            // 硬直が残っているほど大きく反らせ、明けるにつれて構えへ戻す
            const float leanRatio = (std::min)(hitstunTimer_ / kHitstunLeanFullSeconds, 1.0f);
            bodyLean = kHitstunBodyLean * leanRatio;
            weaponSwing = kHitstunWeaponSwing;
        } else {
            attackState_->ApplyPose(bodyLean, weaponSwing);
        }
    }
    if (homeRunSpinTimer_ > 0.0f) {
        // 打ち飛ばされている間は縦に回転しながら飛ぶ
        homeRunSpinTimer_ = (std::max)(homeRunSpinTimer_ - gameConstants::kFrameDeltaTime, 0.0f);
        homeRunSpinAngle_ += kHomeRunSpinSpeed;
        bodyLean += homeRunSpinAngle_;
    }
    // 向きと傾きをモデルへ反映する（落とした武器は寝かせて回す）
    const float facingYaw = facingSign_ >= 0.0f ? gameConstants::kHalfPi : -gameConstants::kHalfPi;
    object_->SetRotation({ 0.0f, facingYaw, bodyLean });
    if (weaponDropped_) {
        weaponObject_->SetRotation({ 0.0f, facingYaw + droppedWeaponSpin_ + weaponBaseYaw_, kDroppedWeaponLieTilt });
    } else {
        weaponObject_->SetRotation({ 0.0f, facingYaw + weaponBaseYaw_, weaponSwing });
    }

    // 被弾フラッシュ: 基準色→白へ寄せ、本体を一瞬膨らませてから戻す
    float flash = 0.0f;
    if (hitFlashTimer_ > 0.0f) {
        hitFlashTimer_ = (std::max)(hitFlashTimer_ - gameConstants::kFrameDeltaTime, 0.0f);
        flash = hitFlashTimer_ / kHitFlashDuration;
    }
    Vector4 flashColor = kDefeatedColor;
    if (!defeated_) {
        // 予備動作中は基準色を警告色へ寄せる（被弾フラッシュはその上から白へ寄せる）
        const float warn = IsTelegraphing() ? kTelegraphTintStrength : 0.0f;
        const Vector4 tintedColor = {
            baseColor_.x + (kTelegraphTint.x - baseColor_.x) * warn,
            baseColor_.y + (kTelegraphTint.y - baseColor_.y) * warn,
            baseColor_.z + (kTelegraphTint.z - baseColor_.z) * warn,
            baseColor_.w
        };
        flashColor = {
            tintedColor.x + (1.0f - tintedColor.x) * flash,
            tintedColor.y + (1.0f - tintedColor.y) * flash,
            tintedColor.z + (1.0f - tintedColor.z) * flash,
            tintedColor.w
        };
    }
    // 回復された直後は緑に光らせる
    if (!defeated_) {
        flashColor = ApplyHealGlow(flashColor, true);
    }
    // 決めた色と被弾時の膨らみを反映して行列を更新する
    SetBodyColor(flashColor);
    weaponObject_->SetColor(flashColor);
    UpdateOfferedWeapon(true);
    const float bodyScale = kBodyScale * (1.0f + kHitScalePunch * flash);
    object_->SetScale({ bodyScale, bodyScale, bodyScale });

    // ボスの技の動きは体の行列を作る前に掛け、武器は体の行列ができてから手元へ付ける
    ApplyMotionBody();
    object_->Update();
    PlaceMotionWeapon();
    weaponObject_->Update();
    SyncMonsterVisual(true);
}

/** @brief 攻撃ステートマシンを毎フレーム進める（Update() から呼ぶ） */
void EnemyEntity::UpdateAttack(float playerX) {
    justFiredAttack_ = false;
    justStartedTelegraph_ = false;

    // 打ち上げ中/撃破後はステートマシンを止め、丸腰の演出中に攻撃が発生しないようにする
    if (defeated_ || isLaunched_) {
        return;
    }
    // 硬直中は攻撃の進行ごと止める（殴られながら平然と攻撃を続けないように）
    if (IsInHitstun()) {
        return;
    }

    attackTimer_ -= gameConstants::kFrameDeltaTime;
    if (attackTimer_ > 0.0f) {
        return;
    }

    attackState_->Advance(*this, EnemyTuning::GetInstance()->Basic(), playerX);
}

//  Attack State（攻撃の進行フェーズ）
/** @brief 攻撃間隔とプレイヤーとの距離が条件を満たすまで待機する状態 */
class EnemyEntity::IdleAttackState : public IAttackState {
public:
    void Advance(EnemyEntity& enemy, const BasicEnemyTuning& tuning, float playerX) const override;
    /** @brief 待機中は体の傾きと武器の振りを変えない */
    void ApplyPose(float&, float&) const override { }
    /** @brief 待機状態かを返す */
    bool IsIdle() const override {
        return true;
    }
};
/** @brief 予備動作を表示して攻撃の発生を待つ状態 */
class EnemyEntity::TelegraphAttackState : public IAttackState {
public:
    void Advance(EnemyEntity& enemy, const BasicEnemyTuning& tuning, float playerX) const override;
    /** @brief 予備動作の体の傾きと武器の振りかぶりを設定する */
    void ApplyPose(float& bodyLean, float& weaponSwing) const override {
        bodyLean = kTelegraphBodyLean;
        weaponSwing = kTelegraphWeaponSwing;
    }
    /** @brief 予備動作の状態かを返す */
    bool IsTelegraph() const override {
        return true;
    }
};
/** @brief 攻撃判定を発生させ、持続時間の終了後に待機へ戻る状態 */
class EnemyEntity::ActiveAttackState : public IAttackState {
public:
    void Advance(EnemyEntity& enemy, const BasicEnemyTuning& tuning, float playerX) const override;
    /** @brief 攻撃中の体の傾きと武器の振りを設定する */
    void ApplyPose(float& bodyLean, float& weaponSwing) const override {
        bodyLean = kActiveBodyLean;
        weaponSwing = kActiveWeaponSwing;
    }
};
/** @brief プレイヤーが攻撃開始の間合いに入ったら予備動作へ移る */
void EnemyEntity::IdleAttackState::Advance(EnemyEntity& enemy, const BasicEnemyTuning& tuning, float playerX) const {
    // 遠隔の敵は持ち場基準の索敵距離、近接の敵は自分からの間合いで攻撃開始を判定する
    // （遠くの敵が延々と素振り・発砲を繰り返さないように。近接敵は届く距離でしか振らない）
    const bool inStartRange = enemy.IsMeleeAttacker()
        ? std::abs(playerX - enemy.pos_.x) <= tuning.meleeAttackRange
        : std::abs(playerX - enemy.spawnX_) <= tuning.aggroRange;
    if (!inStartRange) {
        enemy.attackTimer_ = 0.0f;
        return;
    }
    enemy.attackState_ = &AttackStateOf<TelegraphAttackState>();
    enemy.justStartedTelegraph_ = true;
    enemy.attackTimer_ = tuning.*AttackProfileOf(enemy.weaponType_).telegraph;
}
/** @brief 予備動作が終わったので攻撃を出す状態へ移る */
void EnemyEntity::TelegraphAttackState::Advance(EnemyEntity& enemy, const BasicEnemyTuning& tuning, float) const {
    enemy.attackState_ = &AttackStateOf<ActiveAttackState>();
    enemy.attackTimer_ = tuning.attackActive;
    enemy.justFiredAttack_ = true;
}
/** @brief 攻撃が終わったので硬直時間を設定して待機へ戻る */
void EnemyEntity::ActiveAttackState::Advance(EnemyEntity& enemy, const BasicEnemyTuning& tuning, float) const {
    enemy.attackState_ = &AttackStateOf<IdleAttackState>();
    enemy.attackTimer_ = tuning.*AttackProfileOf(enemy.weaponType_).recovery;
}
/** @brief 現在の攻撃状態の共有インスタンスを返す */
template <class T>
const EnemyEntity::IAttackState& EnemyEntity::AttackStateOf() {
    static const T kInstance;
    return kInstance;
}
/** @brief クールダウン状態へ戻す（被弾・打ち飛ばし・回復役など、攻撃を中断する時に使う） */
void EnemyEntity::ResetAttackState() {
    attackState_ = &AttackStateOf<IdleAttackState>();
}
/** @brief モデルを描画する */
void EnemyEntity::Draw() {
    if (!visible_) {
        return;
    }
    // モンスターは武器を持たないので、差し替えた本体だけを描く
    if (monsterObject_) {
        monsterObject_->Draw();
        return;
    }
    // 別の見た目を重ねている間も、落とした武器と撃破後に浮かべる武器は拾えるように描く
    if (!bodyHidden_) {
        object_->Draw();
    }
    if ((!bodyHidden_ || weaponDropped_ || defeated_) && !weaponHidden_) {
        weaponObject_->Draw();
    }
}
/** @brief 見た目を騎士からモンスターのモデルに差し替える（動き・攻撃・被弾の挙動は騎士と同じものを使う） */
void EnemyEntity::SetMonsterVisual(ModelCommon* modelCommon, const std::string& kind) {
    const EnemyVisualTuning* def = EnemyTuning::GetInstance()->FindVisual(kind);
    if (def == nullptr) {
        return;
    }
    monsterObject_ = std::make_unique<Object3d>();
    monsterObject_->Initialize(modelCommon);
    monsterObject_->SetModel(engine::Services().models->GetOrLoad(modelCommon, def->model, def->texture));
    monsterObject_->SetEnableLighting(true);
    monsterScale_ = def->scale;
    monsterBodyHalfWidth_ = def->bodyHalfWidth;
    monsterBodyHeight_ = def->bodyHeight;
    monsterYawOffset_ = def->yawOffset;
    monsterDepthOffset_ = def->depthOffset;
    monsterKind_ = kind;
    // 素材の色をそのまま見せる（回復役だけは緑で見分けられるようにする）
    SetColor(IsHealer() ? kHealerColor : kDefaultArchetypeColor);
    SyncMonsterVisual(false);
}
/** @brief 飛ぶ種類が上下の揺れや元の高さへ戻る動きで足場に入った時、浅い方（上か下）へ押し出す（横から入った分は壁からの押し出しに任せる） */
void EnemyEntity::PushHoverOutOfBlocksVertically(const std::vector<engine::AABB>& solids) {
    const EnemySeparationTuning& separation = EnemyTuning::GetInstance()->Separation();
    const float halfWidth = WallBodyHalfWidth(separation.bodyHalfWidth);
    // 飛ぶ種類は床に立たないので、歩く敵のように足元を少し空けず、足元から体として数える（足元が足場の上端より下に沈まないように）
    constexpr float bandBottom = 0.0f;
    const float bandTop = WallBodyHeight(separation.wallBandTop);
    const float startY = pos_.y;
    const float startX = pos_.x;
    const engine::AABB* blockAbove = nullptr; // 元の高さへ戻るのを上から塞いでいる足場
    for (const engine::AABB& block : solids) {
        const float bodyBottom = pos_.y + bandBottom;
        const float bodyTop = pos_.y + bandTop;
        const bool overlaps = pos_.z >= block.min.z && pos_.z <= block.max.z && pos_.x + halfWidth > block.min.x
            && pos_.x - halfWidth < block.max.x && bodyTop > block.min.y && bodyBottom < block.max.y;
        if (!overlaps) {
            continue;
        }
        // 横から入ったかどうかは、壁からの押し出し（EnemySeparation）と同じ体の範囲（足元を wallBandBottom だけ空ける）で決める。
        // 違う範囲で決めると、どちらも相手が押し出すとみなして、足場の中に残ってしまう
        const float sideBodyBottom = pos_.y + separation.wallBandBottom;
        const float sideVerticalDepth = (std::min)(bodyTop, block.max.y) - (std::max)(sideBodyBottom, block.min.y);
        const float horizontalDepth = (std::min)(pos_.x + halfWidth - block.min.x, block.max.x - (pos_.x - halfWidth));
        if (sideVerticalDepth >= horizontalDepth) {
            continue;
        }
        // 体の真ん中が足場の真ん中より上なら上へ、下なら下へ出す
        const float bodyCenter = (bodyBottom + bodyTop) * kHalfRatio;
        const float blockCenter = (block.min.y + block.max.y) * kHalfRatio;
        pos_.y = bodyCenter >= blockCenter ? block.max.y - bandBottom + kHoverBlockClearance : block.min.y - bandTop - kHoverBlockClearance;
        if (bodyCenter < blockCenter && spawnY_ > block.max.y) {
            blockAbove = &block;
        }
    }
    // 足場の下へ入り込んで元の高さへ戻れない時は、つながった足場の近い方の端へ横にずれて、外へ出てから上がる
    if (blockAbove != nullptr) {
        SlideOutFromUnderBlock(*blockAbove, solids, halfWidth);
    }
    if (pos_.y != startY || pos_.x != startX) {
        RefreshVisualTransforms();
    }
}

/** @brief 飛ぶ種類が足場の下に入り込んだ時、横につながった足場の近い方の端へ少しずつずらす（真上が塞がれて元の高さへ戻れないため） */
void EnemyEntity::SlideOutFromUnderBlock(const engine::AABB& block, const std::vector<engine::AABB>& solids, float halfWidth) {
    // 同じ高さで横に接している足場をつなげ、足場全体の左右の端を求める
    float left = block.min.x;
    float right = block.max.x;
    bool extended = true;
    while (extended) {
        extended = false;
        for (const engine::AABB& other : solids) {
            const bool sameBand = other.min.y < block.max.y && other.max.y > block.min.y;
            if (!sameBand) {
                continue;
            }
            if (other.min.x < left && other.max.x >= left - kHoverSlideJoinGap) {
                left = other.min.x;
                extended = true;
            }
            if (other.max.x > right && other.min.x <= right + kHoverSlideJoinGap) {
                right = other.max.x;
                extended = true;
            }
        }
    }
    // 近い方の端の外まで、体の幅ぶん出るように少しずつ動かす
    const float toLeft = pos_.x - (left - halfWidth);
    const float toRight = (right + halfWidth) - pos_.x;
    const float step = EnemyTuning::GetInstance()->Basic().approachSpeed * kHoverSlideSpeedScale;
    pos_.x += toLeft <= toRight ? -(std::min)(step, toLeft) : (std::min)(step, toRight);
}

/** @brief 地形に合わせて高さを変える（壁を登る・段差に乗る・足場から落ちる） */
void EnemyEntity::UpdateTerrain(const std::vector<engine::AABB>& solids) {
    // 打ち上げや倒れた後の落下は別の処理が動かし、飛ぶ種類は地形に関係なく浮く（上下から足場に入った分だけ押し出す）
    if (defeated_ || isLaunched_ || archetype_->Hovers() || IsScripted()) {
        climbing_ = false;
        fallVelY_ = 0.0f;
        if (archetype_->Hovers() && !defeated_ && !isLaunched_ && !IsScripted()) {
            PushHoverOutOfBlocksVertically(solids);
        }
        return;
    }
    const BasicEnemyTuning& tuning = EnemyTuning::GetInstance()->Basic();
    const EnemySeparationTuning& separation = EnemyTuning::GetInstance()->Separation();
    const float halfWidth = WallBodyHalfWidth(separation.bodyHalfWidth);
    const float height = WallBodyHeight(separation.wallBandTop);
    const float footOffset = tuning.corpseFootOffset;
    const float startY = pos_.y;
    const auto spansDepth = [this](const engine::AABB& block) {
        return pos_.z >= block.min.z && pos_.z <= block.max.z;
    };

    // 歩いている向きの体の端に当たるブロックを探し、上に積まれた分も含めた壁の上面を求める
    std::optional<float> wallTop;
    if (walkDirX_ != 0.0f) {
        const float edgeX = pos_.x + walkDirX_ * halfWidth;
        const auto touchesEdge = [&](const engine::AABB& block) {
            return spansDepth(block) && edgeX + separation.wallContactMargin > block.min.x
                && edgeX - separation.wallContactMargin < block.max.x;
        };
        for (const engine::AABB& block : solids) {
            if (touchesEdge(block) && block.max.y > pos_.y + separation.wallBandBottom && block.min.y < pos_.y + height) {
                wallTop = wallTop ? (std::max)(*wallTop, block.max.y) : block.max.y;
            }
        }
        // 接しているブロックが新しく見つからなくなるまで、上へたどる（並べた壁は1個ずつのブロックになっている）
        for (bool extended = wallTop.has_value(); extended;) {
            extended = false;
            for (const engine::AABB& block : solids) {
                if (touchesEdge(block) && block.min.y <= *wallTop + separation.wallContactMargin && block.max.y > *wallTop) {
                    wallTop = block.max.y;
                    extended = true;
                }
            }
        }
    }
    // 登れる高さの壁で、頭の上が塞がれていなければ登る（頭の判定は壁に掛からないよう体の中ほどだけで見る）
    constexpr float kHeadCheckWidthRatio = 0.5f;
    const float headHalfWidth = halfWidth * kHeadCheckWidthRatio;
    const bool headBlocked = std::any_of(solids.begin(), solids.end(), [&](const engine::AABB& block) {
        return spansDepth(block) && pos_.x + headHalfWidth > block.min.x && pos_.x - headHalfWidth < block.max.x
            && block.max.y > pos_.y + height && block.min.y < pos_.y + height + tuning.climbSpeed;
    });
    climbing_ = wallTop && !headBlocked && *wallTop + footOffset - pos_.y <= tuning.climbMaxHeight;
    if (climbing_) {
        pos_.y = (std::min)(pos_.y + tuning.climbSpeed, *wallTop + footOffset);
        fallVelY_ = 0.0f;
    } else if (const auto floor = FindSupportingFloor(pos_, halfWidth, 0.0f, footOffset - tuning.stepUpHeight, solids)) {
        // 足元が床より下なら段差に乗せ、上なら重力で落とす（床が無い場所では配置した高さのまま）
        const float standY = *floor + footOffset;
        if (pos_.y <= standY) {
            pos_.y = standY;
            fallVelY_ = 0.0f;
        } else {
            fallVelY_ -= tuning.gravity;
            pos_.y = (std::max)(pos_.y + fallVelY_, standY);
        }
    }
    if (pos_.y != startY) {
        RefreshVisualTransforms();
    }
}

/** @brief 騎士の本体に計算した位置・向き・大きさ・色を、モンスターの見た目へ写す */
void EnemyEntity::SyncMonsterVisual(bool advanceBob) {
    if (!monsterObject_) {
        return;
    }
    if (advanceBob) {
        monsterBobTimer_ += gameConstants::kFrameDeltaTime;
    }
    // 素材は静止モデルなので、生きている間はその場で小さく弾ませて動きを足す
    // 床より下へ沈まないよう、足元から上へだけ弾ませる
    constexpr float kHalf = 0.5f;
    const float bob = defeated_ ? 0.0f : (1.0f + std::sin(monsterBobTimer_ * kMonsterBobSpeed)) * kHalf * kMonsterBobAmplitude;
    const Vector3 bodyPos = object_->GetPosition();
    // 騎士の本体スケールに対する比（被弾した瞬間の膨らみ）をモンスターにも掛ける
    const float punch = object_->GetScale().x / kBodyScale;
    const float scale = monsterScale_ * punch;
    // 大きい体は見た目だけ奥へずらし、重なったプレイヤーが体の中に隠れて消えたように見えないようにする
    monsterObject_->SetPosition({ bodyPos.x, bodyPos.y + bob, bodyPos.z + monsterDepthOffset_ });
    Vector3 rotation = object_->GetRotation();
    rotation.y += monsterYawOffset_;
    monsterObject_->SetRotation(rotation);
    monsterObject_->SetScale({ scale, scale, scale });
    monsterObject_->SetColor(bodyColor_);
    monsterObject_->Update();
}
/** @brief AIを止めている間（ポーズ・開始演出など）も、向きだけは指定したX座標の方へ合わせる */
void EnemyEntity::FaceTowardX(float targetX) {
    if (defeated_) {
        return;
    }
    facingSign_ = (targetX >= pos_.x) ? 1.0f : -1.0f;
    const float facingYaw = facingSign_ >= 0.0f ? gameConstants::kHalfPi : -gameConstants::kHalfPi;
    // 体の傾きなど他の軸はそのまま、向き（Y軸回転）だけを差し替える
    Vector3 bodyRotation = object_->GetRotation();
    bodyRotation.y = facingYaw;
    object_->SetRotation(bodyRotation);
    if (weaponObject_ && !weaponDropped_) {
        Vector3 weaponRotation = weaponObject_->GetTransform().rotate;
        weaponRotation.y = facingYaw + weaponBaseYaw_;
        weaponObject_->SetRotation(weaponRotation);
    }
}
} // namespace engine::game

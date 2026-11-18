/**
 * @file BossExecution.cpp
 * @brief ボスへのとどめの演出（BossExecution）の拍の進め方と、拍ごとの演出
 */
#include "BossExecution.h"
#include "Camera.h"
#include "CameraShaker.h"
#include "GameConstants.h"
#include "GameSounds.h"
#include "GunCombo.h"
#include "HitEffectLibrary.h"
#include "MeleeCombo.h"
#include "Player.h"
#include "SceneShared.h"
#include "TimeManager.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace engine::game {
namespace {
constexpr float kHalf = 0.5f;
constexpr float kSmoothStepA = 3.0f; ///< なめらかな寄せ方（3t^2 - 2t^3）の係数
constexpr float kSmoothStepB = 2.0f;
constexpr float kMinMoveSeconds = 0.0001f; ///< これ以下の秒数は一瞬で移るとみなす
constexpr float kMinShotSeconds = 0.0001f; ///< 射撃の段の長さの下限（0 で割らないように）
constexpr float kAimHoldMargin = 0.001f; ///< 構え切った所を、発砲の瞬間のわずか手前で止める幅（発砲の瞬間まで進めると反動の姿勢になる）
constexpr char kGroundPrefix = 'g';
constexpr char kAirPrefix = 'a';
constexpr char kShotPrefix = 's';
constexpr const char* kShotLast = "slast";
constexpr const char* kFaceBoss = "boss";
constexpr const char* kFaceAway = "away";
constexpr const char* kGroundLast = "glast";
constexpr const char* kAirLast = "alast";
constexpr const char* kLauncher = "launch";
constexpr float kPlayerBodyHalfWidth = 0.5f; ///< プレイヤーの当たり判定の幅の半分（Player の 1x1x1 と同じ）
constexpr float kPlayerBodyHalfHeight = 0.5f; ///< プレイヤーの当たり判定の高さの半分
constexpr float kWallRoomMargin = 0.05f; ///< 回り込んだプレイヤーと壁の間に空けるすき間
constexpr float kFloorClearance = 0.1f; ///< 回り込む広さを調べる時に体の下端を上げる幅（立っている床を壁と数えない）

/** @brief 決まった効果音の名前と場面の対応 */
struct CueEntry {
    const char* name;
    SoundCue cue;
};
constexpr CueEntry kCues[] = {
    { "beat", SoundCue::kFinisherBeat },
    { "release", SoundCue::kFinisherRelease },
    { "glass", SoundCue::kGlassShatter },
    { "swing", SoundCue::kSwing },
    { "shot", SoundCue::kShot },
};

/** @brief 0〜1 をなめらかに寄せる */
float SmoothStep(float t) {
    const float c = std::clamp(t, 0.0f, 1.0f);
    return c * c * (kSmoothStepA - kSmoothStepB * c);
}

/** @brief 2点の距離 */
float MoveDistance(const Vector3& a, const Vector3& b) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float dz = b.z - a.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

/** @brief 段の並びから番号の段を返す（範囲外は nullptr） */
const MeleeAttackDef* StepAt(const ComboArray<MeleeAttackDef>& steps, int index) {
    if (steps.data == nullptr || index < 0 || index >= steps.count) {
        return nullptr;
    }
    return &steps[index];
}
} // namespace

/** @brief 調整値をファイルから読み直す */
void BossExecution::ReloadTuning() {
    BossExecutionTuningStore::Reload();
}

/** @brief 演出を最初から始める */
void BossExecution::Start(const Setup& setup, const Services& services) {
    services_ = services;
    // 共通の歩み寄り（銃で倒した時は銃の歩み寄り）の後に型の拍をつないで写しておく
    const auto& tuning = BossExecutionTuningStore::Get();
    byGun_ = setup.byGun;
    byBomb_ = setup.byBomb;
    gun_ = setup.gun;
    gunColor_ = setup.gunColor;
    beats_ = byBomb_ ? tuning.bombIntro : (byGun_ ? tuning.gunIntro : tuning.intro);
    endHold_ = 0.0f;
    if (setup.style != nullptr) {
        beats_.insert(beats_.end(), setup.style->beats.begin(), setup.style->beats.end());
        endHold_ = setup.style->endHold;
    }
    weapon_ = setup.weapon;
    bossPos_ = setup.bossPosition;
    const Vector3 playerPos = services_.player->GetPosition();
    side_ = playerPos.x >= bossPos_.x ? 1.0f : -1.0f;
    // 宙で始めても床に立った高さを基準にする（ボスの位置は足元の高さなので、そのまま使うと体半分だけ床へ沈む）
    groundY_ = setup.standingY;
    playerZ_ = playerPos.z;
    playerPos_ = { playerPos.x, groundY_, playerZ_ };
    // 壁際で倒した時は、回り込む動きや歩み寄る動きが壁にめり込まないよう、ボスとプレイヤーをそろって壁から離す
    if (setup.terrain != nullptr) {
        const float shift = side_ * RoomAroundBossShift(*setup.terrain);
        bossPos_.x += shift;
        playerPos_.x += shift;
    }

    active_ = true;
    finished_ = false;
    beatIndex_ = 0;
    beatTimer_ = 0.0f;
    endTimer_ = 0.0f;
    moving_ = false;
    walking_ = false;
    faceAway_ = false;
    grabbed_ = false;
    grabPending_ = false;
    slicePending_ = false;
    bombPending_ = false;
    poseAttack_ = nullptr;
    poseShot_ = nullptr;
    gunPosed_ = false;
    poseTimer_ = 0.0f;
    poseSeconds_ = 0.0f;
    poseFrom_ = 0.0f;
    poseTo_ = 1.0f;
    zoom_ = 1.0f;
    targetZoom_ = 1.0f;
    cameraFocus_ = { (playerPos_.x + bossPos_.x) * kHalf, groundY_ + tuning.cameraHeight, bossPos_.z };
    ApplyPlayerPose();
}

/** @brief 1フレーム進める */
void BossExecution::Update(float dt) {
    if (!active_) {
        return;
    }
    UpdateMove(dt);
    poseTimer_ += dt;

    // 待ちが明けた拍をまとめて行う（delay 0 の拍は同じフレームに続けて行う）
    if (!finished_) {
        beatTimer_ += dt;
        for (const ExecutionBeat* beat = NextBeat(); beat != nullptr; beat = NextBeat()) {
            if (beat->afterMove && moving_) {
                beatTimer_ = 0.0f;
                break;
            }
            if (beatTimer_ < beat->delay) {
                break;
            }
            beatTimer_ = 0.0f;
            ++beatIndex_;
            Fire(*beat);
        }
        // 拍を出し切ったら余韻を見せて終える
        if (NextBeat() == nullptr && !moving_) {
            endTimer_ += dt;
            if (endTimer_ >= endHold_) {
                finished_ = true;
            }
        }
    }
    ApplyPlayerPose();
    UpdateCamera();
}

/** @brief 演出を閉じ、プレイヤーを技の姿勢から戻す */
void BossExecution::End() {
    if (!active_) {
        return;
    }
    active_ = false;
    services_.player->EndMeleePreview();
}

/** @brief 武器を抜き取った最初の1回だけ true */
bool BossExecution::ConsumeGrab() {
    const bool pending = grabPending_;
    grabPending_ = false;
    return pending;
}

/** @brief ボスを切り分ける拍の最初の1回だけ true */
bool BossExecution::ConsumeSlice() {
    const bool pending = slicePending_;
    slicePending_ = false;
    return pending;
}

/** @brief めり込んだ爆弾を爆発させる拍の最初の1回だけ true */
bool BossExecution::ConsumeBomb() {
    const bool pending = bombPending_;
    bombPending_ = false;
    return pending;
}

/** @brief 次に行う拍 */
const ExecutionBeat* BossExecution::NextBeat() const {
    return beatIndex_ < beats_.size() ? &beats_[beatIndex_] : nullptr;
}

/** @brief 拍に書いてある事をまとめて行う */
void BossExecution::Fire(const ExecutionBeat& beat) {
    if (beat.face == kFaceBoss) {
        faceAway_ = false;
    } else if (beat.face == kFaceAway) {
        faceAway_ = true;
    }
    if (beat.grab && !grabbed_) {
        grabbed_ = true;
        grabPending_ = true;
    }
    StartPose(beat);
    if (beat.move) {
        BeginMove(beat);
    }
    if (beat.zoom > 0.0f) {
        targetZoom_ = beat.zoom;
    }
    // 動いた先から撃つように、移動を始めた後で弾を出す
    if (beat.shot) {
        FireShot();
    }
    FireSlash(beat);
    FireEffects(beat);
    if (beat.slice) {
        slicePending_ = true;
    }
    if (beat.bomb) {
        bombPending_ = true;
    }
}

/** @brief 拍に書いてある姿勢を取り始める */
void BossExecution::StartPose(const ExecutionBeat& beat) {
    if (!beat.pose.empty()) {
        // 射撃の名前なら射撃の段、そうでなければ抜き取った武器の技の段
        poseShot_ = ResolveShot(beat.pose);
        poseAttack_ = poseShot_ == nullptr ? ResolvePose(beat.pose) : nullptr;
    } else if (beat.shot && poseShot_ == nullptr) {
        // 姿勢を決めずに撃つ拍は、射撃の最初の段で撃つ
        const GunComboSet& set = GetGunComboSet(gun_);
        poseShot_ = set.count > 0 ? &set[0] : nullptr;
    } else if (!beat.shot) {
        return;
    }
    poseTimer_ = 0.0f;
    poseSeconds_ = beat.poseSeconds;
    poseFrom_ = 0.0f;
    poseTo_ = 1.0f;
    if (poseShot_ == nullptr) {
        return;
    }
    // 射撃の段は発砲の瞬間で分け、撃つ拍は反動から構え直すまで、撃たない拍は構え切る所までを動かす
    const float shotAt = std::clamp(poseShot_->shotTime / (std::max)(poseShot_->duration, kMinShotSeconds), 0.0f, 1.0f);
    if (beat.shot) {
        poseFrom_ = shotAt;
    } else {
        poseTo_ = (std::max)(shotAt - kAimHoldMargin, 0.0f);
    }
}

/** @brief プレイヤーからボスの体の真ん中へ弾を飛ばす */
void BossExecution::FireShot() {
    const HitEffectPlayer fx(services_.particles, services_.shaker, services_.rng);
    sceneShared::EmitGunShotParticles(fx, playerPos_, BossCenter(), poseShot_, gunColor_);
}

/** @brief 今の姿勢の経過割合 */
float BossExecution::PoseProgress() const {
    // 秒数が 0 の姿勢は動かさず、動かし始めの形のまま見せる
    const float ratio = poseSeconds_ > 0.0f ? std::clamp(poseTimer_ / poseSeconds_, 0.0f, 1.0f) : 0.0f;
    return poseFrom_ + (poseTo_ - poseFrom_) * ratio;
}

/** @brief 拍の効果を出す */
void BossExecution::FireEffects(const ExecutionBeat& beat) {
    const HitEffectPlayer fx(services_.particles, services_.shaker, services_.rng);
    const auto* library = HitEffectLibrary::GetInstance();
    const Vector3 center = BossCenter();
    fx.SparksIfNamed(beat.sparks, center);
    fx.RingIfNamed(beat.ring, center);
    if (!beat.flash.empty() && library->Has("flashes", beat.flash)) {
        fx.Flash(beat.flash);
    }
    if (!beat.impact.empty() && library->Has("impacts", beat.impact)) {
        fx.Impact(beat.impact);
    }
    // 動く拍の火花は動き始め（と一瞬で移った先）に BeginMove が出す
    if (!beat.move) {
        fx.SparksIfNamed(beat.playerSparks, playerPos_);
    }

    if (!beat.sound.empty()) {
        gameSounds::PlayStyled(beat.sound, std::string());
    }
    for (const CueEntry& entry : kCues) {
        if (beat.cue == entry.name) {
            gameSounds::Play(entry.cue);
        }
    }
    auto* time = TimeManager::GetInstance();
    if (beat.hitStop > 0) {
        time->RequestHitStop(beat.hitStop);
    }
    if (beat.slowSeconds > 0.0f) {
        time->RequestSlowMotion(beat.slowSeconds, beat.slowScale);
    }
    if (beat.shake > 0.0f && services_.shaker != nullptr) {
        services_.shaker->Request(beat.shake, beat.shakeSeconds);
    }
}

/** @brief 拍の斬撃線を出す */
void BossExecution::FireSlash(const ExecutionBeat& beat) {
    if (beat.slashLength <= 0.0f) {
        return;
    }
    // 角度はプレイヤーが +X 側にいる時の向きで書くので、-X 側なら左右を反転する
    const float radians = beat.slashAngle * gameConstants::kDegToRad;
    const float angle = side_ > 0.0f ? radians : gameConstants::kPi - radians;
    const Vector3 center = BossCenter();
    const Vector3 slashCenter = { center.x + side_ * beat.slashOffset.x, center.y + beat.slashOffset.y, center.z };
    sceneShared::EmitFinisherSlashLine(services_.particles, "sword_slash", "hit_spark", slashCenter, angle, beat.slashLength);
    if (!beat.screenSlash || services_.camera == nullptr) {
        return;
    }
    // 画面を横切る細い線（斬った跡が一瞬遅れて走る見せ方）
    const auto& tuning = BossExecutionTuningStore::Get();
    const float dx = std::cos(angle) * beat.screenSlashLength;
    const float dy = std::sin(angle) * beat.screenSlashLength;
    const Vector3& cameraPos = services_.camera->GetTranslate();
    sceneShared::SpawnSlashMarkWorld({ slashCenter.x - dx, slashCenter.y - dy }, { slashCenter.x + dx, slashCenter.y + dy },
        cameraPos.x, cameraPos.y, tuning.slashMarkColor, tuning.slashMarkThickness, tuning.slashMarkSeconds);
}

/** @brief 移動を始める */
void BossExecution::BeginMove(const ExecutionBeat& beat) {
    moveFrom_ = playerPos_;
    moveTo_ = ToWorld(beat.moveTo);
    moveTimer_ = 0.0f;
    walking_ = beat.walkSpeed > 0.0f;
    moveSeconds_ = walking_ ? MoveDistance(moveFrom_, moveTo_) / beat.walkSpeed : beat.moveSeconds;
    // 動き始めの位置に火花を出す
    const HitEffectPlayer fx(services_.particles, services_.shaker, services_.rng);
    fx.SparksIfNamed(beat.playerSparks, moveFrom_);
    if (moveSeconds_ <= kMinMoveSeconds) {
        // 一瞬で移る時は、移った先にも火花を出して消えて現れたように見せる
        playerPos_ = moveTo_;
        moving_ = false;
        walking_ = false;
        fx.SparksIfNamed(beat.playerSparks, playerPos_);
        return;
    }
    moving_ = true;
}

/** @brief 移動を進める */
void BossExecution::UpdateMove(float dt) {
    if (!moving_) {
        return;
    }
    moveTimer_ += dt;
    const float t = moveTimer_ / moveSeconds_;
    // 歩く時は一定の速さ、それ以外はなめらかに寄せる
    const float eased = walking_ ? std::clamp(t, 0.0f, 1.0f) : SmoothStep(t);
    playerPos_ = {
        moveFrom_.x + (moveTo_.x - moveFrom_.x) * eased,
        moveFrom_.y + (moveTo_.y - moveFrom_.y) * eased,
        moveFrom_.z + (moveTo_.z - moveFrom_.z) * eased,
    };
    if (t >= 1.0f) {
        playerPos_ = moveTo_;
        moving_ = false;
        walking_ = false;
    }
}

/** @brief プレイヤーの位置・向き・姿勢を反映する */
void BossExecution::ApplyPlayerPose() {
    Player* player = services_.player;
    player->SetPosition(playerPos_);
    // ボスの方か、ボスに背を向ける方へ向かせる
    const float toBoss = bossPos_.x >= playerPos_.x ? 1.0f : -1.0f;
    const float facing = faceAway_ ? -toBoss : toBoss;
    player->FaceTarget({ playerPos_.x + facing, playerPos_.y, playerPos_.z });
    // 射撃の段を取っている間は銃を構えた姿勢
    if (poseShot_ != nullptr) {
        player->PreviewGunPose(poseShot_, PoseProgress());
        gunPosed_ = true;
        return;
    }
    // 射撃の姿勢から離れる時は、構えを解いてから次の姿を取らせる
    if (gunPosed_) {
        player->EndMeleePreview();
        gunPosed_ = false;
    }
    // 武器を抜き取るまでは素の姿（歩く・立つ）、抜き取った後はその武器の技の姿勢
    if (!grabbed_) {
        player->UpdateCinematicPose(false, {}, walking_);
        return;
    }
    const MeleeAttackDef* attack = poseAttack_ != nullptr ? poseAttack_ : StepAt(GetMeleeComboSet(weapon_).ground, 0);
    if (attack == nullptr) {
        player->UpdateCinematicPose(false, {}, walking_);
        return;
    }
    player->PreviewMeleePose(attack, poseAttack_ != nullptr ? PoseProgress() : 0.0f, weapon_);
}

/** @brief カメラの見る位置と引きを目標へ寄せる */
void BossExecution::UpdateCamera() {
    const auto& tuning = BossExecutionTuningStore::Get();
    const float rate = (std::min)(tuning.zoomRate * gameConstants::kFrameDeltaTime, 1.0f);
    zoom_ += (targetZoom_ - zoom_) * rate;
    const Vector3 target = { (playerPos_.x + bossPos_.x) * kHalf, groundY_ + tuning.cameraHeight, bossPos_.z };
    cameraFocus_.x += (target.x - cameraFocus_.x) * rate;
    cameraFocus_.y += (target.y - cameraFocus_.y) * rate;
    cameraFocus_.z = target.z;
}

/** @brief 姿勢の名前から技の段を引く */
const MeleeAttackDef* BossExecution::ResolvePose(const std::string& name) const {
    const MeleeComboSet& set = GetMeleeComboSet(weapon_);
    if (name == kGroundLast) {
        return StepAt(set.ground, set.ground.count - 1);
    }
    if (name == kAirLast) {
        return StepAt(set.air, set.air.count - 1);
    }
    if (name == kLauncher) {
        return set.launcher;
    }
    if (name.size() >= 2 && (name[0] == kGroundPrefix || name[0] == kAirPrefix)) {
        const int index = std::atoi(name.c_str() + 1);
        // 段の数より大きい番号は最後の段にする（武器ごとに段の数が違っても同じ書き方で使えるように）
        const ComboArray<MeleeAttackDef>& steps = name[0] == kGroundPrefix ? set.ground : set.air;
        return StepAt(steps, (std::min)(index, steps.count - 1));
    }
    return nullptr;
}

/** @brief 姿勢の名前から射撃の段を引く */
const GunShotDef* BossExecution::ResolveShot(const std::string& name) const {
    const GunComboSet& set = GetGunComboSet(gun_);
    if (set.count <= 0) {
        return nullptr;
    }
    if (name == kShotLast) {
        return &set[set.count - 1];
    }
    if (name.size() >= 2 && name[0] == kShotPrefix) {
        // 段の数より大きい番号は最後の段にする（銃ごとに段の数が違っても同じ書き方で使えるように）
        const int index = std::atoi(name.c_str() + 1);
        return &set[std::clamp(index, 0, set.count - 1)];
    }
    return nullptr;
}

/** @brief ボスの足元からの位置を世界の位置へ直す */
Vector3 BossExecution::ToWorld(const Vector3& local) const {
    return { bossPos_.x + side_ * local.x, groundY_ + local.y, playerZ_ };
}

/**
 * @brief 拍の動く先が壁に掛かるなら、ボスとプレイヤーをそろって動かす量を返す（正でプレイヤーが最初にいた側へ）
 * @note ボスの向こう側（X が負）へ回り込む先が壁なら手前へ、手前側（X が正）へ動く先が壁なら向こう側へずらす
 */
float BossExecution::RoomAroundBossShift(const std::vector<engine::AABB>& terrain) const {
    // 拍の動く先のうち、一番ボスの向こう側へ行く所と、一番手前側へ行く所
    float behind = 0.0f;
    float front = 0.0f;
    for (const ExecutionBeat& beat : beats_) {
        if (beat.move) {
            behind = (std::max)(behind, -beat.moveTo.x);
            front = (std::max)(front, beat.moveTo.x);
        }
    }
    // 足元の床は壁として数えないよう、体の下端を少し上げて調べる
    const float bodyBottom = groundY_ - kPlayerBodyHalfHeight + kFloorClearance;
    const float bodyTop = groundY_ + kPlayerBodyHalfHeight;
    // ボスから direction の向き（+1 で手前側、-1 で向こう側）へ reach まで動いたプレイヤーの体が、壁にどれだけ食い込むか
    const auto penetration = [&](float direction, float reach) {
        if (reach <= 0.0f) {
            return 0.0f;
        }
        const float sign = side_ * direction;
        const float farX = bossPos_.x + sign * (reach + kPlayerBodyHalfWidth + kWallRoomMargin);
        const float minX = (std::min)(bossPos_.x, farX);
        const float maxX = (std::max)(bossPos_.x, farX);
        float depth = 0.0f;
        for (const engine::AABB& block : terrain) {
            const bool atBodyHeight = block.max.y > bodyBottom && block.min.y < bodyTop && playerZ_ >= block.min.z && playerZ_ <= block.max.z;
            if (!atBodyHeight || block.max.x <= minX || block.min.x >= maxX) {
                continue;
            }
            // 動く向きが右なら壁の左端から、左なら壁の右端からの食い込み
            depth = (std::max)(depth, sign > 0.0f ? farX - block.min.x : block.max.x - farX);
        }
        return depth;
    };
    constexpr float kFrontSide = 1.0f;
    constexpr float kBehindSide = -1.0f;
    return penetration(kBehindSide, behind) - penetration(kFrontSide, front);
}

/** @brief ボスの体の真ん中 */
Vector3 BossExecution::BossCenter() const {
    return { bossPos_.x, bossPos_.y + BossExecutionTuningStore::Get().bossCenterHeight, bossPos_.z };
}

} // namespace engine::game

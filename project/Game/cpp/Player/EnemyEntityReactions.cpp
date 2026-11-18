/**
 * @file EnemyEntityReactions.cpp
 * @brief 武器を持つ敵の、打ち上げ・吹き飛び・被弾硬直と、武器を落とす処理
 */
#include "EnemyEntity.h"
#include "EnemyTuning.h"
#include "GameConstants.h"
#include <algorithm>
#include <cmath>

namespace engine::game {
namespace {
constexpr float kWallStopClearance = 0.001f; ///< 壁際で止めた体と壁のすき間（計算の誤差で触れたままにならないように）
} // namespace

/** @brief 倒れた体を真下の床まで重力で落とす（宙で倒れた敵や、乗っていた足場が消えた体が浮いたまま止まらないように） */
void EnemyEntity::DropCorpseTo(const std::optional<float>& floorY, bool allowFallOut) {
    const std::optional<float> previousFloorY = corpseFloorY_;
    const float restY = pos_.y;
    corpseFallStarted_ = true;
    float targetY = pos_.y;
    if (floorY) {
        targetY = *floorY;
        corpseFloorY_ = floorY;
    } else if (previousFloorY && allowFallOut) {
        // 乗っていた床が無くなり、下にも床が無い体は画面の下まで落とす
        targetY = pos_.y - kCorpseFallOutDistance;
        corpseFloorY_.reset();
    } else {
        return; // 最初から床の無い所で倒れた体と、落としてはいけない体は、その高さに留める
    }
    if (pos_.y <= targetY) {
        // もう床の上にいる。乗っている床が上がった時は、めり込まないよう上がった分だけ一緒に持ち上げる
        if (previousFloorY && targetY > *previousFloorY && pos_.y < targetY) {
            pos_.y = (std::min)(pos_.y + (targetY - *previousFloorY), targetY);
            RefreshVisualTransforms();
        }
        return;
    }
    // 打ち上げと同じ重力の処理で落とし、着地目標だけ床の高さへ差し替える（落ちた武器もここへ落ちる）
    launchOriginY_ = targetY;
    if (!isLaunched_) {
        isLaunched_ = true;
        velY_ = 0.0f;
    }
    // 同じ床に転がっていた武器も、浮いたまま残らないよう一緒に落とし直す
    const bool weaponOnSameFloor = std::abs(droppedWeaponGroundY_ - kDroppedWeaponRestHeight - restY) <= kSameFloorTolerance;
    if (weaponDropped_ && droppedWeaponLanded_ && !weaponPulled_ && weaponOnSameFloor) {
        droppedWeaponGroundY_ = targetY + kDroppedWeaponRestHeight;
        droppedWeaponVelY_ = 0.0f;
        droppedWeaponLanded_ = false;
    }
}
/** @brief 上方向の初速を与えて打ち上げる */
void EnemyEntity::Launch(float velY) {
    // 台本で動かしている間は、位置を台本だけが決める
    if (IsScripted()) {
        return;
    }
    if (!isLaunched_) {
        launchOriginY_ = pos_.y; // 最初の打ち上げ時点の高さを着地目標として記録する（空中コンボ中は上書きしない）
    }
    // 打ち上げは吹き飛びとして扱い、届かない所で止まったら ResolveBlownAway で倒す
    BeginBlownAway();
    isLaunched_ = true;
    velY_ = velY;
    airComboTimer_ = EnemyTuning::GetInstance()->Basic().airComboHold;
}
/** @brief 近接コンボのヒットに対する吹き飛び・打ち上げを与える */
void EnemyEntity::ApplyComboReaction(float knockDirX, float knockY,
    bool switchPull, float playerX, bool finisher) {
    if (defeated_ || IsScripted()) {
        return;
    }
    if (switchPull) {
        const float toPlayer = playerX - pos_.x;
        knockVelX_ = std::clamp(toPlayer * kSwitchPullStrength, -kSwitchPullClamp, kSwitchPullClamp);
        airComboTimer_ = EnemyTuning::GetInstance()->Basic().airComboHold + kSwitchPullAirComboBonus;
    } else {
        // 加算すると段を重ねるほど遠ざかって次の段が空振りするため、毎ヒット上書きして途中の段は前進距離より短く押すだけにする
        knockVelX_ = knockDirX * (finisher ? kFinisherKnockDirXScale : kKnockDirXScale);
        // 締めの大きな吹き飛びは、壁を突き抜けたり届かない所で止まったりしたら倒す
        if (finisher) {
            BeginBlownAway();
        }
    }
    if (knockY > kLaunchThreshold) {
        Launch(knockY);
    }
}
/** @brief 被弾硬直を与える（硬直中は歩かず攻撃も進めず、のけぞった姿勢になる） */
void EnemyEntity::ApplyHitstun(float seconds, bool interruptAttack) {
    if (defeated_) {
        return;
    }
    hitstunTimer_ = (std::max)(hitstunTimer_, seconds);
    if (interruptAttack) {
        ResetAttackState();
        attackTimer_ = (std::max)(attackTimer_, kPostHitstunAttackDelay);
    }
}
/** @brief 大技の締めで斜め上へ大きく打ち飛ばし、回転させながら飛ばす（撃破済みでも飛ばす） */
void EnemyEntity::ApplyHomeRun(float dirX) {
    // 撃破済みでも飛ばす（とどめの一撃で遠くまで打ち飛ばすのが見せ場なので、ApplyComboReactionの撃破ガードは通さない）
    knockVelX_ = dirX * kHomeRunSpeedX;
    Launch(kHomeRunLaunchY);
    homeRunSpinTimer_ = kHomeRunSpinSeconds;
    homeRunSpinAngle_ = 0.0f;
    ResetAttackState();
    DropWeapon();
}
/** @brief 持っている武器をその場に落とす（以後は本体から離れて地面に転がる） */
void EnemyEntity::DropWeapon() {
    // 膝をつくボスは武器を手放さない（倒れた後に地面へ突き立てて見せるため）
    if (weaponDropped_ || kneelOnDefeat_) {
        return;
    }
    weaponDropped_ = true;
    droppedWeaponLanded_ = false;
    droppedWeaponPos_ = { pos_.x + facingSign_ * kWeaponOffsetX, pos_.y + kWeaponOffsetY, pos_.z + kWeaponOffsetZ };
    droppedWeaponVelY_ = kDroppedWeaponPopY;
    droppedWeaponSpin_ = 0.0f;
    // 打ち上げ中なら打ち上げ前の足元、地上ならいまの足元を地面とみなす
    droppedWeaponGroundY_ = (isLaunched_ ? launchOriginY_ : pos_.y) + kDroppedWeaponRestHeight;
}
/** @brief 落とした武器を指定位置へ寄せる（武器回収の吸い込み演出用） */
void EnemyEntity::PullDroppedWeaponToward(const Vector3& target, float rate) {
    if (!weaponDropped_) {
        return;
    }
    droppedWeaponLanded_ = true; // 吸い込み中は落下させない
    weaponPulled_ = true; // 浮かべるのをやめ、吸い寄せる位置をそのまま使う
    droppedWeaponPos_.x += (target.x - droppedWeaponPos_.x) * rate;
    droppedWeaponPos_.y += (target.y - droppedWeaponPos_.y) * rate;
    weaponObject_->SetPosition(droppedWeaponPos_);
    weaponObject_->Update();
}
/** @brief 吹き飛ばされた敵が、壁を突き抜けたり屋根の上などの届かない所へ行ったら倒す */
bool EnemyEntity::ResolveBlownAway(const std::vector<engine::AABB>& solids) {
    if (!blownAway_) {
        return false;
    }
    if (defeated_ || IsScripted()) {
        blownAway_ = false;
        return false;
    }
    const BasicEnemyTuning& tuning = EnemyTuning::GetInstance()->Basic();
    const EnemySeparationTuning& separation = EnemyTuning::GetInstance()->Separation();
    const float bodyBottom = pos_.y + separation.wallBandBottom;
    const float bodyTop = pos_.y + WallBodyHeight(separation.wallBandTop);
    const auto overlapsBodyHeight = [&](const engine::AABB& block) {
        return pos_.z >= block.min.z && pos_.z <= block.max.z && block.max.y > bodyBottom && block.min.y < bodyTop;
    };
    const auto defeatHere = [this]() {
        knockVelX_ = 0.0f;
        blownAway_ = false;
        TakeDamage(hp_);
        return true;
    };

    // 壁の真ん中を越えた敵は、押し出すと壁の向こう側へ出てしまうので、突き抜けたものとして倒す（体は手前に残す）
    const float fromX = previousX_;
    const float toX = pos_.x;
    if (fromX != toX) {
        const float minX = (std::min)(fromX, toX);
        const float maxX = (std::max)(fromX, toX);
        for (const engine::AABB& block : solids) {
            const float centerX = (block.min.x + block.max.x) * 0.5f;
            const bool crossedCenter = minX < centerX && centerX <= maxX;
            if (crossedCenter && overlapsBodyHeight(block)) {
                const float side = toX > fromX ? -1.0f : 1.0f;
                pos_.x = (side < 0.0f ? block.min.x : block.max.x) + side * WallBodyHalfWidth(separation.bodyHalfWidth);
                return defeatHere();
            }
        }
    }

    // 飛んでいる間と滑っている間は待ち、止まってから少し待って地形に合わせ終わった所で調べる
    if (isLaunched_ || std::abs(knockVelX_) > kKnockbackStopSpeed) {
        blownSettleTimer_ = 0.0f;
        return false;
    }
    blownSettleTimer_ += gameConstants::kFrameDeltaTime;
    if (blownSettleTimer_ < tuning.blownAwaySettleSeconds) {
        return false;
    }
    blownAway_ = false;
    // 元の高さよりずっと高い所（屋根の上など）で止まった敵は、プレイヤーが届かないので倒す（飛ぶ種類は元の高さへ戻るので見ない）
    const bool strandedHigh = !archetype_->Hovers() && pos_.y - blownFromY_ > tuning.blownAwayKillRise;
    // 体の中心が地形の中に埋まったまま止まった敵も倒す
    const bool embedded = std::any_of(solids.begin(), solids.end(), [&](const engine::AABB& block) {
        return pos_.x > block.min.x && pos_.x < block.max.x && pos_.z >= block.min.z && pos_.z <= block.max.z
            && pos_.y > block.min.y && pos_.y < block.max.y;
    });
    if (strandedHigh || embedded) {
        return defeatHere();
    }
    return false;
}

/** @brief このフレームの横の動きで新しく壁へ入った・壁をまたいだ敵を、来た側の壁際で止める */
bool EnemyEntity::StopAtWallsEnteredThisFrame(const std::vector<engine::AABB>& solids) {
    if (defeated_ || IsScripted() || previousX_ == pos_.x) {
        return false;
    }
    const EnemySeparationTuning& separation = EnemyTuning::GetInstance()->Separation();
    const float halfWidth = WallBodyHalfWidth(separation.bodyHalfWidth);
    const float bodyBottom = pos_.y + separation.wallBandBottom;
    const float bodyTop = pos_.y + WallBodyHeight(separation.wallBandTop);
    // 1フレームで大きく動いた時に壁を飛び越さないよう、動く前から今までの体の通り道で調べる
    const float sweepMinX = (std::min)(previousX_, pos_.x) - halfWidth;
    const float sweepMaxX = (std::max)(previousX_, pos_.x) + halfWidth;
    const bool movingRight = pos_.x > previousX_;
    std::optional<float> stopX;
    for (const engine::AABB& block : solids) {
        const bool atBodyHeight = pos_.z >= block.min.z && pos_.z <= block.max.z && block.max.y > bodyBottom && block.min.y < bodyTop;
        const bool overlappedBefore = previousX_ + halfWidth > block.min.x && previousX_ - halfWidth < block.max.x;
        const bool sweptInto = sweepMaxX > block.min.x && sweepMinX < block.max.x;
        if (!atBodyHeight || overlappedBefore || !sweptInto) {
            continue;
        }
        // 来た側の壁際。いくつもの壁に掛かったら、一番手前の壁で止める
        const float candidate = movingRight ? block.min.x - halfWidth - kWallStopClearance : block.max.x + halfWidth + kWallStopClearance;
        if (!stopX || (movingRight ? candidate < *stopX : candidate > *stopX)) {
            stopX = candidate;
        }
    }
    if (!stopX) {
        return false;
    }
    pos_.x = *stopX;
    knockVelX_ = 0.0f;
    return true;
}

/** @brief 吹き飛び始めを記録する（もう吹き飛んでいる間は、最初に立っていた高さを残す） */
void EnemyEntity::BeginBlownAway() {
    if (!blownAway_) {
        blownAway_ = true;
        blownFromY_ = isLaunched_ ? launchOriginY_ : pos_.y;
    }
    blownSettleTimer_ = 0.0f;
}

} // namespace engine::game

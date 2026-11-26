/**
 * @file PlayerStatus.cpp
 * @brief プレイヤーの状態（乱舞の終了・移動範囲など）を外から操作・取得する関数の実装
 */
#include "Player.h"
#include "CombatDepth.h"

namespace engine::game {
/** @brief 乱舞フェーズを強制終了する（外部から撃破時などに呼ぶ） */
void Player::EndRampage() {
    if (rampage_->IsJuggling()) {
        rampage_ = &InactiveRampage();
    }
}

/** @brief プレイヤーが動ける左右の範囲を設定する */
void Player::SetHorizontalBounds(float minX, float maxX) {
    minX_ = minX;
    maxX_ = maxX;
}

/** @brief プレイヤーの現在位置から AABB コライダーを生成して返す */
Collider Player::GetCollider() const {
    Collider c;
    engine::AABB body = { { pos_.x - kBodyHalfExtent, pos_.y - kBodyHalfExtent, -kBodyHalfExtent },
        { pos_.x + kBodyHalfExtent, pos_.y + kBodyHalfExtent, kBodyHalfExtent } };
    // 奥行きにも動く型では、今の奥行きの前後だけを体にする
    combatDepth::Apply(body, pos_.z);
    c.SetAsAABB(body);
    return c;
}

/** @brief 回避中に敵の攻撃を紙一重で避けた（ジャスト回避）ことを1回だけ成立させる */
bool Player::ConsumeJustDodge() {
    if (!dodge_.active || dodge_.rewardClaimed) {
        return false;
    }
    dodge_.rewardClaimed = true;
    return true;
}

/** @brief 攻撃が実際に命中した際にシーン側の当たり判定から呼び、覚醒ゲージを加算する */
void Player::ChargeAwakenGauge(float amount) {
    if (!awaken_.isAwakened) {
        awaken_.gauge = (std::min)(awaken_.gauge + amount * skillMods_.gaugeChargeMult, 1.0f);
    }
}

/** @brief このフレームに固有技の攻撃判定が出たか（叩きつけ・スピン連射・鎌の回転・斧の突進のどれか） */
bool Player::JustWeaponSkillHit() const {
    // 固有技の攻撃判定になる出来事（シーン側はどれか1つでも立っていれば周囲へ判定を出す）
    static constexpr FrameEvent kSkillHitEvents[] = {
        FrameEvent::kGreatswordSlam,
        FrameEvent::kSpinShot,
        FrameEvent::kScytheSpin,
        FrameEvent::kAxeCharge,
    };
    for (FrameEvent event : kSkillHitEvents) {
        if (HasFrameEvent(event)) {
            return true;
        }
    }
    return false;
}

} // namespace engine::game

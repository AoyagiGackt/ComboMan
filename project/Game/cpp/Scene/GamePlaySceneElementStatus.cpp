/**
 * @file GamePlaySceneElementStatus.cpp
 * @brief GamePlayScene のうち、武器の属性が敵に残る状態（燃焼・凍結など）、武器を手に入れた瞬間の演出、武器の合体と砕け、名前の表示
 */
#include "GamePlayScene.h"
#include "CombatTuning.h"
#include "CrashBreadcrumbs.h"
#include "ElementEffect.h"
#include "GameSettings.h"
#include "GameConstants.h"
#include "HitEffectLibrary.h"
#include "HudLayer.h"
#include "UILayout.h"
#include "WeaponManager.h"
#include <algorithm>
#include <random>
using namespace engine::graphics;

namespace engine::game {
namespace {
// 武器名の表示（画面の中央上に出し、出てすぐ大きく弾けてから元の大きさへ戻り、最後に薄く消える）
constexpr float kAcquireBannerSeconds = 1.6f; // 出している秒数
constexpr float kAcquireBannerPopSeconds = 0.15f; // 出てから元の大きさへ戻るまでの秒数
constexpr float kAcquireBannerFadeSeconds = 0.45f; // 最後に薄く消えていく秒数
constexpr float kAcquireBannerPopScale = 0.6f; // 出た瞬間に大きさへ足す割合
constexpr Vector2 kAcquireBannerPosition = { gameConstants::kScreenCenterX, 200.0f }; // 文字の中心の位置
constexpr float kAcquireBannerScale = 2.4f; // 文字の大きさ
constexpr float kAcquireBannerWhiteMix = 0.45f; // 武器の色へ白を混ぜる割合（暗い色の武器でも読めるように）
constexpr Vector4 kAcquireBannerShadowColor = { 0.0f, 0.0f, 0.0f, 0.85f }; // 影の色
constexpr float kAcquireBannerShadowOffset = 3.0f; // 影をずらす量

// 合体した武器が砕けた時の演出
constexpr Vector3 kFusionBreakFlashTint = { 1.0f, 0.35f, 0.25f }; // 砕けた時の閃光の色
constexpr Vector4 kFusionBreakBannerColor = { 1.0f, 0.4f, 0.3f, 1.0f }; // 砕けた知らせの文字の色（白を混ぜて出す）
constexpr float kFusionBreakBurstStrength = 1.6f; // 破片と属性の弾けの大きさ

// 合体の主軸を選ぶ画面（画面の中央に見出し・左右の選択肢・操作の案内）
constexpr float kBossChoiceY = 300.0f; // 選択肢の高さ
constexpr float kBossChoiceTitleOffset = 70.0f; // 見出しを選択肢から上げる量
constexpr float kBossChoiceHelpOffset = 70.0f; // 操作の案内を選択肢から下げる量
constexpr float kBossChoiceOptionOffsetX = 200.0f; // 左右の選択肢を中央から離す量
constexpr float kBossChoiceTitleScale = 1.8f; // 見出しの大きさ
constexpr float kBossChoiceOptionScale = 1.5f; // 選択肢の大きさ
constexpr float kBossChoiceHelpScaleRatio = 0.7f; // 操作の案内の大きさ（選択肢に対する割合）
constexpr float kBossChoicePadding = 10.0f; // 文字の後ろの板の余白
constexpr Vector4 kBossChoicePanelColor = { 0.0f, 0.0f, 0.05f, 0.75f }; // 文字の後ろの板
constexpr Vector4 kBossChoiceTitleColor = { 1.0f, 0.85f, 0.3f, 1.0f }; // 見出しの色
constexpr Vector4 kBossChoiceOptionColor = { 0.85f, 0.85f, 0.9f, 1.0f }; // 選んでいない選択肢と案内の色
constexpr Vector4 kBossChoiceSelectedColor = { 1.0f, 1.0f, 0.4f, 1.0f }; // 選んでいる選択肢の色

// 状態の粒を出す間隔の下限（設定が0でも毎フレーム何度も出さないように）
constexpr float kMinStatusEmitInterval = 1.0f / 60.0f;
} // namespace

/** @brief 攻撃していない間に武器へ属性をまとわせるか（覚醒中か、スタイルランクが高い時に確率で） */
bool GamePlayScene::ShouldEmitIdleAura() {
    if (player_->IsAwakened()) {
        return true;
    }
    // ランクが高い時は毎フレームではなく確率で出し、覚醒中より控えめにまとわせる
    const auto& visuals = HitEffectLibrary::GetInstance()->SceneParticles();
    if (styleRankHud_.GetRankIndex() < visuals.idleAuraMinRank) {
        return false;
    }
    std::uniform_real_distribution<float> chanceD(0.0f, 1.0f);
    return chanceD(rng_) < visuals.idleAuraRankChance;
}

/** @brief 当てた敵に属性の状態を付ける。同じ属性なら残り時間を延ばし、違う属性なら上書きする */
void GamePlayScene::ApplyElementStatus(EnemyEntity* enemy, const std::string& element, const Vector4& color) {
    if (enemy == nullptr || enemy->IsDefeated()) {
        return;
    }
    const IElementEffect* effect = IElementEffect::Find(element);
    if (effect == nullptr || effect->Status().duration <= 0.0f) {
        return;
    }
    const ElementStatusDef& def = effect->Status();
    EnemyElementStatus& status = enemy->ElementStatus();
    // 違う属性に替わった時だけ継続ダメージの間合いを数え直す（同じ属性を当て続けても継続ダメージが遅れないように）
    if (status.element != element || !status.IsActive()) {
        status.element = element;
        status.tickTimer = def.tickInterval;
        status.emitTimer = 0.0f;
    }
    status.color = color;
    status.remaining = def.duration;
    enemy->SetStatusSpeedScale(def.speedScale);
}

/** @brief 道中の敵への命中1回ぶんから、何で当てたかに合う属性の状態を付ける（効きが落ちた武器では付けない） */
void GamePlayScene::ApplyWeaponEnemyElementStatus(EnemyEntity& enemy, const WeaponEnemyHit& hit) {
    if (hit.fatigueMult < 1.0f) {
        return;
    }
    // 撃った弾は銃の属性、近接と固有技は手持ちの武器の属性
    const auto* wm = WeaponManager::GetInstance();
    if (hit.fired) {
        const RangedWeaponData& gun = wm->GetRanged();
        ApplyElementStatus(&enemy, gun.element, ColorFromArray(gun.effectColor));
        return;
    }
    if ((hit.melee || hit.skill) && wm->HasEquippedWeapon()) {
        const WeaponData& weapon = wm->GetCurrent();
        ApplyElementStatus(&enemy, weapon.element, ColorFromArray(weapon.effectColor));
    }
}

/** @brief ボスと道中の敵に残っている属性の状態を進める */
void GamePlayScene::UpdateElementStatuses(float dt) {
    if (enemy_ != nullptr) {
        UpdateElementStatus(*enemy_, dt);
    }
    for (WeaponEnemyEntry& entry : weaponEnemies_) {
        if (entry.enemy != nullptr) {
            UpdateElementStatus(*entry.enemy, dt);
        }
    }
}

/** @brief 敵1体の属性の状態を進める（倒れた・見えない敵は状態を消して歩く速さを戻す） */
void GamePlayScene::UpdateElementStatus(EnemyEntity& enemy, float dt) {
    EnemyElementStatus& status = enemy.ElementStatus();
    if (!status.IsActive()) {
        return;
    }
    const IElementEffect* effect = IElementEffect::Find(status.element);
    status.remaining -= dt;
    if (effect == nullptr || status.remaining <= 0.0f || enemy.IsDefeated() || !enemy.IsVisible()) {
        status = EnemyElementStatus {};
        enemy.SetStatusSpeedScale(1.0f);
        return;
    }
    const ElementStatusDef& def = effect->Status();
    // 継続ダメージ（間隔ごとに少しずつ。倒すのは攻撃が当たった時の流れに任せる）
    if (def.tickInterval > 0.0f) {
        status.tickTimer -= dt;
        while (status.tickTimer <= 0.0f) {
            status.tickTimer += def.tickInterval;
            enemy.TakeStatusDamage(def.tickDamage);
        }
    }
    // 体から状態の粒を出す（燃えている・凍っているなど）
    status.emitTimer -= dt;
    if (status.emitTimer <= 0.0f) {
        status.emitTimer = (std::max)(def.emitInterval, kMinStatusEmitInterval);
        effect->EmitStatus(ElementContext(), enemy.GetPosition(), status.color);
    }
}

/** @brief 武器を手に入れた瞬間に、胸の高さへ属性の粒を出す（武器名は出さない） */
void GamePlayScene::PlayWeaponAcquireEffect(WeaponType type) {
    const auto& list = WeaponManager::GetInstance()->GetList();
    const auto found = std::find_if(list.begin(), list.end(), [type](const WeaponData& data) {
        return data.type == type;
    });
    if (found == list.end()) {
        return;
    }
    const WeaponData& weapon = *found;
    const Vector4 color = ColorFromArray(weapon.effectColor);
    const Vector3 playerPos = player_->GetPosition();
    const Vector3 chest = { playerPos.x, playerPos.y + HitEffectLibrary::GetInstance()->SceneParticles().chestHeight, playerPos.z };
    // 吸い込みと同じ輪と閃光を武器の色で出し、その上に属性の粒をばらまく
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Ring("absorb", chest, { .tint = ColorRgb(color) });
    fx.Flash("absorb", { .tint = ColorRgb(color) });
    const IElementEffect* effect = IElementEffect::Find(weapon.element);
    if (effect != nullptr) {
        effect->EmitAcquire(ElementContext(), chest, color);
    }
}

/** @brief 画面の中央上に名前を一瞬出す（武器を合体させた時に使う） */
void GamePlayScene::ShowWeaponBanner(const std::wstring& text, const Vector4& color) {
    acquireBannerText_ = text;
    acquireBannerColor_ = {
        color.x + (1.0f - color.x) * kAcquireBannerWhiteMix,
        color.y + (1.0f - color.y) * kAcquireBannerWhiteMix,
        color.z + (1.0f - color.z) * kAcquireBannerWhiteMix,
        1.0f,
    };
    acquireBannerTimer_ = kAcquireBannerSeconds;
}

/** @brief 枠が満杯の時に奪った武器を、今持っている武器に合体させる */
bool GamePlayScene::TryFuseAcquiredWeapon(WeaponManager::AcquireResult result, WeaponType type) {
    // ポーズで合体を切っている時は、今までどおり今の武器と入れ替える（持っている種類なら覚醒ゲージへ）
    if (!GameSettingsManager::GetInstance()->Get().weaponFusion) {
        return false;
    }
    WeaponManager* wm = WeaponManager::GetInstance();
    const bool needsReplacement = result == WeaponManager::AcquireResult::kNeedsReplacement;
    // 枠が満杯の時だけ合体させる（持っていない種類は交換待ちになった時、もう持っている種類は枠が満杯の時）
    if (!needsReplacement && !(result == WeaponManager::AcquireResult::kDuplicate && wm->AreSlotsFull())) {
        return false;
    }
    // 道中の武器はふつうの枠へ合体させる（ボスの枠を選んでいる時は、合体していないふつうの枠へ）
    const int slot = wm->FindFusionSlot();
    if (slot < 0) {
        return false;
    }
    const int durability = CombatTune().fusionDurability;
    const bool fused = needsReplacement ? wm->FusePendingInto(slot, durability) : wm->FuseInto(slot, type, durability);
    if (fused) {
        PlayWeaponFusionEffect(slot, false);
    }
    return fused;
}

/** @brief ボスから奪った武器をボスの枠へ入れる（空なら入れ、同じなら覚醒ゲージへ、別の種類なら主軸を選ぶ画面を出す） */
void GamePlayScene::AcquireBossWeapon(WeaponType type) {
    WeaponManager* wm = WeaponManager::GetInstance();
    const WeaponManager::BossAcquireResult result = wm->AcquireBossWeapon(type);
    CrashBreadcrumbs::Add("game", "ボスの武器を入手（結果 " + std::to_string(static_cast<int>(result)) + "）");
    if (result == WeaponManager::BossAcquireResult::kAdded) {
        PlayWeaponAcquireEffect(type);
        hud_.Notify(HudEvent::kWeaponAcquired);
        return;
    }
    if (result == WeaponManager::BossAcquireResult::kSame) {
        player_->ChargeAwakenGauge(CombatTune().duplicateWeaponAwakenBonus);
        PlayWeaponAcquireEffect(type);
        return;
    }
    // 合体を切っている時は、今のボスの武器のままにして、新しい武器は覚醒ゲージへ回す
    if (!GameSettingsManager::GetInstance()->Get().weaponFusion) {
        wm->DiscardPendingBossWeapon();
        player_->ChargeAwakenGauge(CombatTune().duplicateWeaponAwakenBonus);
        PlayWeaponAcquireEffect(type);
        return;
    }
    // 主軸を選ぶ画面は、今の主軸のままを選んだ状態から始める
    bossChoiceKeepCurrent_ = true;
}

/** @brief 合体の主軸を選ぶ画面の入力（左右で選び、決定で決める。ゲームは止めておく） */
void GamePlayScene::UpdateBossMainChoice() {
    WeaponManager* wm = WeaponManager::GetInstance();
    if (!wm->HasPendingBossChoice()) {
        return;
    }
    if (input_->TriggerMenuLeft() || input_->TriggerMenuRight()) {
        bossChoiceKeepCurrent_ = !bossChoiceKeepCurrent_;
        audio_->PlayMenuChoice();
    }
    if (!input_->TriggerMenuConfirm()) {
        return;
    }
    audio_->PlayMenuSelect();
    wm->ChooseBossMain(bossChoiceKeepCurrent_);
    PlayWeaponFusionEffect(WeaponManager::kBossSlot, true);
}

/** @brief 合体の主軸を選ぶ画面を描く */
void GamePlayScene::DrawBossMainChoice() {
    const WeaponManager* wm = WeaponManager::GetInstance();
    if (!wm->HasPendingBossChoice()) {
        return;
    }
    const auto& list = wm->GetList();
    const int currentIndex = wm->GetSlotWeaponIndex(WeaponManager::kBossSlot);
    if (currentIndex < 0) {
        return;
    }
    const WeaponType current = list[currentIndex].type;
    const WeaponType incoming = wm->GetPendingBossWeapon().type;
    UILayout& layout = UILayout::Get(kLayoutName);
    const float centerY = layout.Float("boss_choice.y", kBossChoiceY);
    const float titleScale = layout.Float("boss_choice.title_scale", kBossChoiceTitleScale);
    const float optionScale = layout.Float("boss_choice.option_scale", kBossChoiceOptionScale);
    // 見出し・左右の選択肢（主軸＋相棒）・操作の案内を中央にそろえて出す
    const auto drawCentered = [&](const std::wstring& text, float x, float y, float scale, const Vector4& color) {
        const Vector2 size = FontRenderer::MeasureStringW(text, scale);
        fontRenderer_.DrawPanel(x - size.x * kCenterFactor - kBossChoicePadding, y - kBossChoicePadding,
            size.x + kBossChoicePadding * 2.0f, size.y + kBossChoicePadding * 2.0f, kBossChoicePanelColor);
        fontRenderer_.DrawStringW(text, x - size.x * kCenterFactor, y, scale, color);
    };
    drawCentered(L"ボスの武器の合体  どちらを主軸にする？", gameConstants::kScreenCenterX, centerY - kBossChoiceTitleOffset, titleScale,
        kBossChoiceTitleColor);
    const std::wstring keep = std::wstring(WeaponTypeLabel(current)) + L"＋" + WeaponTypeLabel(incoming);
    const std::wstring swap = std::wstring(WeaponTypeLabel(incoming)) + L"＋" + WeaponTypeLabel(current);
    drawCentered((bossChoiceKeepCurrent_ ? L"▶ " : L"   ") + keep, gameConstants::kScreenCenterX - kBossChoiceOptionOffsetX, centerY,
        optionScale, bossChoiceKeepCurrent_ ? kBossChoiceSelectedColor : kBossChoiceOptionColor);
    drawCentered((bossChoiceKeepCurrent_ ? L"   " : L"▶ ") + swap, gameConstants::kScreenCenterX + kBossChoiceOptionOffsetX, centerY,
        optionScale, bossChoiceKeepCurrent_ ? kBossChoiceOptionColor : kBossChoiceSelectedColor);
    drawCentered(std::wstring(input_->IsUsingGamepad() ? L"左右で選んで Aボタンで決める" : L"左右で選んで Enter か Space で決める") + L"（主軸の技で戦い、相棒の属性が乗る）",
        gameConstants::kScreenCenterX, centerY + kBossChoiceHelpOffset, optionScale * kBossChoiceHelpScaleRatio, kBossChoiceOptionColor);
}

/** @brief 武器が合体した瞬間の演出（2本の色の輪・閃光・両方の属性の粒・合体した名前） */
void GamePlayScene::PlayWeaponFusionEffect(int slot, bool bossWeapon) {
    const auto* wm = WeaponManager::GetInstance();
    const int baseIndex = wm->GetSlotWeaponIndex(slot);
    const int partnerIndex = wm->GetFusedWeaponIndex(slot);
    if (baseIndex < 0 || partnerIndex < 0) {
        return;
    }
    const WeaponData& base = wm->GetList()[baseIndex];
    const WeaponData* partner = &wm->GetList()[partnerIndex];
    const Vector4 baseColor = ColorFromArray(base.effectColor);
    const Vector4 partnerColor = ColorFromArray(partner->effectColor);
    const Vector3 playerPos = player_->GetPosition();
    const Vector3 chest = { playerPos.x, playerPos.y + HitEffectLibrary::GetInstance()->SceneParticles().chestHeight, playerPos.z };
    // 2本の色の輪を重ね、相棒の色で画面を光らせて揺らす
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Ring("absorb", chest, { .tint = ColorRgb(baseColor) });
    fx.Ring("weapon_switch", chest, { .tint = ColorRgb(partnerColor) });
    fx.Flash("absorb", { .tint = ColorRgb(partnerColor) });
    fx.Impact("weapon_switch");
    // 両方の属性の粒をまとめて吹き出させる
    for (const WeaponData* weapon : { &base, partner }) {
        if (const IElementEffect* effect = IElementEffect::Find(weapon->element)) {
            effect->EmitAcquire(ElementContext(), chest, ColorFromArray(weapon->effectColor));
        }
    }
    // ボスの武器どうしの合体は見出しを変える
    const std::wstring heading = bossWeapon ? L"ボス武器の合体 " : L"合体 ";
    ShowWeaponBanner(heading + WeaponTypeLabel(base.type) + L"＋" + WeaponTypeLabel(partner->type), partnerColor);
    hud_.Notify(HudEvent::kWeaponAcquired);
}

/** @brief 合体している武器の近接・固有技のダメージ倍率（合体していなければ 1） */
float GamePlayScene::FusionDamageMult() const {
    const WeaponManager* wm = WeaponManager::GetInstance();
    // ボスの枠の武器は合体していなくても少し強く、ボスの武器どうしの合体はさらに強い
    if (wm->IsBossSlotSelected()) {
        return wm->IsCurrentFused() ? CombatTune().bossFusionDamageMult : CombatTune().bossWeaponDamageMult;
    }
    return wm->IsCurrentFused() ? CombatTune().fusionDamageMult : 1.0f;
}

/** @brief 合体している武器で当てた時に、相棒の属性の演出と状態を足し、耐久を減らす */
void GamePlayScene::ApplyFusionHit(EnemyEntity* enemy, float scale) {
    auto* wm = WeaponManager::GetInstance();
    const WeaponData* partner = wm->GetCurrentFusionPartner();
    if (enemy == nullptr || partner == nullptr) {
        return;
    }
    // 相棒の属性も一緒に出し、敵に残る状態は相棒の属性にする（2つ目の属性が乗っていることを見せる）
    const Vector4 partnerColor = ColorFromArray(partner->effectColor);
    EmitElementalHitEffect(partner->element, partnerColor, enemy->GetPosition(), scale);
    ApplyElementStatus(enemy, partner->element, partnerColor);
    // 砕けると枠が変わるので、演出に使う武器は先に写しておく
    const WeaponData base = wm->GetCurrent();
    const WeaponData partnerCopy = *partner;
    if (wm->ConsumeFusionDurability(1)) {
        player_->CancelAttacksForWeaponBreak();
        PlayWeaponFusionBreakEffect(base, partnerCopy);
    }
}

/** @brief 合体した武器が砕けた瞬間の演出 */
void GamePlayScene::PlayWeaponFusionBreakEffect(const WeaponData& weapon, const WeaponData& partner) {
    const Vector3 playerPos = player_->GetPosition();
    const Vector3 chest = { playerPos.x, playerPos.y + HitEffectLibrary::GetInstance()->SceneParticles().chestHeight, playerPos.z };
    const Vector4 color = ColorFromArray(weapon.effectColor);
    // 武器の色の破片を散らし、相棒の属性を最後に大きく弾けさせる
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Ring("defeat", chest, { .tint = ColorRgb(color) });
    fx.Flash("absorb", { .tint = kFusionBreakFlashTint });
    fx.Impact("weapon_switch");
    EmitEnemyHitEffect(chest, color, kFusionBreakBurstStrength, weapon.effectBurstCount, weapon.effectRingRadius);
    if (const IElementEffect* effect = IElementEffect::Find(partner.element)) {
        effect->EmitHit(ElementContext(), chest, ColorFromArray(partner.effectColor), kFusionBreakBurstStrength);
    }
    ShowWeaponBanner(std::wstring(WeaponTypeLabel(weapon.type)) + L"が砕けた", kFusionBreakBannerColor);
    hud_.Notify(HudEvent::kWeaponAcquired);
}

/** @brief 武器名の表示の残り時間を進める */
void GamePlayScene::UpdateWeaponAcquireBanner(float dt) {
    acquireBannerTimer_ = (std::max)(acquireBannerTimer_ - dt, 0.0f);
}

/** @brief 手に入れた武器の名前を画面の中央上に出す（出てすぐ大きく弾け、最後に薄く消える） */
void GamePlayScene::DrawWeaponAcquireBanner() {
    if (acquireBannerTimer_ <= 0.0f || acquireBannerText_.empty()) {
        return;
    }
    UILayout& layout = UILayout::Get(kLayoutName);
    const Vector2 center = layout.Pos("acquire_banner.pos", kAcquireBannerPosition);
    const float baseScale = layout.Float("acquire_banner.scale", kAcquireBannerScale);
    // 出た直後ほど大きく、時間とともに元の大きさへ戻す
    const float elapsed = kAcquireBannerSeconds - acquireBannerTimer_;
    const float pop = 1.0f - std::clamp(elapsed / kAcquireBannerPopSeconds, 0.0f, 1.0f);
    const float scale = baseScale * (1.0f + kAcquireBannerPopScale * pop);
    // 最後の時間で薄く消す
    const float alpha = std::clamp(acquireBannerTimer_ / kAcquireBannerFadeSeconds, 0.0f, 1.0f);
    Vector4 color = acquireBannerColor_;
    color.w *= alpha;
    Vector4 shadow = kAcquireBannerShadowColor;
    shadow.w *= alpha;
    const Vector2 size = FontRenderer::MeasureStringW(acquireBannerText_, scale);
    const float x = center.x - size.x * kCenterFactor;
    const float y = center.y - size.y * kCenterFactor;
    fontRenderer_.DrawStringW(acquireBannerText_, x + kAcquireBannerShadowOffset, y + kAcquireBannerShadowOffset, scale, shadow);
    fontRenderer_.DrawStringW(acquireBannerText_, x, y, scale, color);
}

} // namespace engine::game

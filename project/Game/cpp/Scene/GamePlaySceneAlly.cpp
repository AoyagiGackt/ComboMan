/**
 * @file GamePlaySceneAlly.cpp
 * @brief GamePlayScene のうち、味方のボスの呼び出し・2人目の参加と抜け・味方と敵の当たり・味方の表示
 */
#include "GamePlayScene.h"
#include "CombatTuning.h"
#include "CrashBreadcrumbs.h"
#include "ElementEffect.h"
#include "FinalBossBattle.h"
#include "GameConstants.h"
#include "GameRules.h"
#include "HitEffectLibrary.h"
#include "SceneShared.h"
#include "StageEditor.h"
#include "UILayout.h"
#include "WeaponManager.h"
#include <algorithm>
#include <cmath>
#include <iterator>
using namespace engine::graphics;

namespace engine::game {
namespace {
// 2P の参加と抜けるのは、ふだん使わない左右のスティックの押し込みの同時押し
constexpr WORD kJoinButtonA = XINPUT_GAMEPAD_LEFT_THUMB;
constexpr WORD kJoinButtonB = XINPUT_GAMEPAD_RIGHT_THUMB;
constexpr WORD kSummonButton = XINPUT_GAMEPAD_BACK; // 1P のパッドの呼び出しと引っ込め
constexpr WORD kPadJump = XINPUT_GAMEPAD_A;
constexpr WORD kPadAttack = XINPUT_GAMEPAD_X;
constexpr WORD kPadDodge = XINPUT_GAMEPAD_B;
constexpr int kFirstPad = 0; // 1台目のパッドの番号
constexpr int kSecondPad = 1; // 2台目のパッドの番号
constexpr float kStickDeadZone = 0.3f; // これより小さいスティックの傾きは入力なし
constexpr float kAllyHitStrength = 0.7f; // 味方の攻撃の命中演出の大きさ
constexpr float kAllyElementScale = 0.6f; // 味方の攻撃に乗せる相棒の属性の演出の大きさ
constexpr float kSummonSide = 1.5f; // 呼び出した時にプレイヤーの後ろへ置く距離

// 表示（味方の頭上の体力バーと 2P の印、2P の操作の案内）
constexpr Vector2 kAllyHpBarSize = { 70.0f, 8.0f };
constexpr float kAllyHpBarRaise = 90.0f; // 足元から体力バーまでの高さ（画面の px）
constexpr float kAllyHpBarGreenScale = 0.5f;
constexpr float kAllyHpBarGreenBase = 0.5f;
constexpr float kAllyHpBarAlpha = 0.9f;
constexpr Vector4 kAllyHpBarBackColor = { 0.05f, 0.15f, 0.25f, 0.85f };
constexpr float kTwoPlayerMarkRaise = 118.0f; // 足元から頭上の印までの高さ（画面の px）
constexpr float kTwoPlayerMarkScale = 1.4f;
constexpr Vector4 kAllyMarkColor = { 0.45f, 0.9f, 1.0f, 1.0f }; // 分身の頭上の印の色（体の色に合わせる）
constexpr Vector4 kAllyMessageColor = { 1.0f, 0.75f, 0.45f, 1.0f }; // 参加・抜け・呼べない理由の知らせの色
} // namespace

/** @brief 2P の機器の画面に出す名前 */
const wchar_t* GamePlayScene::AllyDeviceName(AllyDevice device) {
    // 機器ごとの名前の表（並びは AllyDevice と同じ）
    static constexpr const wchar_t* kNames[] = { L"", L"コントローラー", L"コントローラー2", L"キーボード" };
    const size_t index = static_cast<size_t>(device);
    return index < std::size(kNames) ? kNames[index] : L"";
}

/** @brief 分身を呼べるか（ボスの武器を持っていて、倒れた後の待ちが明けている）。呼べない時は理由を返す */
std::wstring GamePlayScene::AllyUnavailableReason() const {
    const WeaponManager* wm = WeaponManager::GetInstance();
    if (!wm->HasBossWeapon()) {
        return L"ボスの武器を手に入れると、分身が出てくる";
    }
    if (ally_.RespawnRemaining() > 0.0f) {
        return L"分身はまだ休んでいる（あと " + std::to_wstring(static_cast<int>(std::ceil(ally_.RespawnRemaining()))) + L" 秒）";
    }
    return std::wstring();
}

/** @brief 味方のボスを呼び出す（呼べない時は理由を出して false） */
bool GamePlayScene::SummonAlly() {
    const std::wstring reason = AllyUnavailableReason();
    if (!reason.empty()) {
        ShowWeaponBanner(reason, kAllyMessageColor);
        return false;
    }
    const WeaponManager* wm = WeaponManager::GetInstance();
    const WeaponData& main = wm->GetList()[wm->GetSlotWeaponIndex(WeaponManager::kBossSlot)];
    const Vector3 playerPos = player_->GetPosition();
    const Vector3 position = { playerPos.x - player_->GetLastDirX() * kSummonSide, playerPos.y, playerPos.z };
    // ボスの武器どうしを合体させていれば、体力とダメージが上がる
    ally_.Summon(main.type, AllyTune().color, position, wm->GetFusedWeaponIndex(WeaponManager::kBossSlot) >= 0);
    // 呼び出した所に吸い込みと同じ輪を主軸の武器の色で出す
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Ring("absorb", position, { .tint = ColorRgb(ColorFromArray(main.effectColor)) });
    CrashBreadcrumbs::Add("game", "味方のボスを呼んだ");
    return true;
}

/** @brief 2P を参加させる（味方がいなければ呼ぶ。呼べなければ参加しない） */
void GamePlayScene::JoinTwoPlayer(AllyDevice device) {
    if (finalBoss_.IsActive()) {
        // 闘技場では味方を呼ばずに援護だけするので、援護に使うボスの武器がそろっているかだけを見る
        const WeaponData* main = nullptr;
        const WeaponData* partner = nullptr;
        if (!ArenaAssistWeapons(main, partner)) {
            ShowWeaponBanner(L"ボスの武器を持っていないので、援護できない", kAllyMessageColor);
            return;
        }
    } else if (!ally_.IsActive() && !SummonAlly()) {
        return;
    }
    allyDevice_ = device;
    allyIdleSeconds_ = 0.0f;
    // 2P の機器は 1P の操作から外す
    input_->SetPrimaryGamepadEnabled(device != AllyDevice::kFirstPad);
    input_->SetPrimaryKeyboardEnabled(device != AllyDevice::kKeyboard);
    const std::wstring firstPlayer = device == AllyDevice::kKeyboard ? L"コントローラー" : L"キーボード";
    ShowWeaponBanner(std::wstring(L"2P が参加しました（") + AllyDeviceName(device) + L"）  1P: " + firstPlayer, kAllyMessageColor);
    CrashBreadcrumbs::Add("game", "2P が参加した");
}

/** @brief 2P を抜けさせる（味方のボスは残って自動で戦う。機器は 1P の操作へ戻す） */
void GamePlayScene::LeaveTwoPlayer(const wchar_t* message) {
    if (allyDevice_ == AllyDevice::kNone) {
        return;
    }
    allyDevice_ = AllyDevice::kNone;
    input_->SetPrimaryGamepadEnabled(true);
    input_->SetPrimaryKeyboardEnabled(true);
    if (message != nullptr) {
        ShowWeaponBanner(message, kAllyMessageColor);
    }
}

/** @brief 指定した機器で、参加と抜けるの同時押しをしたか */
bool GamePlayScene::PressedJoinCombo(AllyDevice device) const {
    if (device == AllyDevice::kKeyboard) {
        const BYTE keyA = Input::KeyFromName(AllyTune().keyJoinA);
        const BYTE keyB = Input::KeyFromName(AllyTune().keyJoinB);
        return input_->PushKey(keyA) && input_->PushKey(keyB) && (input_->TriggerKey(keyA) || input_->TriggerKey(keyB));
    }
    const int pad = device == AllyDevice::kFirstPad ? kFirstPad : kSecondPad;
    return input_->PadPushed(pad, kJoinButtonA) && input_->PadPushed(pad, kJoinButtonB)
        && (input_->PadTriggered(pad, kJoinButtonA) || input_->PadTriggered(pad, kJoinButtonB));
}

/** @brief 2P の機器から、このフレームの操作を読む */
AllyControl GamePlayScene::ReadAllyControl() const {
    AllyControl control;
    if (allyDevice_ == AllyDevice::kKeyboard) {
        const AllyBossTuning& tune = AllyTune();
        const bool left = input_->PushKey(Input::KeyFromName(tune.keyLeft));
        const bool right = input_->PushKey(Input::KeyFromName(tune.keyRight));
        control.moveX = (right ? 1.0f : 0.0f) - (left ? 1.0f : 0.0f);
        control.jump = input_->TriggerKey(Input::KeyFromName(tune.keyJump));
        control.attack = input_->TriggerKey(Input::KeyFromName(tune.keyAttack));
        control.dodge = input_->TriggerKey(Input::KeyFromName(tune.keyDodge));
    } else {
        const int pad = allyDevice_ == AllyDevice::kFirstPad ? kFirstPad : kSecondPad;
        const Input::Stick stick = input_->PadLeftStick(pad);
        float moveX = std::abs(stick.x) >= kStickDeadZone ? stick.x : 0.0f;
        if (input_->PadPushed(pad, XINPUT_GAMEPAD_DPAD_LEFT)) {
            moveX = -1.0f;
        }
        if (input_->PadPushed(pad, XINPUT_GAMEPAD_DPAD_RIGHT)) {
            moveX = 1.0f;
        }
        control.moveX = moveX;
        control.jump = input_->PadTriggered(pad, kPadJump);
        control.attack = input_->PadTriggered(pad, kPadAttack);
        control.dodge = input_->PadTriggered(pad, kPadDodge);
    }
    control.any = control.moveX != 0.0f || control.jump || control.attack || control.dodge;
    return control;
}

/** @brief 呼び出し・参加・抜けるの操作を受け付ける */
void GamePlayScene::HandleAllyInput() {
    // 1P の呼び出しと引っ込め（2P が操作している間は引っ込めない）
    const bool summonPressed = input_->TriggerKey(Input::KeyFromName(AllyTune().keySummon)) || input_->TriggerButton(kSummonButton);
    if (summonPressed && allyDevice_ == AllyDevice::kNone && !finalBoss_.IsActive()) {
        // 引っ込めたら、もう一度押すまでは自動で出さない
        if (ally_.IsActive()) {
            ally_.Dismiss();
            allyPutAway_ = true;
        } else {
            allyPutAway_ = false;
            SummonAlly();
        }
    }
    // 2P が参加している時は、同じ機器の同時押しで抜ける
    if (allyDevice_ != AllyDevice::kNone) {
        if (PressedJoinCombo(allyDevice_)) {
            LeaveTwoPlayer(L"2P が抜けました。分身は自動で戦います");
        }
        return;
    }
    // 参加は、2台目のパッドか、1P が使っていない方の機器（1P がキーボードなら1台目のパッド、パッドならキーボード）
    if (PressedJoinCombo(AllyDevice::kSecondPad)) {
        JoinTwoPlayer(AllyDevice::kSecondPad);
    } else if (!input_->IsUsingGamepad() && !input_->IsPadConnected(kSecondPad) && PressedJoinCombo(AllyDevice::kFirstPad)) {
        JoinTwoPlayer(AllyDevice::kFirstPad);
    } else if (input_->IsUsingGamepad() && PressedJoinCombo(AllyDevice::kKeyboard)) {
        JoinTwoPlayer(AllyDevice::kKeyboard);
    }
}

/** @brief 味方のボスを1フレーム進める（呼び出し・参加の操作、動き、味方と敵の当たり） */
void GamePlayScene::UpdateAlly() {
    // 分身を使わない設定の時は、呼び出しも 2人目の参加もラスボス戦の援護もしない
    if (!AllyTune().enabled) {
        if (ally_.IsActive() || allyDevice_ != AllyDevice::kNone) {
            ResetAlly();
        }
        return;
    }
    ally_.TickRespawn(gameConstants::kFrameDeltaTime);
    // とどめ・クリア・やられの演出の間は隠して止める
    const bool hold = bossExecution_.IsActive() || clearTriggered_ || playerDefeat_.IsActive();
    // 闘技場では味方は出さず、援護だけにする（回るカメラで 2P が歩いて戦うのは難しいため）
    ally_.SetHidden(hold || finalBoss_.IsActive());
    if (hold) {
        return;
    }
    if (finalBoss_.IsActive()) {
        UpdateArenaAssist();
        return;
    }
    HandleAllyInput();
    AutoSummonAlly();
    if (!ally_.IsActive()) {
        LeaveTwoPlayer(nullptr);
        return;
    }
    // 周りの様子を集める（生きていて見えている敵と、敵の体を除いた地形）
    AllySurroundings around;
    around.playerPosition = player_->GetPosition();
    const std::vector<AABB> terrain = GetStageEditor().GetTerrainColliders();
    around.terrain = &terrain;
    around.cameraX = camera_->GetTranslate().x;
    around.cameraHalfWidth = gameConstants::kCameraHalfW;
    if (enemy_ != nullptr && !enemy_->IsDefeated() && enemy_->IsVisible()) {
        around.enemies.push_back(enemy_);
    }
    for (WeaponEnemyEntry& entry : weaponEnemies_) {
        if (entry.enemy != nullptr && !entry.enemy->IsDefeated() && entry.enemy->IsVisible()) {
            around.enemies.push_back(entry.enemy);
        }
    }
    // 2P の操作（しばらく何もしなければ自動に戻す）
    AllyControl control;
    const AllyControl* controlPtr = nullptr;
    if (allyDevice_ != AllyDevice::kNone) {
        control = ReadAllyControl();
        allyIdleSeconds_ = control.any ? 0.0f : allyIdleSeconds_ + gameConstants::kFrameDeltaTime;
        if (allyIdleSeconds_ >= AllyTune().idleSeconds) {
            LeaveTwoPlayer(L"2P の操作が無いので、分身は自動で戦います");
        } else {
            controlPtr = &control;
        }
    }
    if (ally_.Update(around, controlPtr)) {
        ApplyAllyHits(around);
    }
    ApplyEnemyAttacksToAlly(around);
}

/** @brief ラスボス戦の援護（2P の攻撃の合図か、2P がいなければ自動で時々、分身の横に現れて一撃入れて消える） */
void GamePlayScene::UpdateArenaAssist() {
    const AllyBossTuning& tune = AllyTune();
    ally_.TickAssistCooldown(gameConstants::kFrameDeltaTime);
    HandleAllyInput();
    FinalBossBattle::SliceSource shadow;
    const bool hasShadow = finalBoss_.GetSliceSource(shadow);
    // 斬る瞬間に、ボスの体力を減らして命中の演出を出す（体力は1より下げない）
    const WeaponData* main = nullptr;
    const WeaponData* partner = nullptr;
    const bool hasWeapons = ArenaAssistWeapons(main, partner);
    if (ally_.IsAssisting()) {
        if (ally_.UpdateArenaAssist() && hasWeapons && enemy_ != nullptr && !enemy_->IsDefeated()) {
            // 合体させていれば強く、素材の属性の演出も乗せる
            enemy_->TakeStatusDamage(tune.arenaAssistDamage * (partner != nullptr ? tune.fusedDamageMult : 1.0f));
            const Vector3 hitPos = hasShadow ? shadow.position : enemy_->GetPosition();
            EmitEnemyHitEffect(hitPos, ColorFromArray(main->effectColor), kAllyHitStrength, 0, 0.0f);
            if (partner != nullptr) {
                EmitElementalHitEffect(partner->element, ColorFromArray(partner->effectColor), hitPos, 1.0f);
            }
        }
        return;
    }
    // 援護できない間（ボスの武器どうしの合体が無い・倒れた後の待ち・演出で操作が止まっている）は何もしない
    if (!hasWeapons || ally_.RespawnRemaining() > 0.0f || finalBoss_.BlocksGameplay() || !hasShadow || enemy_ == nullptr
        || enemy_->IsDefeated()) {
        return;
    }
    // 2P は攻撃の合図で、2P がいなければ自動の間隔で援護する（どちらも待ちが明けている時だけ）
    bool request = false;
    if (allyDevice_ != AllyDevice::kNone) {
        const AllyControl control = ReadAllyControl();
        allyIdleSeconds_ = control.any ? 0.0f : allyIdleSeconds_ + gameConstants::kFrameDeltaTime;
        if (allyIdleSeconds_ >= tune.idleSeconds) {
            LeaveTwoPlayer(L"2P の操作が無いので、分身は自動で援護します");
        }
        request = control.attack && ally_.TickAssistCooldown(0.0f) <= 0.0f;
    } else if (ally_.TickAssistCooldown(0.0f) <= 0.0f) {
        request = true;
    }
    if (!request) {
        return;
    }
    // 分身からプレイヤーの側へ少し離れた所に現れて、分身の方を向いて斬る
    const Vector3 playerPos = player_->GetPosition();
    const float side = playerPos.x >= shadow.position.x ? 1.0f : -1.0f;
    const Vector3 appear = { shadow.position.x + side * tune.arenaAssistSide, shadow.position.y, shadow.position.z };
    ally_.StartArenaAssist(main->type, AllyTune().color, appear, -side);
    // 2P がいない時の自動の援護は、2P の合図より長い間隔にする
    if (allyDevice_ == AllyDevice::kNone) {
        ally_.SetAssistCooldown(tune.arenaAutoAssistInterval);
    }
    const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
    fx.Ring("absorb", appear, { .tint = ColorRgb(ColorFromArray(main->effectColor)) });
}

/** @brief ラスボス戦で援護に使うボスの武器を返す（刀だけに差し替える前のボスの枠。主軸があれば true。合体していなければ素材は nullptr） */
bool GamePlayScene::ArenaAssistWeapons(const WeaponData*& main, const WeaponData*& partner) const {
    int mainIndex = -1;
    int partnerIndex = -1;
    finalBoss_.GetSavedBossWeapons(mainIndex, partnerIndex);
    const auto& list = WeaponManager::GetInstance()->GetList();
    const auto inRange = [&list](int index) {
        return index >= 0 && index < static_cast<int>(list.size());
    };
    if (!inRange(mainIndex)) {
        return false;
    }
    main = &list[static_cast<size_t>(mainIndex)];
    partner = inRange(partnerIndex) ? &list[static_cast<size_t>(partnerIndex)] : nullptr;
    return true;
}

/** @brief 味方の攻撃が当たった敵にダメージ（体力は1より下げない）・のけぞり・命中演出を出す */
void GamePlayScene::ApplyAllyHits(const AllySurroundings& around) {
    const WeaponManager* wm = WeaponManager::GetInstance();
    const WeaponData& main = wm->GetList()[static_cast<size_t>((std::max)(wm->GetSlotWeaponIndex(WeaponManager::kBossSlot), 0))];
    const int partnerIndex = wm->GetFusedWeaponIndex(WeaponManager::kBossSlot);
    const Vector4 color = ColorFromArray(main.effectColor);
    for (EnemyEntity* enemy : ally_.CollectHitTargets(around)) {
        // とどめはプレイヤーが刺すので、味方の攻撃では敵の体力を1より下げない
        enemy->TakeStatusDamage(AllyTune().damage * (ally_.IsFused() ? AllyTune().fusedDamageMult : 1.0f));
        if (enemy != enemy_) {
            enemy->ApplyHitstun(AllyTune().hitstunSeconds, true);
        }
        EmitEnemyHitEffect(enemy->GetPosition(), color, kAllyHitStrength, 0, 0.0f);
        // 相棒の属性も小さく乗せる
        if (partnerIndex >= 0) {
            const WeaponData& partner = wm->GetList()[static_cast<size_t>(partnerIndex)];
            EmitElementalHitEffect(partner.element, ColorFromArray(partner.effectColor), enemy->GetPosition(), kAllyElementScale);
        }
    }
}

/** @brief 敵の攻撃と弾を味方に当てる（プレイヤーの体力は減らさない。倒れたら 2P も抜ける） */
void GamePlayScene::ApplyEnemyAttacksToAlly(const AllySurroundings& around) {
    const AllyBossTuning& tune = AllyTune();
    const Vector3& allyPos = ally_.GetPosition();
    bool fell = false;
    // 攻撃を出した瞬間の敵が届く距離にいれば当てる
    for (EnemyEntity* enemy : around.enemies) {
        const Vector3 position = enemy->GetPosition();
        if (enemy->JustFiredAttack() && std::abs(position.x - allyPos.x) <= tune.enemyHitReach
            && std::abs(position.y - allyPos.y) <= tune.enemyHitHeight) {
            fell = ally_.TakeDamage(enemy->GetAttackDamage()) || fell;
        }
    }
    // 敵の弾は当たったら消す
    for (auto bullet = enemyBullets_.begin(); bullet != enemyBullets_.end();) {
        if (ally_.IsActive() && ally_.Overlaps(bullet->pos, tune.bulletHitRadius)) {
            fell = ally_.TakeDamage(bullet->damage) || fell;
            bullet = enemyBullets_.erase(bullet);
        } else {
            ++bullet;
        }
    }
    if (fell) {
        const HitEffectPlayer fx(pm_, &cameraShaker_, &rng_);
        fx.Ring("defeat", allyPos);
        LeaveTwoPlayer(nullptr);
        ShowWeaponBanner(L"分身が倒れた", kAllyMessageColor);
    }
}

/** @brief 味方のボスの体を描く */
void GamePlayScene::DrawAlly() {
    if (!ally_.IsActive() && !ally_.IsAssisting()) {
        return;
    }
    SetupModelRenderState();
    ally_.Draw();
}

/** @brief 味方の頭上の体力バーの位置を決める（毎フレーム、描く前に呼ぶ） */
void GamePlayScene::UpdateAllyHud() {
    allyHpBarPlaced_ = false;
    if (!ally_.IsActive() || allyHpBarBg_ == nullptr || allyHpBarFg_ == nullptr) {
        return;
    }
    const sceneShared::HpBarStyle style { kAllyHpBarSize, kAllyHpBarRaise, kAllyHpBarGreenScale, kAllyHpBarGreenBase, kAllyHpBarAlpha };
    sceneShared::UpdateWorldHpBar(allyHpBarBg_.get(), allyHpBarFg_.get(), ally_.GetPosition(), camera_->GetTranslate(), ally_.HpRatio(), style);
    allyHpBarPlaced_ = true;
}

/** @brief 味方の頭上の体力バーを描く */
void GamePlayScene::DrawAllyHpBar() {
    if (!allyHpBarPlaced_ || !ally_.IsActive() || finalBoss_.IsActive()) {
        return;
    }
    allyHpBarBg_->Draw();
    allyHpBarFg_->Draw();
}

/** @brief 分身の頭上に、プレイヤーと見分ける印を出す */
void GamePlayScene::DrawAllyMark() {
    if (!ally_.IsActive() || finalBoss_.IsActive() || bossExecution_.IsActive() || clearTriggered_) {
        return;
    }
    const Vector3& cam = camera_->GetTranslate();
    const Vector3& position = ally_.GetPosition();
    float sx = 0.0f;
    float sy = 0.0f;
    sceneShared::WorldToScreen(position.x, position.y, cam.x, cam.y, sx, sy);
    const std::wstring mark = L"分身";
    UILayout& layout = UILayout::Get(kLayoutName);
    const float markScale = layout.Float("two_player.mark_scale", kTwoPlayerMarkScale);
    const Vector2 markSize = FontRenderer::MeasureStringW(mark, markScale);
    fontRenderer_.DrawStringW(mark, sx - markSize.x * kCenterFactor, sy - kTwoPlayerMarkRaise, markScale, kAllyMarkColor);
}

/** @brief 味方の体力バーを作る（シーンの初期化で1回。ゲーム中に作ったり消したりしない） */
void GamePlayScene::InitializeAlly() {
    ally_.Initialize(modelCommon_.get());
    sceneShared::CreateHpBarSprites(spriteCommon_.get(), allyHpBarBg_, allyHpBarFg_, kAllyHpBarBackColor);
    allyHpBarBg_->Update();
    allyHpBarFg_->Update();
}

/** @brief ステージを読み直した時に、味方を引っ込めて 2P を抜けさせる */
void GamePlayScene::ResetAlly() {
    ally_.Dismiss();
    allyPutAway_ = false;
    // ステージに入った時にボスの武器を持っていれば、このステージでは分身を自動で出す
    // （ステージの途中、ボスのとどめで手に入れた時は出さない。結果画面の前に出てきてしまうため）
    allyAutoSummon_ = WeaponManager::GetInstance()->HasBossWeapon();
    LeaveTwoPlayer(nullptr);
}

/** @brief ボスの武器を持ってステージに入っていれば分身を自動で出し、主軸の武器か合体が変わったら出し直す */
void GamePlayScene::AutoSummonAlly() {
    const WeaponManager* wm = WeaponManager::GetInstance();
    if (!allyAutoSummon_ || !wm->HasBossWeapon() || allyPutAway_) {
        return;
    }
    const int mainIndex = wm->GetSlotWeaponIndex(WeaponManager::kBossSlot);
    if (mainIndex < 0) {
        return;
    }
    const WeaponType mainType = wm->GetList()[static_cast<size_t>(mainIndex)].type;
    const bool fused = wm->GetFusedWeaponIndex(WeaponManager::kBossSlot) >= 0;
    // 出ていれば、主軸の武器か合体が変わった時だけ、新しい武器と強さで出し直す
    if (ally_.IsActive()) {
        if (ally_.GetWeapon() == mainType && ally_.IsFused() == fused) {
            return;
        }
        ally_.Dismiss();
    }
    // 倒れた後の休みが明けていなければ待つ（案内は出さない）
    if (AllyUnavailableReason().empty()) {
        SummonAlly();
    }
}

} // namespace engine::game

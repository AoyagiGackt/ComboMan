/**
 * @file GamePlaySceneExecution.cpp
 * @brief GamePlayScene のうち、膝をついたボスへのとどめの演出（BossExecution）と、ボスの武器を手に入れる所
 * @note 分割ファイル。クラスは GamePlayScene のまま、とどめまわりの定義だけをここに置く
 */
#include "GamePlayScene.h"
#include "BossExecutionTuning.h"
#include "FinisherEffects.h"
#include "GameRules.h"
#include "RunData.h"
#include "SlashMark.h"
#include "StageEditor.h"
#include "TimeManager.h"
#include "WeaponManager.h"

namespace engine::game {
namespace {
constexpr Vector3 kSliceScale = { 1.0f, 1.0f, 1.0f }; ///< ボスを切り分ける時の破片の倍率
} // namespace

/** @brief 膝をついたボスへのとどめの演出を始める（始められない時は false） */
bool GamePlayScene::TryStartBossExecution() {
    // 膝をついて見えているステージのボスだけ（ラスボス戦は専用の演出がある）
    if (enemy_ == nullptr || !enemy_->IsKneeling() || !enemy_->IsVisible() || IsFinalBossMode()) {
        return false;
    }
    const ExecutionStyle* style = BossExecutionTuningStore::Get().FindStyle(GetEditorLevelPath());
    if (style == nullptr) {
        return false;
    }
    BossExecution::Setup setup;
    setup.style = style;
    setup.weapon = bossWeaponType_;
    setup.bossPosition = enemy_->GetPosition();
    BossExecution::Services services;
    services.player = player_.get();
    services.particles = pm_;
    services.shaker = &cameraShaker_;
    services.rng = &rng_;
    services.camera = camera_.get();
    // ロックオンでカメラが片寄らないように外してから映す
    lockedEnemy_ = nullptr;
    // 銃で倒していても、とどめは抜き取った武器で斬るので、クリアの決め演出は斬撃の方にする
    bossFinishedByGun_ = false;
    bossExecution_.Start(setup, services);
    GetStageEditor().SetGameplayHeld(true);
    return true;
}

/** @brief とどめの演出の間の1フレーム */
void GamePlayScene::UpdateBossExecution() {
    // HUDは出さない（文字コマンドの破棄はふだんDrawStyleUI()が行うのでここで代わりに行う）
    fontRenderer_.Reset();
    GetStageEditor().SetGameplayHeld(true);
    const float dt = TimeManager::GetInstance()->GetDeltaTime();
    bossExecution_.Update(dt);
    if (enemy_ != nullptr) {
        // 武器を抜き取ったら、ボスが突き立てていた武器を消す
        if (bossExecution_.ConsumeGrab()) {
            enemy_->SetWeaponHidden(true);
        }
        // 切り分ける拍で、ボスを破片に差し替えて刃を一斉に放つ
        if (bossExecution_.ConsumeSlice()) {
            const Vector3 epos = enemy_->GetPosition();
            enemySlice_.Start(enemy_->GetModel(), epos, kSliceScale, rng_());
            enemy_->SetVisible(false);
            finisherEffects::EmitReleaseBlades(bladeFlash_, spaceWarp_, epos);
        }
    }
    // 戦闘の更新を通らないので、カメラ・空・敵の見た目と切り分けの演出はここで進める
    UpdateCamera();
    skydome_->Update(camera_.get());
    RefreshEnemyVisualTransforms();
    enemySlice_.Update(dt, camera_.get());
    bladeFlash_.Update(dt, camera_.get());
    if (enemy_ != nullptr && spaceWarp_.IsActive()) {
        finisherEffects::UpdateWarpCenter(spaceWarp_, *camera_, enemy_->GetPosition());
    }
    spaceWarp_.Update(dt);
    SlashMark::GetInstance()->Update(dt);
    // 見せ終えて破片も消えたら、プレイヤーを戻して武器を渡す（次のフレームのクリア判定でクリアへ進む）
    if (bossExecution_.IsFinished() && !enemySlice_.IsActive()) {
        bossExecution_.End();
        CompleteBossWeaponSteal();
    }
}

/** @brief ボスの武器を手に入れ、ボス技を覚える */
void GamePlayScene::CompleteBossWeaponSteal() {
    const GameRulesData& rules = GameRules::GetInstance()->Get();
    weaponStealTriggered_ = true;
    enemy_->SetVisible(false);
    // ボスの配置物に設定された武器を奪う（未設定ならgame_rules.jsonの既定）。あわせてボス技を習得する
    const WeaponManager::AcquireResult acquired = WeaponManager::GetInstance()->Acquire(bossWeaponType_);
    ApplyWeaponAcquireResult(acquired);
    // ボスの武器は次のステージの専用の壁を壊す鍵になるため、交換画面が出ても破棄させない
    pendingWeaponMandatory_ = WeaponManager::GetInstance()->HasPendingWeapon();
    RunData::GetInstance()->AddBossTechnique(rules.bossTechnique);
}

} // namespace engine::game

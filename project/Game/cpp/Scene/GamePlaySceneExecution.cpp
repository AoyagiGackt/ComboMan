/**
 * @file GamePlaySceneExecution.cpp
 * @brief GamePlayScene のうち、膝をついたボスへのとどめの演出（BossExecution）と、ボスの武器を手に入れる所
 * @note 分割ファイル。クラスは GamePlayScene のまま、とどめまわりの定義だけをここに置く
 */
#include "GamePlayScene.h"
#include "BossExecutionTuning.h"
#include "FinisherEffects.h"
#include "GameFlags.h"
#include "GameRules.h"
#include "HitEffectLibrary.h"
#include "RunData.h"
#include "SlashMark.h"
#include "StageEditor.h"
#include "TimeManager.h"
#include "WeaponManager.h"

namespace engine::game {
namespace {
constexpr Vector3 kSliceScale = { 1.0f, 1.0f, 1.0f }; ///< ボスを切り分ける時の破片の倍率
constexpr float kHalf = 0.5f;
} // namespace

/** @brief 膝をついたボスへのとどめの演出を始める（始められない時は false） */
bool GamePlayScene::TryStartBossExecution() {
    // 膝をついて見えているステージのボスだけ（ラスボス戦は専用の演出がある）
    if (enemy_ == nullptr || !enemy_->IsKneeling() || !enemy_->IsVisible() || IsFinalBossMode()) {
        return false;
    }
    // 銃で倒した時は銃で撃ち抜く型にする（クリアの決め演出も弾痕のまま）。
    // 銃の型が無ければ抜き取った武器で斬る型にするので、クリアの決め演出も斬撃の方にする
    const BossExecutionTuning& tuning = BossExecutionTuningStore::Get();
    const ExecutionStyle* style = bossFinishedByGun_ ? tuning.FindGunStyle(GetEditorLevelPath()) : nullptr;
    const bool byGun = style != nullptr;
    if (style == nullptr) {
        style = tuning.FindStyle(GetEditorLevelPath());
    }
    if (style == nullptr) {
        return false;
    }
    bossFinishedByGun_ = byGun;
    // 隠しの型: 爆弾がボスにめり込んだまま倒した時は、武器を抜かずにその爆弾で吹き飛ばす（クリアの決め演出は最後の一撃のまま）
    executionBombName_.clear();
    const ExecutionStyle* bombStyle = tuning.FindBombStyle();
    if (bombStyle != nullptr) {
        executionBombName_ = FindBombInsideBoss();
    }
    const bool byBomb = !executionBombName_.empty();
    if (byBomb) {
        style = bombStyle;
    }
    const RangedWeaponData& gun = WeaponManager::GetInstance()->GetRanged();
    BossExecution::Setup setup;
    setup.style = style;
    setup.weapon = bossWeaponType_;
    setup.byGun = byGun && !byBomb;
    setup.byBomb = byBomb;
    setup.gun = gun.type;
    setup.gunColor = ColorFromArray(gun.color);
    setup.bossPosition = enemy_->GetPosition();
    // ジャンプや落下の途中で始めても、真下の床に立たせた高さから演出を始める
    const std::vector<AABB> terrain = GetStageEditor().GetTerrainColliders();
    setup.standingY = player_->FindStandingPosition(player_->GetPosition(), terrain).y;
    setup.terrain = &terrain;
    BossExecution::Services services;
    services.player = player_.get();
    services.particles = pm_;
    services.shaker = &cameraShaker_;
    services.rng = &rng_;
    services.camera = camera_.get();
    // ロックオンでカメラが片寄らないように外してから映す
    lockedEnemy_ = nullptr;
    bossExecution_.Start(setup, services);
    // 壁際で倒した時に演出がずらしたボスの位置へ、ボスの体も合わせる
    GetStageEditor().PlaceEnemyAt(enemy_, bossExecution_.GetBossPosition());
    GetStageEditor().SetGameplayHeld(true);
    // 落ちている武器が演出のカメラに映り込まないよう、終わるまで隠す
    SetWeaponEnemyWeaponsHidden(true);
    return true;
}

/** @brief ボスの体の中に中心がめり込んでいる、まだ壊れていない爆弾の名前を返す（無ければ空） */
std::string GamePlayScene::FindBombInsideBoss() {
    const AABB box = GetEnemyHitBox();
    for (const BreakableRef& breakable : GetStageEditor().GetBreakables()) {
        const BreakableSettings settings = breakable.Settings();
        // 爆風を持つ物だけ（壁や崩れるだけの物は爆弾ではない）
        if (settings.radius <= 0.0f || settings.isWall) {
            continue;
        }
        const Vector3 position = breakable.GetPosition();
        const bool inside = position.x >= box.min.x && position.x <= box.max.x && position.y >= box.min.y && position.y <= box.max.y;
        if (inside) {
            return std::string(settings.name);
        }
    }
    return std::string();
}

/** @brief とどめの演出で、ボスにめり込んだ爆弾を壊して爆発を出す（プレイヤーと敵は巻き込まない） */
void GamePlayScene::DetonateExecutionBomb() {
    for (const BreakableRef& breakable : GetStageEditor().GetBreakables()) {
        const BreakableSettings settings = breakable.Settings();
        if (settings.name != executionBombName_) {
            continue;
        }
        breakable.Destroy();
        GameFlags::GetInstance()->SetFlag("broken_" + executionBombName_, true);
        const Vector3 position = breakable.GetPosition();
        // 爆発の土煙は箱の下端に置く
        const Vector3 footPosition = { position.x, position.y - settings.scale.y * kHalf, position.z };
        EmitBreakableDestructionEffect(position, footPosition, settings.radius);
        break;
    }
    executionBombName_.clear();
}

/** @brief 道中の武器敵の武器（倒して落ちている物も含む）を描かない・描くに切り替える */
void GamePlayScene::SetWeaponEnemyWeaponsHidden(bool hidden) {
    for (auto& entry : weaponEnemies_) {
        if (entry.enemy != nullptr) {
            entry.enemy->SetWeaponHidden(hidden);
        }
    }
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
        // 爆弾を爆発させる拍で、めり込んでいた爆弾を壊して爆発を出す（切り分けと同じ拍に置くと、爆発で砕けたように見える）
        if (bossExecution_.ConsumeBomb()) {
            DetonateExecutionBomb();
        }
        // 切り分ける拍で、ボスを破片に差し替えて刃を一斉に放つ
        if (bossExecution_.ConsumeSlice()) {
            const Vector3 epos = enemy_->GetPosition();
            enemySlice_.Start(enemy_->GetModel(), epos, kSliceScale, rng_());
            if (bossExecution_.IsByGun() || bossExecution_.IsByBomb()) {
                // 銃で撃ち砕いた時と爆弾で吹き飛ばした時は刃を放たず、空間のゆがみだけを掛ける。突き立った武器は後で抜き取るので本体だけを消す
                enemy_->SetBodyHidden(true);
                spaceWarp_.AddImpulse(HitEffectLibrary::GetInstance()->FinisherVisuals().releaseWarpImpulse);
            } else {
                enemy_->SetVisible(false);
                finisherEffects::EmitReleaseBlades(bladeFlash_, spaceWarp_, epos);
            }
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
        SetWeaponEnemyWeaponsHidden(false);
        CompleteBossWeaponSteal();
    }
}

/** @brief ボスの武器を手に入れ、ボス技を覚える */
void GamePlayScene::CompleteBossWeaponSteal() {
    const GameRulesData& rules = GameRules::GetInstance()->Get();
    weaponStealTriggered_ = true;
    enemy_->SetVisible(false);
    // 銃で撃ち砕いた時に本体だけ消していた分を戻す（全体を消したので、見た目は変わらない）
    enemy_->SetBodyHidden(false);
    // ボスの配置物に設定された武器を奪う（未設定ならgame_rules.jsonの既定）。あわせてボス技を習得する
    // ボスの武器はふつうの枠とは別のボスの枠へ入れる（入れ替わらない。別の種類なら主軸を選んで合体）
    AcquireBossWeapon(bossWeaponType_);
    RunData::GetInstance()->AddBossTechnique(rules.bossTechnique);
}

} // namespace engine::game

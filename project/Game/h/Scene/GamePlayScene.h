/**
 * @file GamePlayScene.h
 * @brief メインの戦闘シーン（ローグライト／サンドボックス両対応）
 */
#pragma once
#include <cstdint>
#include "HudLayer.h"

// 標準ライブラリ
#include <array>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

// エンジンシステム・基盤
#include "Audio.h"
#include "BaseScene.h"
#include "Camera.h"
#include "DirectXCommon.h"
#include "ImGuiManager.h"
#include "Input.h"
#include "Model.h"
#include "ModelCommon.h"
#include "Object3dCommon.h"
#include "ShadowManager.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include "SrvManager.h"

// ゲームロジック・オブジェクト
#include "BladeFlashEffect.h"
#include "ComboFeelDirector.h"
#include "ShotTargetPicker.h"
#include "CameraShaker.h"
#include "Collision.h"
#include "ElementEffect.h"
#include "EnemyEntity.h"
#include "AllyBoss.h"
#include "EnemyRegistry.h"
#include "FontRenderer.h"
#include "GlassShatterEffect.h"
#include "ImageFilter.h"
#include "BossBattleDirector.h"
#include "FinalBossHud.h"
#include "FinisherEffects.h"
#include "KickedBombPool.h"
#include "ThrownWeaponPool.h"
#include "LevelLoader.h"
#include "MeshSliceEffect.h"
#include "Object3d.h"
#include "Player.h"
#include "RenderTexture.h"
#include "SceneEditor.h"
#include "SceneShared.h"
#include "Skydome.h"
#include "SpaceDistortionEffect.h"
#include "BossExecution.h"
#include "ClearFinish.h"
#include "FinalBossBattle.h"
#include "GuidePanel.h"
#include "PlayerDefeat.h"
#include "StageIntro.h"
#include "ResultBackdrop.h"
#include "StyleMeter.h"
#include "TimeManager.h"
#include "ComboMotionEditor.h"
#include "PauseMenu.h"
#include "UIMenu.h"
namespace engine::graphics {
class GrayscaleEffect;
class HsvFilter;
class ParticleManager;
}

namespace engine::game {

class BreakableRef;
struct BreakableSettings;
struct WeaponFinisherData;
struct MonsterTraitData;
using engine::AABB;
using engine::Audio;
using engine::Collision;
using engine::DirectXCommon;
using engine::Input;
using engine::TimeManager;
using engine::graphics::BladeFlashEffect;
using engine::graphics::Camera;
using engine::graphics::GlassShatterEffect;
using engine::graphics::GrayscaleEffect;
using engine::graphics::HsvFilter;
using engine::graphics::ImageFilter;
using engine::graphics::ImGuiManager;
using engine::graphics::MeshSliceEffect;
using engine::graphics::Model;
using engine::graphics::ModelCommon;
using engine::graphics::Object3d;
using engine::graphics::Object3dCommon;
using engine::graphics::ParticleManager;
using engine::graphics::RenderTexture;
using engine::graphics::ShadowManager;
using engine::graphics::Skydome;
using engine::graphics::SpaceDistortionEffect;
using engine::graphics::Sprite;
using engine::graphics::SpriteCommon;
using engine::graphics::SrvManager;

class GamePlaySceneInitializer;

/**
 * @brief メインステージの戦闘、進行、演出、描画を統括する
 *
 * プレイヤーと敵のゲーム進行を調停し、個別システムの更新結果を描画パスへ渡す。
 */
class GamePlayScene : public BaseScene {
    friend class GamePlaySceneInitializer;

public:
    /**
     * @brief シーンで使用するゲーム実体と描画資源を初期化する
     * @param dxCommon DirectXの共通処理
     * @param input 入力管理
     * @param audio 音声管理
     */
    void Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio) override;
    /** @brief シーン固有の演出資源と登録済みコールバックを破棄する */
    void Finalize() override;
    /** @brief ゲーム進行、戦闘、カメラ、演出を更新する */
    void Update() override;
    /** @brief 3Dワールドと画面UIを描画する */
    void Draw() override;
    /**
     * @brief ポストエフェクト対応の有無を返す
     * @return 常にtrue
     */
    bool SupportsPostEffects() const override {
        return true;
    }

    /** @brief エディタから操作するプレイヤーを返す */
    Player* GetEditorPlayer() const override {
        return player_.get();
    }

    /**
     * @brief ステージエディタが読み書きするレベルファイルを返す
     * @return レベルJSONのパス（game_rules.jsonのlevelPathsから現在フロアに対応するもの）
     */
    std::string GetEditorLevelPath() const override;
    /**
     * @brief ステージ配置物の生成に使用するモデル共通処理を返す
     * @return シーンが所有するモデル共通処理
     */
    ModelCommon* GetEditorModelCommon() override {
        return modelCommon_.get();
    }
    /**
     * @brief ステージエディタの表示と操作に使用するカメラを返す
     * @return シーンが所有するカメラ
     */
    Camera* GetEditorCamera() override {
        return camera_.get();
    }
    /**
     * @brief エディタ配置敵の演出に使用するパーティクル管理を返す
     * @return 共有パーティクル管理
     */
    ParticleManager* GetEditorParticleManager() override {
        return pm_;
    }
    /** @brief 編集中にプレイヤーの表示座標を現在位置へ同期する */
    void RefreshVisualTransformsForEditor() override;

    /** @brief StageEditorへ配置済みの敵（武器持ち雑魚・ボス）をenemy_/weaponEnemies_へ結び付ける */
    void OnEditorLevelLoaded() override;
    /**
     * @brief StageEditorの戦闘対象一覧とweaponEnemies_を同期する（毎フレーム呼ぶ）
     * @note spawn_pointが後から出した敵を取り込み、無効化された敵を外す。既存の項目はHP・奪取状態を保つ
     */
    void SyncCombatEnemies();
    /** @brief エディタが配置物の実体を作り直していたら、ボスとロック対象を含む参照を取得し直す */
    void SynchronizeEditorReferences() override;
    /** @brief 配置されたボスを取得し、新しい実体の時だけ初期のHPと色を設定する */
    void BindEditorBoss();
    /** @brief 現在参照しているボス実体の識別番号 */
    uint64_t bossInstanceId_ = 0;

    /** @brief クリアの決め演出を確認用に始める（剣か銃かはランダム。ランの結果やセーブには反映しない） */
    void TriggerGlassShatterTest();
    /** @brief デバッグ用にその場でクリアする（進行度を進め、マップで解放演出を見せる） */
    void TriggerDebugClear();
    /** @brief ボスを倒さずにクリアの決め演出を始める（previewOnly なら進行とセーブに反映しない） */
    void StartDebugClear(bool previewOnly);

    /**
     * @brief 追加ホットキーの案内文字列を返す
     * @return シーン調整パネルのホットキー文字列
     */
    const char* GetHotkeyOverlayExtra() const override {
        return "F3: シーン調整パネル\nF6: コンボ演出の調整\nF7: モーションエディタ";
    }

private:
    static constexpr const char* kSceneName = "GAMEPLAY"; ///< scene_flow.json のキー（SceneFactory の登録名と同じ）
    static constexpr const char* kLayoutName = "gameplay"; ///< 画面UIの配置を引く名前（F2のエディタで動かした値は Resources/Config/UI/gameplay.json に保存される）
    static constexpr float kCenterFactor = 0.5f; ///< 文字を左右の中央にそろえる時に、幅へ掛ける割合

    /** @brief シーンが所有するゲーム実体、描画資源、演出を責務順に初期化する */
    void InitializeCoreSystems();
    /** @brief InitializeCoreSystems()の下請け 描画基盤(Common類・シャドウ・カメラ)とスカイドームを初期化する */
    void InitializeRenderFoundation();
    /** @brief InitializeCoreSystems()の下請け レンダーテクスチャとクリア演出用オーバーレイスプライトを初期化する */
    void InitializeRenderTargetsAndOverlays();
    /** @brief InitializeCoreSystems()の下請け パーティクル・水面・武器スロットHUDを初期化する */
    void InitializeParticlesAndHud();
    /** @brief InitializeCoreSystems()の下請け 残像用オブジェクト・シーンエディタ・画面演出エフェクトを初期化する */
    void InitializeGhostEditorAndEffects();
    // シャドウマップ描画パス
    void DrawShadowPass();
    // スタイルランクとコンボ数のUI描画
    void DrawStyleUI();
    /** @brief テストシーンと共通の武器スロットUIを初期化する */
    void InitializeWeaponSlotHud();
    /** @brief 武器スロットUIの選択状態と演出を更新する */
    void UpdateWeaponSlotHud();
    /** @brief 武器スロットUIを描画する */
    void DrawWeaponSlotHud();
    /** @brief プレイヤーの進行位置に対応する操作目標を描画する */
    void DrawStageGuide();
    /** @brief 満杯時の武器交換入力を処理する */
    void UpdateWeaponExchange();
    /** @brief 満杯時の武器交換画面を描画する */
    void DrawWeaponExchange();
    /** @brief 一時停止メニューの初期化（退出項目はステージセレクトへ戻る） */
    void SetupPauseMenu();
    /** @brief 道中の武器敵を更新し、攻撃と武器奪取を処理する */
    void UpdateWeaponEnemies();
    /** @brief 武器を奪った結果に応じた後処理（新しく増えたら通知、持っている武器なら覚醒ゲージへ） */
    void ApplyWeaponAcquireResult(WeaponManager::AcquireResult result);
    /** @brief レベルに配置された壊せる物（kind=="breakable"）のヒット判定・爆発ダメージ・演出を処理する */
    void UpdateExplosiveBarrels();
    /** @brief 爆風の有無に応じて破壊演出をまとめて出す */
    void EmitBreakableDestructionEffect(const Vector3& position, const Vector3& footPosition, float radius);
    /** @brief 攻撃を当てた爆弾を吹き飛ばす（吹き飛ばせない物・飛ばす枠が無い時は false） */
    bool TryKickBomb(const BreakableRef& barrel, const BreakableSettings& settings, const BombPayload& payload);
    /** @brief 爆風の範囲にいるプレイヤーと敵へダメージを与える */
    void ApplyBombBlast(const Vector3& position, const BombPayload& payload);
    /** @brief 生きている敵の当たり判定のどれかに、球が重なったか */
    bool BombTouchesEnemy(const Vector3& position, float radius) const;
    /** @brief 飛んでいる爆弾を進め、ぶつかった爆弾を爆発させる */
    void UpdateKickedBombs();
    /** @brief 自動テストプレイの間だけ、遊んでいる様子からおかしな所を探して SmokeTestRunner へ知らせる */
    void RunBugChecks();
    /** @brief RunBugChecks の下請け プレイヤーの位置・体力・先へ進めるかを調べる */
    void CheckPlayerForBugs(const AABB& stageBounds, bool hasBounds, const std::vector<AABB>& terrain, float elapsed);
    /** @brief RunBugChecks の下請け 生きている敵が届かない所にいないか・生き返っていないかを調べる */
    void CheckEnemiesForBugs(const AABB& stageBounds, bool hasBounds, const std::vector<AABB>& terrain, float elapsed);
    /** @brief RunBugChecks の下請け 敵グループの全滅を待つ条件（扉など）が、倒し切ったのに成立しない所を探す */
    void CheckEnemyGroupConditionsForBugs(const AABB& stageBounds, bool hasBounds, const std::vector<AABB>& terrain, float elapsed);
    /** @brief プレイヤーから見て対象の向こう側の向き（真上や真下で決まらない時は向いている方） */
    float AwayFromPlayerDirX(const Vector3& position) const;
    /** @brief 壊した壁をプレイヤーの反対側へ吹き飛ばす（飛ばす枠が無い時は false） */
    bool LaunchBrokenWall(const BreakableRef& wall, const BreakableSettings& settings);
    /** @brief 飛んでいる壁の箱に重なった敵を、まだ巻き込んでいなければダメージを与えて吹き飛ばす */
    void SweepEnemiesWithWall(const AABB& box, float dirX, std::vector<const void*>& alreadyHit);
    /** @brief その位置へボスを動かすと壁にめり込むか */
    bool IsBossBlockedAt(const Vector3& position) const;
    /** @brief ボスとプレイヤーの間に、壁や閉じた扉が無いか */
    bool BossCanSeePlayer() const;
    /** @brief 敵とプレイヤーの間に、壁や閉じた扉があるか（敵の体の高さで横に細い箱を通して調べる。低い段差は数えない） */
    bool IsWallBetweenEnemyAndPlayer(const Vector3& enemyPosition) const;
    /** @brief ボス戦の進行役を1フレーム進め、起きたことに合わせて演出と当たり判定を出す */
    void UpdateStageBoss(float dt);
    /** @brief 画面下の体力バーと名乗りの表示を更新する */
    void UpdateStageBossHud(float dt);
    /** @brief ボスの叩きつけの範囲（第2段階で広がる） */
    float BossSlamRadius() const;
    /** @brief 当てた攻撃でボスが吹き飛んだりのけぞったりするか（ステージボスはひるんでいる間だけ） */
    bool BossReactsToHits() const;

    // ── 武器ごとの締めの動作（GamePlaySceneFinishers.cpp） ──
    /** @brief 締めの動作で狙う敵1体（ボスかどうかで吹き飛ばしてよいかが変わる） */
    struct FinisherTarget {
        EnemyEntity* enemy = nullptr;
        bool isBoss = false;
    };
    /** @brief 締めの動作で狙える生きた敵を集める（ボスと道中の敵） */
    std::vector<FinisherTarget> CollectFinisherTargets() const;
    /** @brief 締めの動作のダメージを与え、吹き飛ばしてよいかを返す（ボスはひるんでいる間だけ true） */
    bool DamageByFinisher(const FinisherTarget& target, float damageMult, float weaponDamage);
    /** @brief 今持っている武器の威力 */
    float CurrentWeaponDamage() const;
    /** @brief 投げる武器（斧）の威力 */
    float ThrownWeaponDamage() const;
    /** @brief 地上コンボの最後の段が当たった瞬間なら、締めの動作を出すと覚える（毎フレーム、プレイヤーの更新の後に呼ぶ） */
    void UpdateWeaponFinisher();
    /** @brief 覚えておいた締めの動作を出す（ボスへの通常の当たりの処理の後に呼ぶ） */
    void ApplyPendingWeaponFinisher();
    /** @brief プレイヤーがその位置に立てるか（体が壁にめり込まず、足元に床がある） */
    bool CanPlayerStandAt(const Vector3& position) const;
    /** @brief プレイヤーを今の位置から横へその位置まで動かす通り道に、壁や閉じた扉があるか（瞬間移動で壁を抜けないように） */
    bool IsPlayerPathBlocked(const Vector3& position) const;
    /** @brief プレイヤーを from から横へ to まで動かす通り道に、壁や閉じた扉があるか（高さは to で見る） */
    bool IsPlayerSweepBlocked(const Vector3& from, const Vector3& to) const;
    /** @brief プレイヤーが from から to へ動いた間に、体の高さにある壁や閉じた扉の真ん中を横切ったか */
    bool DidPlayerCrossWall(const Vector3& from, const Vector3& to) const;
    /** @brief 敵を from から横へ to まで動かす通り道に、壁や閉じた扉があるか（halfWidth は敵の体の幅の半分） */
    bool IsEnemyPathBlocked(const Vector3& from, const Vector3& to, float halfWidth) const;
    /** @brief 前方の箱に重なる敵へ、関数を呼ぶ */
    void ForEachTargetInFront(float length, float halfHeight, const std::function<void(const FinisherTarget&)>& visit);
    /** @brief 槍: 前へ長く突き、当たった敵を横へ突き飛ばす */
    void FinishThrust(const WeaponFinisherData& data);
    /** @brief ハンマー: 地面を叩き、周りの敵を衝撃波で浮かせて崩す */
    void FinishShockwave(const WeaponFinisherData& data);
    /** @brief 大剣: 前へ振り下ろし、浮いている敵は地面へ叩きつけ、地上の敵は長く止める */
    void FinishSlamDown(const WeaponFinisherData& data);
    /** @brief 鎌: 回りながら周りの敵を手元へ引き寄せる */
    void FinishPullSpin(const WeaponFinisherData& data);
    /** @brief 短剣: 前にいる一番近い敵の背後へ回り込む */
    void FinishWarpBehind(const WeaponFinisherData& data);
    /** @brief 斧: 前へ投げ、貫通して戻ってくる */
    void FinishThrow(const WeaponFinisherData& data);
    /** @brief 投げる武器の見た目を用意する（シーンの初期化で1回） */
    void InitializeThrownWeapons();
    /** @brief 投げた武器を進め、当たった敵にダメージを与える（毎フレーム呼ぶ） */
    void UpdateThrownWeapons();
    /** @brief その位置に立つと壁の中か（足元の床は数えない） */
    bool IsSolidAt(const Vector3& position) const;
    /** @brief 左上にステージ名の見出しを描画する */
    void DrawWeaponListPanel();
    /** @brief ノードグラフの ShowMessage で出した文字を画面中央に描画し、残り時間を減らす */
    void DrawScriptMessage();
    /** @brief ノードグラフのメッセージ・カメラ・操作停止の受け口を登録する */
    void RegisterScriptEffectHandlers();
    /** @brief ノードグラフで操作を止めている間は、ポーズ以外の操作を押していない扱いにする */
    void ApplyScriptControlLock();
    /** @brief ボス・道中の武器敵の頭上HPバーの位置と色を更新する（BattleTestSceneのダミーHPバーと同じ体裁） */
    void UpdateEnemyHpBars();
    /** @brief 左下のプレイヤーHPゲージの位置と色を更新する */
    void UpdatePlayerHpBar();
    // ローグライトのHP/フロア情報HUD描画
    void DrawRogueliteHUD();
    // モデル共通描画設定（PSO/ルートシグネチャ）の適用
    void SetupModelRenderState();
    // ポストエフェクト適用中かで描画先RTVを切り替える
    D3D12_CPU_DESCRIPTOR_HANDLE GetActiveRTVHandle() const;
    /** @brief いまの描画先がバックバッファか（ポストエフェクトを使わずに直接画面へ描いているか） */
    bool IsDrawingToBackBuffer() const;
    // メインレンダーターゲットのセットアップ
    void SetupMainRenderTarget();
    // カメラ位置・回転の移動平均によるスムージング
    void UpdateCameraSmoothing();
    // SceneEditor用の編集コンテキストを構築
    SceneEditor::EditContext BuildEditContext();

    // クリア演出（結果表示）の状態更新表示中ならtrue
    bool UpdateClearState();
    // 戦闘ロジック全体の更新
    void UpdateCombat();
    /** @brief 当たり判定ブロック全体を囲むステージの範囲 */
    struct StageBounds {
        float left;
        float right;
        float bottom;
        float top;
        bool fromLevel; ///< レベルのブロックから求めたか（false ならブロックが無く既定の広さ）
    };
    StageBounds GetStageBounds();
    /** @brief ステージ開始演出を始める（最初にレベルを読み込んだ時に1回） */
    void StartStageIntro();
    /** @brief 開始演出の間の1フレーム（操作・HUD・敵を止め、カメラだけ演出に合わせて動かす） */
    void UpdateStageIntro();
    // ── ラスボス戦（GamePlaySceneFinalBoss.cpp） ──
    /** @brief ラスボス戦のレベルを開いているか（最後のステージをクリアしてからラスボスを倒すまで） */
    bool IsFinalBossMode() const;
    /** @brief ラスボス戦の進行役を用意する（Initialize の最後に、ラスボス戦の時だけ） */
    void InitializeFinalBoss();
    /** @brief 演出の途中でプレイヤーの操作を止めている間の1フレーム（止めていなければ false） */
    bool UpdateFinalBossCinematic();
    /** @brief 闘技場のカメラにする（やられ演出の間は使わない。使ったら true） */
    bool UpdateArenaCamera();
    /** @brief 闘技場の刀を画面内の目印と画面端の方向矢印で案内する。 */
    void DrawArenaWeaponStealPrompt(const Vector3& position, bool selected, float promptScale, const Vector4& promptColor);
    /** @brief ラスボスの攻撃の判定をプレイヤーへ当てる（当たったら true。回避中・無敵中は当たらない） */
    bool StrikePlayerFromBoss(const AABB& box, const Vector3& effectPosition, int damage);
    /** @brief プレイヤーのやられ演出を始める（ラン中にHPが尽きた時に1回） */
    void StartPlayerDefeat();
    /** @brief やられ演出の間の1フレーム（操作・HUD・敵を止め、カメラだけ演出に合わせて動かし、見せ終えたら選ばれた行き先（同じステージ／マップ）へ） */
    void UpdatePlayerDefeat();
    /** @brief 膝をついたボスへのとどめの演出を始める（始められない時は false を返し、従来の吸い寄せで奪う） */
    bool TryStartBossExecution();
    /** @brief とどめの演出の間の1フレーム（操作・敵を止め、演出・切り分け・カメラだけを進め、終わったら武器を渡す） */
    void UpdateBossExecution();
    /** @brief ボスに近接武器を当てた事を覚える（倒した後の当たりは数えない。とどめの型と姿勢をこの武器で決める） */
    void RecordBossMeleeHit(WeaponType weapon);
    /** @brief ボスの体の中に中心がめり込んでいる、まだ壊れていない爆弾の名前を返す（無ければ空） */
    std::string FindBombInsideBoss();
    /** @brief とどめの演出で、ボスにめり込んだ爆弾を壊して爆発を出す（プレイヤーと敵は巻き込まない） */
    void DetonateExecutionBomb();
    /**
     * @brief ボスの武器を手に入れ、ボス技を覚える（吸い寄せ・とどめのどちらで奪った時も同じ）
     * @param keepHeldWeapon true なら手に入れた後も今の武器を持ったままにする（倒した武器のままとどめを刺した時）。false ならボスの武器へ持ち替える
     */
    void CompleteBossWeaponSteal(bool keepHeldWeapon = false);
    /** @brief 道中の武器敵の武器（倒して落ちている物も含む）を描かない・描くに切り替える（とどめの演出の画面に映さないため） */
    void SetWeaponEnemyWeaponsHidden(bool hidden);
    /** @brief 敵（ボスと道中の武器敵）の見た目の行列だけを今のカメラで計算し直す */
    void RefreshEnemyVisualTransforms();
    /** @brief 立っている敵（倒れた敵と消えた敵は除く）の位置を集める */
    std::vector<Vector3> CollectStandingEnemyPositions() const;
    /** @brief 乱舞の相手に向いた敵を選ぶ（ロック中の敵、いなければ近くで一番近い敵。届く所に誰もいなければ nullptr） */
    EnemyEntity* PickRampageEnemy() const;
    /** @brief 乱舞の相手を決め直し、向かう位置を返す（乱舞の途中は同じ相手を追い、乱舞していない時は近くの敵を選び直す） */
    Vector3 UpdateRampageEnemy();
    /** @brief 乱舞の打ち上げ・連撃・締めを向ける敵（相手を選べずその場で打ち上げた時は nullptr） */
    EnemyEntity* RampageTargetEnemy() const;
    /** @brief 今の戦闘の相手（ボスか道中の敵）の実体か（作り直しで消えた敵を指したままにしないための確認） */
    bool IsCombatEnemy(const EnemyEntity* enemy) const;
    /** @brief カメラに映っている、まだ倒れていない敵（ステージの主の敵と道中の敵）を集める（フィニッシャーの当たる相手） */
    std::vector<EnemyEntity*> CollectOnScreenEnemies() const;
    /** @brief クリアの決め演出を始める（ボスを倒した最後の一撃が銃なら弾痕、それ以外なら斬線） */
    void StartClearFinish();
    /** @brief クリアの結果画面（黒い背景に CLEAR!。withRunResult ならランク・スコアも）を描く */
    void DrawClearResultScreen(bool withRunResult);
    // 攻撃ヒット判定・ダメージ処理などの戦闘イベント更新
    void UpdateCombatEvents();
    /** @brief Shift長押しで最寄りの敵をロックオンし、その方向へ向かせる（BattleTestSceneと同じ規約） */
    void UpdateTargetLock();
    // カメラ追従・シェイクの更新
    void UpdateCamera();
    // スタイルメーターとUI状態の更新
    void UpdateStyleAndUI(float dt);
    /** @brief UpdateStyleAndUI()の下請け 敵の当たり判定AABBを現在位置から算出する */
    AABB GetEnemyHitBox() const;
    /**
     * @brief 敵の当たり判定AABBを指定位置から算出する（ボス・道中の敵で共通の大きさ）
     * @param position 敵の位置
     */
    static AABB EnemyHitBoxAt(const Vector3& position);
    /**
     * @brief 空中の近接判定を、上下は中心から指定の高さに、左右は指定の幅だけ広げる（高さが合わずに空振りするのを減らす）
     * @param range 広げる近接判定
     * @param center プレイヤーの位置
     * @param halfHeight 上下に取る高さ（中心から片側）
     * @param widthPadding 左右に足す幅（片側）
     */
    static void WidenAirMeleeRange(AABB& range, const Vector3& center, float halfHeight, float widthPadding);
    /**
     * @brief 固有技の判定範囲（プレイヤーを中心にした横長の箱）を返す
     * @param center プレイヤーの位置
     * @param radius 左右の半径
     */
    static AABB SkillRangeAround(const Vector3& center, float radius);
    /** @brief UpdateStyleAndUI()の下請け 近接コンボのヒット判定とスタイル加点・属性追撃を処理する */
    void ApplyMeleeComboStyleHit(const AABB& enemyAABB);
    /** @brief UpdateStyleAndUI()の下請け 武器固有技（SPACE）のヒット判定とスタイル加点を処理する */
    void ApplyWeaponSkillStyleHit(const AABB& enemyAABB);
    /** @brief UpdateStyleAndUI()の下請け 銃コンボのヒット判定とスタイル加点を処理する */
    void ApplyGunShotStyleHit();
    /** @brief UpdateStyleAndUI()の下請け 覚醒乱舞ラッシュのヒット判定とスタイル加点を処理する（乱舞の相手に当てる） */
    void ApplyRampageStyleHit();
    /** @brief 近接の一撃ぶんのスタイルを加える（ボスにも道中の敵にも共通） */
    void AwardMeleeStyle();
    /** @brief 武器固有技の一撃ぶんのスタイルを加える（ボスにも道中の敵にも共通） */
    void AwardSkillStyle();
    /** @brief 射撃の一発ぶんのスタイルを加える（ボスにも道中の敵にも共通） */
    void AwardGunStyle();
    /** @brief UpdateStyleAndUI()の下請け スタイルメーターの時間経過による減衰を処理する */
    void DecayStyleMeter(float dt);
    /** @brief 現在のスタイルランクが高いほど多く覚醒ゲージを溜める（攻撃ヒット・ジャスト回避用） */
    void ChargeAwakenGaugeByStyle(float amount);
    /** @brief UpdateStyleAndUI()の下請け 回避の連打ペナルティを処理する（ジャスト回避の加点はTryJustDodge側） */
    void UpdateDodgeStyle();
    /** @brief 覚醒中の武器倍率とジャスト回避直後の強化窓を合わせた、現在の攻撃ダメージ倍率 */
    float CurrentDamageMult() const;
    /**
     * @brief 固有技の判定半径を返す（覚醒中の武器倍率と、習得済みボス技による叩きつけ強化を含む）
     * @param baseRadius 通常時の判定半径
     * @param slam 叩きつけ系（大剣/ハンマー）の技か
     */
    float SkillRadiusFor(float baseRadius, bool slam) const;
    /**
     * @brief 近接ヒットで敵に与える被弾硬直の秒数（次の段が出るまで拘束できる長さ）
     * @param attack ヒットした段（nullptrなら既定値）
     */
    float MeleeHitstunSeconds(const MeleeAttackDef* attack) const;
    /** @brief 銃の疲労の添字（近接武器の後ろ） */
    int GunFatigueIndex() const;
    /** @brief 手に持っている近接武器の疲労の添字（素手なら-1） */
    int HeldMeleeFatigueIndex() const;
    /** @brief 指定した武器の疲労から、今のダメージ倍率を返す（1.0=万全〜weaponFatigueMinDamageMult） */
    float WeaponFatigueDamageMult(int fatigueIndex) const;
    /** @brief このフレームに当てた武器を記録する（疲労の加算はUpdateWeaponFatigue()でまとめて1回だけ行う） */
    void RegisterFatigueHit(int fatigueIndex, float amount);
    /** @brief 当てた武器へ疲労を足し、それ以外の武器の疲労を抜く（ヒット判定の後に毎フレーム呼ぶ） */
    void UpdateWeaponFatigue();
    /** @brief 疲労で効きが落ちた命中の弾けを、威力に合わせて小さく灰色に寄せる */
    void ApplyFatigueToHitEffect(float damageMult, Vector4& color, float& strength) const;
    /** @brief 同じ武器で当て続けて効きが落ちている時に、持ち替えを促す警告を出す */
    void DrawWeaponFatigueWarning();
    /** @brief レベルに置いた案内文を出すか（1面だけ。エディタのテストプレイでは出す） */
    bool ShowsStageGuideText() const;
    /** @brief 右上のランクの下に、よく使う操作だけの短い一覧を出す */
    void DrawShortControls();
    /** @brief 奪える武器に印を出し、届く距離の一番近い1つだけに回収の操作を出す */
    void DrawWeaponStealPrompts(float promptScale, const Vector4& promptColor);
    /** @brief 届く距離の外にある奪える武器を、揺れる矢印か画面端の矢印で指す（横スクロールのステージ用） */
    void DrawWeaponPickupGuide(const Vector3& position);
    /** @brief 画面に敵が1体もいない時に、画面外の一番近い敵の方向を画面端の矢印で指す（閉まった扉の向こうの敵は指さない） */
    void DrawOffscreenEnemyGuide();
    /** @brief 床を抜けてステージの一番下より落ちたら、最後に立っていた床へ戻す（毎フレーム、当たり判定の後に呼ぶ） */
    void UpdateFallRescue();
    /** @brief 読み込んだステージに合わせて空の画像を選ぶ（レベルの JSON の sky。書いていないステージは既定の空） */
    void ApplyStageSky();

    // ── 味方のボスと 2人目（GamePlaySceneAlly.cpp） ──
    /** @brief 2P が操作に使っている機器（kNone なら味方のボスは自動で戦う） */
    enum class AllyDevice {
        kNone,
        kFirstPad, ///< 1台目のパッド（1P がキーボードの時だけ）
        kSecondPad, ///< 2台目のパッド
        kKeyboard, ///< キーボード（1P がパッドの時だけ）
    };
    /** @brief 2P の機器の画面に出す名前 */
    static const wchar_t* AllyDeviceName(AllyDevice device);
    /** @brief 味方のボスを呼べるか（ボスの武器どうしを合体させていて、倒れた後の待ちが明けている）。呼べない時は理由を返す */
    std::wstring AllyUnavailableReason() const;
    /** @brief 味方のボスを呼び出す（呼べない時は理由を出して false） */
    bool SummonAlly();
    /** @brief 2P を参加させる（味方がいなければ呼ぶ。呼べなければ参加しない） */
    void JoinTwoPlayer(AllyDevice device);
    /** @brief 2P を抜けさせる（味方のボスは残って自動で戦う。機器は 1P の操作へ戻す） @param message 出す知らせ（nullptr なら出さない） */
    void LeaveTwoPlayer(const wchar_t* message);
    /** @brief 指定した機器で、参加と抜けるの同時押しをしたか */
    bool PressedJoinCombo(AllyDevice device) const;
    /** @brief 2P の機器から、このフレームの操作を読む */
    AllyControl ReadAllyControl() const;
    /** @brief 呼び出し・参加・抜けるの操作を受け付ける */
    void HandleAllyInput();
    /** @brief 味方のボスを1フレーム進める（呼び出し・参加の操作、動き、味方と敵の当たり） */
    void UpdateAlly();
    /** @brief 味方の攻撃が当たった敵にダメージ（体力は1より下げない）・のけぞり・命中演出を出す */
    void ApplyAllyHits(const AllySurroundings& around);
    /** @brief 敵の攻撃と弾を味方に当てる（プレイヤーの体力は減らさない。倒れたら 2P も抜ける） */
    void ApplyEnemyAttacksToAlly(const AllySurroundings& around);
    /** @brief ラスボス戦の援護（2P の攻撃の合図か、2P がいなければ自動で時々、分身の横に現れて一撃入れて消える） */
    void UpdateArenaAssist();
    /**
     * @brief ラスボス戦で援護に使うボスの武器を返す（刀だけに差し替える前のボスの枠。主軸と相棒がそろっていれば true）
     * @param main 主軸の武器
     * @param partner 相棒の武器
     */
    bool ArenaAssistWeapons(const WeaponData*& main, const WeaponData*& partner) const;
    /** @brief 味方のボスの体を描く */
    void DrawAlly();
    /** @brief 味方の頭上の体力バーの位置を決める（毎フレーム、描く前に呼ぶ） */
    void UpdateAllyHud();
    /** @brief 味方の頭上の体力バーを描く */
    void DrawAllyHpBar();
    /** @brief 2P が操作している時だけ、画面の右下に 2P の操作の案内を出す */
    /** @brief 味方のボスの頭上に、敵と見分ける印を出す（ふだんは 味方、2P が操作している間は 2P） */
    void DrawAllyMark();
    /** @brief 味方の体力バーを作る（シーンの初期化で1回。ゲーム中に作ったり消したりしない） */
    void InitializeAlly();
    /** @brief ステージを読み直した時に、味方を引っ込めて 2P を抜けさせる */
    void ResetAlly();
    AllyBoss ally_; ///< 味方のボス
    AllyDevice allyDevice_ = AllyDevice::kNone; ///< 2P が使っている機器
    bool allyPutAway_ = false; ///< 1P が分身を引っ込めたか（引っ込めている間は、ボスの武器を持っていても自動で出さない）
    bool allyAutoSummon_ = false; ///< このステージで分身を自動で出すか（ステージに入った時にボスの武器を持っていた時だけ）
    /** @brief ボスの武器を持っていれば分身を自動で出し、主軸の武器か合体が変わったら出し直す */
    void AutoSummonAlly();
    float allyIdleSeconds_ = 0.0f; ///< 2P が操作していない秒数
    std::unique_ptr<Sprite> allyHpBarBg_; ///< 味方の頭上の体力バーの下地
    std::unique_ptr<Sprite> allyHpBarFg_; ///< 味方の頭上の体力バーの残り
    bool allyHpBarPlaced_ = false; ///< 味方の体力バーの位置をこのフレームに決めたか
    /**
     * @brief 自動テストプレイの時だけ、床抜けを直前の様子と一緒に知らせる
     * @param position 落ちていた位置
     */
    void ReportPlayerFall(const Vector3& position);
    /** @brief 自動テストプレイの見張りで、プレイヤーの今の様子を直前の記録に足す */
    void RecordPlayerTrail(const Vector3& position);
    /**
     * @brief 自動テストプレイの見張りで、武器の持ち方におかしな所が無いか調べる（選ぶ画面が出たまま・合体の番号が範囲の外など）
     * @param elapsed 前に調べてから過ぎた秒数
     */
    void CheckWeaponsForBugs(float elapsed);
    /**
     * @brief 自動テストプレイの見張りで、味方のボスにおかしな所が無いか調べる（離れすぎたまま・落ちたまま・体力が範囲の外）
     * @param elapsed 前に調べてから過ぎた秒数
     */
    void CheckAllyForBugs(float elapsed);
    Vector3 lastSafeGround_ {}; ///< プレイヤーが最後に床に立っていた位置（床抜けの救済で戻す先）
    bool hasSafeGround_ = false; ///< lastSafeGround_ が入っているか
    /** @brief 倒れて武器を奪える敵1体ぶん（武器の位置と、奪える距離） */
    struct StealTarget {
        const EnemyEntity* owner = nullptr;
        Vector3 position;
        float range = 0.0f;
    };
    /** @brief 今奪える武器を全部集める（ボスと道中の敵） */
    std::vector<StealTarget> CollectStealTargets() const;
    /** @brief 届く距離の中で一番近いものの番号を返す（無ければ targets.size()） */
    size_t FindNearestStealTarget(const std::vector<StealTarget>& targets) const;
    /** @brief このフレームに奪う相手を1体だけ決める（1回押して2体から同時に奪わないように） */
    void SelectStealTarget();
    /** @brief その敵がこのフレームに奪う相手か */
    bool IsSelectedStealTarget(const EnemyEntity* owner) const {
        return owner != nullptr && owner == selectedStealOwner_;
    }
    /**
     * @brief 近接ヒットのヒットストップとカメラ揺れを要求する（途中の段は控えめ、締めだけ大きく）
     * @param attack        ヒットした段（nullptrなら既定値）
     * @param finisher      コンボの締めか
     * @param defaultFrames 段定義がない時のヒットストップフレーム数
     */
    /** @brief コンボの締めが当たった瞬間に、床を這う衝撃と武器の色の残り火を出す */
    void EmitComboFinisherAccent(const Vector3& position);
    /**
     * @brief 近接が当たった時の、武器ごとの演出と音（hit_effects.json の weapon_hit_<hitStyle>、締めは weapon_finisher_<hitStyle>）
     * @param position 当たった敵の位置
     * @param finisher 締めの一撃か
     */
    void EmitWeaponStyleHit(const Vector3& position, bool finisher);
    void RequestMeleeHitStopAndShake(const MeleeAttackDef* attack, bool finisher, int defaultFrames);
    /** @brief 叩きつけ系固有技が習得済みボス技で強化されているか */
    bool HasBossSlamTechnique() const;
    /**
     * @brief 近接敵の攻撃発生フレームに前方判定を出し、命中ならプレイヤーへダメージを与える
     * @param attacker 攻撃した敵
     * @note 回避中ならジャスト回避として処理し、ダメージは入らない
     */
    void ApplyEnemyMeleeSwing(EnemyEntity* attacker);
    /**
     * @brief 敵の攻撃がプレイヤーに届いた瞬間、回避中ならジャスト回避として成立させる
     * @param hitPos 演出を出す位置（弾や着弾点）
     * @return 回避中で攻撃を無効化した場合はtrue（呼び出し側はダメージ処理を行わない）
     * @note 1回の回避につき加点・演出は1回だけ。2発目以降は無効化のみ行う
     */
    bool TryJustDodge(const Vector3& hitPos);
    /** @brief ジャスト回避の後に、プレイヤーの周りの敵だけを遅くする（ボスの台本の技と味方は遅くならない） */
    void SlowEnemiesAfterJustDodge();

    // ── アクションの手応え（GamePlaySceneActionFeel.cpp、調整は action_feel.json） ──
    /** @brief アクションの手応え（吹き飛んだ敵どうしの衝突・跳ね返りの演出・部屋の最後の1体）を1フレーム進める */
    void UpdateActionFeel();
    /** @brief 吹き飛んで横へ速く飛んでいる敵か（倒れて打ち飛ばされている敵も含む） */
    bool IsFlyingEnemy(const EnemyEntity& enemy) const;
    /** @brief 吹き飛んだ敵がほかの敵に当たったら、相手にダメージを与えて同じ向きへ飛ばす（連鎖は決めた深さまで） */
    void UpdateFlyingEnemyCollisions();
    /** @brief 壁か床で跳ね返った敵に、跳ね返った所で演出を出す（跳ね返り自体は敵の側で起きる） */
    void UpdateEnemyBounceEffects();
    /** @brief 扉の条件が見ている敵グループの最後の1体を倒した瞬間に、スローと寄りで締める（ボスの部屋は除く） */
    void UpdateRoomClearFinale();
    /** @brief 部屋の最後の1体を倒した所で、スロー・寄り・閃光・音で締める */
    void StartRoomClearFinale(const Vector3& position);
    // パーティクルの更新
    void UpdateParticles(float dt);
    /** @brief UpdateParticles()の下請け 着地ほこりとジャンプ煙のパーティクルを更新する */
    void UpdateLandingAndJumpDustParticles();
    /** @brief UpdateParticles()の下請け 横移動・空中時の残像トレイルをスポーン・経年・削除する */
    void UpdateGhostTrail(float dt);
    /** @brief UpdateParticles()の下請け プレイヤーと敵の接触ヒット判定・ダメージ・演出を処理する */
    void UpdatePlayerEnemyContactHit(float dt);
    /** @brief UpdateParticles()の下請け 敵の弾の発射・飛翔・プレイヤーへの命中時のダメージ・無敵開始・演出を処理する */
    void UpdateEnemyAttackOnPlayer(float dt);
    /** @brief UpdateParticles()の下請け ボスの予告円→着弾ダメージのAoEスラム攻撃を処理する */
    void UpdateBossSlamAttack(float dt);
    /** @brief UpdateParticles()の下請け 格闘/射撃/瞬歩/覚醒ゲージ・覚醒発動・ランク上昇のスタイル演出パーティクルを更新する */
    void UpdateStyleTechniqueParticles(float dt);
    /** @brief UpdateStyleTechniqueParticles()の下請け 格闘コンボヒット時の斬撃・属性パーティクルを発生させる */
    void EmitComboHitParticles(const Vector3& ppos);
    /**
     * @brief 敵に攻撃が当たった位置へ星・リング・火花を出す（どの攻撃が敵に入ったかを敵側で見せる）
     * @param enemyPos 当たった敵の中心位置
     * @param color 演出の色（武器の属性色など）
     * @param strength 演出の大きさ倍率（1.0が通常ヒット。強い技ほど大きく）
     */
    void EmitEnemyHitEffect(const Vector3& enemyPos, const Vector4& color, float strength,
        int extraBurstCount = 0, float ringRadius = 0.0f);
    /** @brief 連続ヒット数に応じた命中演出の盛り度合い（0.0=初撃〜1.0=上限） */
    float HitEscalationRatio() const;
    /** @brief 武器属性ごとに形・運動の異なる追加命中演出を出す */
    void EmitElementalHitEffect(const WeaponData& weapon, const Vector3& enemyPos, int comboStep);
    /**
     * @brief 属性名ごとの追加命中演出を出す（近接・固有技・銃で共通）
     * @param element  属性名（Water/Fire/Lightning/Ice/Gravity/Blood/Void/Wind、それ以外は何も出さない）
     * @param color    属性色
     * @param enemyPos 命中した敵の位置
     * @param scale    演出の大きさ倍率（連続ヒットによる盛りはこの上に掛かる）
     */
    void EmitElementalHitEffect(const std::string& element, const Vector4& color, const Vector3& enemyPos, float scale);
    /** @brief 近接モーション中、武器の先から属性ごとの粒子を零す（火の粉・水滴・冷気など） */
    void EmitWeaponElementAura(const std::string& element, const Vector4& color, const Vector3& weaponPos);
    /** @brief 属性エフェクトに渡す粒子の発生先・乱数・プレイヤーの向きをまとめる */
    ElementEffectContext ElementContext();
    /** @brief 敵が予備動作に入った瞬間に警告リングを出す（攻撃が来ることを事前に伝え、回避を狙えるようにする） */
    void EmitEnemyTelegraphCue(const EnemyEntity* enemy);
    /** @brief UpdateStyleTechniqueParticles()の下請け 銃発射時の弾煙パーティクルを発生させる */
    void EmitGunFireParticles(const Vector3& ppos);
    /** @brief UpdateStyleTechniqueParticles()の下請け 武器固有技（ダガー以外）発動時に技ごとの見た目を出す */
    void EmitWeaponSkillCastParticles(const Vector3& ppos);
    /**
     * @brief 固有技が決まった瞬間の共通演出（閃光・停止・揺れ・刃の明滅・大きな弾け）を当たり外れに関係なく出す
     * @param center    演出の中心（技の着弾点）
     * @param color     武器の属性色
     * @param slashLine 進行方向へ画面を横切る一閃を引くか（居合系の突進技用）
     */
    void EmitWeaponSkillImpact(const Vector3& center, const Vector4& color, bool slashLine);
    /** @brief UpdateStyleTechniqueParticles()の下請け 覚醒中の継続オーラと発動瞬間の衝撃波を発生させる */
    void EmitAwakenParticles(const Vector3& ppos, float dt);
    /** @brief UpdateStyleTechniqueParticles()の下請け styleRankHud_のランクが上がった瞬間にリング・火花・カメラシェイク・画面フラッシュを出す */
    void EmitStyleRankUpParticles(const Vector3& ppos);
    /** @brief 近接コンボのモーション中、装備武器の色で手元にトレイル残像を発生させ続ける */
    void UpdateWeaponTrail();
    /** @brief 攻撃していない間に武器へ属性をまとわせるか（覚醒中か、スタイルランクが高い時に確率で） */
    bool ShouldEmitIdleAura();
    /**
     * @brief 当てた敵に属性の状態（燃焼・凍結など）を付ける。同じ属性なら残り時間を延ばし、違う属性なら上書きする
     * @param enemy   当たった敵（nullptr なら何もしない）
     * @param element 属性名
     * @param color   演出の色
     */
    void ApplyElementStatus(EnemyEntity* enemy, const std::string& element, const Vector4& color);
    /** @brief ボスと道中の敵に残っている属性の状態を進める（継続ダメージ・歩く速さ・体から出る粒） */
    void UpdateElementStatuses(float dt);
    /** @brief 敵1体の属性の状態を進める */
    void UpdateElementStatus(EnemyEntity& enemy, float dt);
    /** @brief 武器を手に入れた瞬間に、胸の高さへ属性の粒を出す（武器名は出さない） */
    void PlayWeaponAcquireEffect(WeaponType type);
    /**
     * @brief 枠が満杯の時に奪った武器を、今持っている武器に合体させる
     * @param result 奪った時の Acquire() の結果
     * @param type 奪った武器の種類
     * @return 合体させたら true（交換や覚醒ゲージへの転用などのふだんの後処理はしない）
     */
    bool TryFuseAcquiredWeapon(WeaponManager::AcquireResult result, WeaponType type);
    /**
     * @brief ボスから奪った武器をボスの枠へ入れる（空なら入れ、同じなら覚醒ゲージへ、別の種類なら主軸を選ぶ画面を出す）
     * @param type ボスから奪った武器の種類
     */
    void AcquireBossWeapon(WeaponType type);
    /** @brief 合体の主軸を選ぶ画面の入力（左右で選び、決定で決める。ゲームは止めておく） */
    void UpdateBossMainChoice();
    /** @brief 合体の主軸を選ぶ画面を描く */
    void DrawBossMainChoice();
    /** @brief ボスの枠の武器の効きの落ちの添字（銃の次） */
    int BossFatigueIndex() const;
    bool bossChoiceKeepCurrent_ = true; ///< 主軸を選ぶ画面で、今の主軸のままを選んでいるか
    /**
     * @brief 武器が合体した瞬間の演出（2本の色の輪・閃光・両方の属性の粒・合体した名前）
     * @param slot 合体した枠
     * @param bossWeapon ボスの武器どうしの合体か
     */
    void PlayWeaponFusionEffect(int slot, bool bossWeapon);
    /** @brief 合体している武器の近接・固有技のダメージ倍率（合体していなければ 1） */
    float FusionDamageMult() const;
    /**
     * @brief 合体している武器で当てた時に、相棒の属性の演出と状態を足し、耐久を減らす（尽きたら砕ける演出）
     * @param enemy 当たった敵
     * @param scale 属性演出の大きさ倍率
     */
    void ApplyFusionHit(EnemyEntity* enemy, float scale);
    /**
     * @brief 合体した武器が砕けた瞬間の演出
     * @param weapon 砕けた武器
     * @param partner 合体していた相棒の武器
     */
    void PlayWeaponFusionBreakEffect(const WeaponData& weapon, const WeaponData& partner);
    /**
     * @brief 画面の中央上に名前を一瞬出す（武器を合体させた時に使う）
     * @param text  出す名前
     * @param color 武器の演出色（読みやすいように白を混ぜて出す）
     */
    void ShowWeaponBanner(const std::wstring& text, const Vector4& color);
    /** @brief 武器名の表示の残り時間を進める */
    void UpdateWeaponAcquireBanner(float dt);
    /** @brief 手に入れた武器の名前を画面の中央上に出す（出てすぐ大きく弾け、最後に薄く消える） */
    void DrawWeaponAcquireBanner();
    /** @brief 刃の軌跡・締め演出・F6調整パネルを更新する @param gameAdvanced このフレームにプレイヤーが動いたか */
    void UpdateComboFeel(bool gameAdvanced);
    /** @brief 撃ったフレームに、ボス・道中の敵・壊せる物から銃口に一番近い1体を射撃の的に選ぶ */
    void BuildGunShotTarget();
    // フィニッシャースラッシュ演出（斬撃線を1本ずつ表示→本命ヒット）の更新
    void UpdateFinisherSlash(float dt);
    /** @brief フィニッシャーのどれかの段階の最中か（溜め・斬撃線・解放の画面の割れ・敵の破片）。とどめやクリアはこれが終わってから始める */
    bool IsFinisherBusy() const;
    // 敵撃破などのクリア条件判定
    void CheckClearCondition();

    /** @brief ラン外（エディタのテストプレイなど）のクリアか（結果画面は CLEAR! だけにして、終わったらクリア画面へ） */
    bool IsSandboxClear() const;
    /** @brief ラン中のクリアの結果（ランク・スコア）を求め、apply なら進行とセーブにも反映する（確認用の再生は false） */
    void RecordRunClear(bool apply);

    /** @brief Draw()の下請け クリア演出中の専用画面（ローグライト結果表示／サンドボックスのガラス割れ導入）を描画する。描画してDraw()を打ち切るべきなら true を返す */
    bool DrawClearOverlayIfNeeded();
    /** @brief Draw()の下請け 3Dワールド（地形・残像・プレイヤー/敵・パーティクル・空間歪み）を描画する */
    void DrawWorldAndActors();
    /** @brief Draw()の下請け エディタUI・フィニッシャー演出・フォントなど2D上乗せ描画をまとめて行う */
    void DrawOverlaysAndUI();
    /** @brief HPバー・ランク・武器欄・エディタ配置UIなどHUDのスプライトを描く（一時停止中は呼ばない） */
    void DrawHudSprites();

    DirectXCommon* dxCommon_ = nullptr;
    Input* input_ = nullptr;
    Audio* audio_ = nullptr;

    SrvManager* srvManager_ = nullptr;
    GrayscaleEffect* grayscaleEffect_ = nullptr;
    ImageFilter* imageFilter_ = nullptr;
    HsvFilter* hsvFilter_ = nullptr;
    ParticleManager* pm_ = nullptr;

    std::unique_ptr<SpriteCommon> spriteCommon_;
    HudLayer hud_;

    std::unique_ptr<ModelCommon> modelCommon_;
    std::unique_ptr<Object3dCommon> objectCommon_;
    std::unique_ptr<ShadowManager> shadowManager_;
    std::unique_ptr<Camera> camera_;

    std::unique_ptr<Skydome> skydome_;

    std::unique_ptr<Player> player_;
    // ボス敵。実体はStageEditorの配置物(kind=="enemy_basic", isStageBoss=true)が所有し、
    // OnEditorLevelLoaded()でポインタだけを受け取る（非所有）
    EnemyEntity* enemy_ = nullptr;
    WeaponType bossWeaponType_ = WeaponType::kSword; ///< ボスの配置物に設定された武器種別（撃破後にこれを奪う）
    // ボス頭上のHPバー（BattleTestSceneのDummy::hpBarBg/Fgと同じ体裁、GamePlayScene初期化時に一度だけ生成）
    const EnemyEntity* selectedStealOwner_ = nullptr; ///< このフレームに奪う相手（SelectStealTarget で決める）
    bool hudReady_ = false; ///< 通常HUDの位置と色を更新し終えたか（済むまで描画しない）
    std::unique_ptr<Sprite> bossHpBarBg_;
    std::unique_ptr<Sprite> bossHpBarFg_;

    // プレイヤーHPゲージ（左下、数値表示に加えて一目で残量が分かるようにするバー）
    std::unique_ptr<Sprite> playerHpBarBg_;
    std::unique_ptr<Sprite> playerHpBarFg_;

    /** @brief 道中に配置された武器持ち敵1体分の状態（撃破後、Jキーで吸収して武器を奪取する） */
    /** @brief モンスター1体の個性の動きの途中の状態（GamePlaySceneMonsters.cpp） */
    struct MonsterTraitState {
        /** @brief 動きの段階 */
        enum class Phase { kIdle, kTelegraph, kSwoopDown, kSwoopUp };
        Phase phase = Phase::kIdle;
        float timer = 0.0f; ///< 待つ間は次の動きまで、身構える間は残りの秒数
        float pulse = 0.0f; ///< 身構えの輪を次に出すまでの秒数
        float elapsed = 0.0f; ///< 急降下の今の段階の経過秒数
        Vector3 from {}; ///< 急降下を始めた位置（戻る先）
        Vector3 to {}; ///< 急降下で狙う位置
        bool struck = false; ///< この急降下でもう当たったか
    };
    /** @brief 道中の武器を持った敵1体ぶんの登録内容 */
    struct WeaponEnemyEntry {
        /** @brief 登録時の敵の識別番号 */
        uint64_t instanceId = 0;
        EnemyEntity* enemy = nullptr; // 非所有。StageEditorの配置物が実体を所有する
        WeaponType weaponType = WeaponType::kSword;
        bool hasWeapon = true; ///< falseなら武器を持たないモンスター（倒すと消えて覚醒ゲージになる）
        float vanishTimer = 0.0f; ///< モンスターが倒れてから消えるまでの残り秒数
        bool weaponAcquired = false; ///< 武器を奪い終えた（モンスターは消え終えた）
        bool absorbing = false;
        bool defeatEffectEmitted = false;
        float absorbTimer = 0.0f;
        std::unique_ptr<Sprite> hpBarBg; // OnEditorLevelLoaded()で生成（レベル再読込のたびに作り直す）
        std::unique_ptr<Sprite> hpBarFg;
        bool hpBarPlaced = false; ///< HPバーの位置を一度でも決めたか（決める前に描くと画面の右上に白い四角が映る）
        MonsterTraitState trait; ///< モンスターの個性の動きの状態
    };
    std::vector<WeaponEnemyEntry> weaponEnemies_;
    /** @brief 道中のモンスターの個性の動きを1フレーム進める（毎フレーム呼ぶ） */
    void UpdateMonsterTraits(float dt);
    /** @brief モンスター1体の個性の動きを進める（待つ → 身構える → 動く） */
    void UpdateMonsterTrait(WeaponEnemyEntry& entry, const MonsterTraitData& data, float dt);
    /** @brief 身構え終わったモンスターの動きを出す */
    void PerformMonsterTrait(WeaponEnemyEntry& entry, const MonsterTraitData& data);
    /** @brief 急降下と戻りを進める */
    void UpdateMonsterSwoop(WeaponEnemyEntry& entry, const MonsterTraitData& data, float dt);

    /** @brief 道中の武器敵1体への命中1回ぶんの内訳（何で当てたかと、そこから決まる反応・演出の強さ） */
    struct WeaponEnemyHit {
        bool hit = false; ///< 何かが当たった
        bool melee = false; ///< 近接コンボで当てた
        bool gun = false; ///< 銃（スピン連射を含む）で当てた
        bool skill = false; ///< 武器固有技で当てた
        bool fired = false; ///< このフレームに撃った弾で当てた
        bool extraHit = false; ///< 覚醒中の多段ヒットの2発目以降
        bool comboFinisher = false; ///< コンボの締めの段
        bool blowAway = false; ///< 大きく吹き飛ばす
        const MeleeAttackDef* attack = nullptr; ///< 当てた近接の段（無ければ nullptr）
        float fatigueMult = 1.0f; ///< 武器の疲労によるダメージ倍率
    };
    /**
     * @brief 道中の武器敵に、このフレームの近接・射撃・固有技のどれが当たったかを調べる
     * @param entry 調べる武器敵
     * @param enemyBounds 武器敵の当たり判定
     */
    WeaponEnemyHit DetectWeaponEnemyHit(const WeaponEnemyEntry& entry, const AABB& enemyBounds) const;
    /**
     * @brief 命中した武器敵にダメージ・吹き飛び・のけぞりを与え、演出を出す
     * @param entry 当たった武器敵
     * @param hit DetectWeaponEnemyHit() の結果（反応の強さを書き足す）
     */
    void ApplyWeaponEnemyHit(WeaponEnemyEntry& entry, WeaponEnemyHit& hit);
    /** @brief 道中の敵への命中1回ぶんから、何で当てたかに合う属性の状態を付ける（効きが落ちた武器では付けない） */
    void ApplyWeaponEnemyElementStatus(EnemyEntity& enemy, const WeaponEnemyHit& hit);
    /** @brief 道中の敵へのこのフレームの命中を、種類ごとに一度だけスタイルへ加える */
    void AwardWeaponEnemyStyle(const WeaponEnemyHit& hit, bool& meleeAwarded, bool& skillAwarded, bool& gunAwarded);
    /** @brief 命中時のダメージ量を武器・段・固有技から計算する */
    int ComputeWeaponEnemyDamage(const WeaponEnemyHit& hit) const;
    /**
     * @brief 命中時のパーティクル・属性演出・ヒットストップを出す
     * @param enemyPos 当たった敵の位置
     * @param hit 命中の内訳
     */
    void EmitWeaponEnemyHitEffects(const Vector3& enemyPos, const WeaponEnemyHit& hit);
    /** @brief 武器を持たないモンスターが倒れたら、少し待って弾けて消し、覚醒ゲージにする */
    void UpdateMonsterVanish(WeaponEnemyEntry& entry);
    /** @brief 倒れた武器持ち敵の武器を、奪取ボタンで吸い寄せて手に入れる */
    void UpdateWeaponSteal(WeaponEnemyEntry& entry);

    /** @brief ロックオン中の敵（ボスも道中の敵も同じ扱い。ロックオン長押し中は最寄りの敵を自動追従し、離すとnullptr） */
    const EnemyEntity* lockedEnemy_ = nullptr;
    /** @brief 乱舞の相手（乱舞を始める時に選び、終わるまで同じ敵を追う。使う前に IsCombatEnemy() で確かめる） */
    EnemyEntity* rampageEnemy_ = nullptr;

    // 収集物（pickup）と壊せる物（breakable）はレベルJSONの配置物としてStageEditorが所有する。
    // ここでは壊せる物の脈動表示に使うタイマーだけを持つ
    float explosiveBarrelPulse_ = 0.0f;


    Vector4 skyColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    float skyRotOffsetY_ = 0.0f;

    std::unique_ptr<RenderTexture> renderTexture_;
    std::unique_ptr<Sprite> renderTextureSprite_;

    // カメラスムージング
    Vector3 cameraTargetPos_ = { 14.5f, 6.0f, -24.0f };
    Vector3 cameraTargetRot_ = { 0.0f, 0.0f, 0.0f };
    std::deque<Vector3> cameraPosHistory_;
    std::deque<Vector3> cameraRotHistory_;
    int cameraSmoothFrames_ = 1;

    SceneEditor sceneEditor_;

    float hitCooldown_ = 0.0f;
    std::mt19937 rng_ { std::random_device { }() };


    // 敵の遠隔攻撃弾ボスと道中の武器持ち敵が共用する（発射時にプレイヤーが射程内にいる敵だけ撃つ）
    // 弾速・寿命・射程はResources/Config/enemy_params.jsonのbullet節（EnemyTuning::Bullet()）で持つ

    /** @brief 敵の遠隔攻撃弾1発分の状態 */
    struct EnemyBullet {
        Vector3 pos = { };
        Vector3 vel = { };
        float timer = 0.0f;
        int damage = 0;
        float gravity = 0.0f; ///< 下向きの加速度（山なりに飛ぶ骨など。0ならまっすぐ）
    };
    std::vector<EnemyBullet> enemyBullets_;
    /** @brief 敵がまとめて撃つ弾の中身（モンスターの火の玉・骨と、ボスの投げで共通） */
    struct EnemyVolleyShot {
        int count = 1; ///< 一度に出す数
        float spreadDegrees = 0.0f; ///< 扇状に広げる角度（隣どうしの差）
        float speed = 6.0f; ///< 速さ（山なりの時は横の速さ）
        float riseSpeed = 0.0f; ///< 山なりの時の上向きの速さ
        float gravity = 0.0f; ///< 落ちる加速度（0ならまっすぐ狙う）
        float lifetime = 2.0f; ///< 飛んでいる秒数
        int damage = 1; ///< 当たった時のダメージ
    };
    /** @brief origin から target へ向けて弾をまとめて出す（gravity が 0 なら扇状に狙い、そうでなければ山なりに投げる） */
    void SpawnEnemyVolley(const Vector3& origin, const Vector3& target, const EnemyVolleyShot& shot);
    /** @brief ボスが地面を叩いて出した、床沿いに走る衝撃波1つ */
    struct BossWave {
        float x = 0.0f; ///< 今の横の位置
        float y = 0.0f; ///< 走っている床の高さ（ボスの足元）
        float dirX = 1.0f; ///< 走る向き
        float traveled = 0.0f; ///< 走った距離
        float sparkTimer = 0.0f; ///< 次に土煙を出すまでの秒数
        bool struck = false; ///< もうプレイヤーに当たったか
    };
    std::vector<BossWave> bossWaves_; ///< 走っている衝撃波
    /** @brief 衝撃波を進め、プレイヤーに当たったらダメージ（毎フレーム呼ぶ） */
    void UpdateBossWaves(float dt);
    /** @brief 渦で引き寄せている間の1フレーム（土煙を吸い込ませ、プレイヤーも中心へ寄せる） */
    void UpdateBossVortexPull(float dt, const Vector3& bossPosition, float progress);
    /** @brief 渦が弾けた瞬間（中心のまわりにダメージを出し、吸い込んだ粒を吹き飛ばす） */
    void BurstBossVortex(const Vector3& bossPosition);
    /** @brief 跳びかかりの着地の周りにダメージと演出を出す */
    void LandBossLeap(const Vector3& position);
    /** @brief ボスがふだんの動きで壁にめり込むか足場の外へ出たら、最後に立てていた所へ戻す */
    void KeepStageBossOnGround();

    // ボスの予告円→着弾のAoEスラム攻撃(戦闘演出ギミック)
    // 予告秒数・半径・ダメージはenemy_params.jsonのbossSlam節（EnemyTuning::BossSlam()）、出す時機はボス戦の進行役が決める
    bool bossSlamWarningActive_ = false;
    float bossSlamWarningTimer_ = 0.0f;
    Vector3 bossSlamTargetPos_ = { };
    // 着弾地点に出す予告円（円形グロー画像を着弾範囲の見かけ半径まで拡大し、赤く点滅させる）
    std::unique_ptr<Sprite> bossSlamWarningSprite_;

    /** @brief 覚醒中の残像トレイル1コマ分の位置と経過秒数（設定された寿命を超えたら消える） */
    struct GhostEntry {
        Vector3 pos; ///< 本体の見た目と同じ置き場所（足元合わせ済み）
        float scale; ///< 本体の見た目と同じ倍率
        float yaw; ///< 残した時の向き
        float age;
    };
    std::deque<GhostEntry> ghostTrail_;
    std::unique_ptr<Object3d> ghostObject_;
    KickedBombPool kickedBombs_; ///< 攻撃で吹き飛ばした爆弾（見た目は初期化で作って使い回す）
    /** @brief 自動テストプレイの見張りが、フレームをまたいで覚えておく様子 */
    struct BugWatchState {
        int framesUntilCheck = 0; ///< 次に調べるまでのフレーム数（毎フレームは重いので間を空ける）
        bool wasExpectingProgress = false; ///< 前に調べた時、先へ進む操作をしていたか
        float bestX = 0.0f; ///< 先へ進む操作を始めてから一番右まで行った位置
        float stallSeconds = 0.0f; ///< 一番右の位置が伸びないまま過ぎた秒数
        Vector3 lastGroundedPosition {}; ///< プレイヤーが最後に床に立っていた位置（床抜けの報告に添える）
        Vector3 lastPlayerPosition {}; ///< 前に調べた時のプレイヤーの位置（高い壁や閉じた扉をすり抜けていないか調べる）
        bool hasLastPlayerPosition = false; ///< lastPlayerPosition が入っているか
        uint32_t teleportCount = 0; ///< 前に調べた時の、手順がプレイヤーを移した回数（増えていたら前の位置と比べない）
        std::vector<std::string> playerTrail; ///< 直前のプレイヤーの様子（古い順。床抜けの報告に添える）
        std::vector<std::pair<const EnemyEntity*, uint64_t>> defeatedSeen; ///< 倒されたのを見た敵（生き返りを見つける）
        /** @brief 敵1体の、前に調べた時の様子（理由の分からない大きな移動を見つける） */
        struct EnemyLastSeen {
            Vector3 position {}; ///< 位置
            bool fastMoveExpected = false; ///< 速く動いて当然の状態だったか（ボス・吹き飛び中・台本で動いている）
        };
        std::map<uint64_t, EnemyLastSeen> enemyLastPositions; ///< 敵の番号 → 前に調べた時の様子
        std::vector<std::pair<std::string, float>> suspectSeconds; ///< おかしな様子が続いている秒数（一瞬だけのものは数えない）
    };
    BugWatchState bugWatch_; ///< 自動テストプレイの見張りの様子
    /** @brief 敵グループの全滅を待つ条件1つの、前のフレームまでの様子（部屋の最後の1体を見つける） */
    struct RoomClearWatch {
        bool initialized = false; ///< 一度でも様子を見たか（最初に見た時は演出を出さない）
        bool met = false; ///< 前に見た時に条件が成立していたか
        std::map<std::string, bool> defeated; ///< 敵の名前 → 前に見た時に倒れていたか
        bool hasLastDefeated = false; ///< 最後に倒れた敵を覚えているか
        std::string lastDefeatedName; ///< 最後に倒れた敵の名前
        Vector3 lastDefeatedPosition {}; ///< 最後に倒れた敵が倒れた位置
    };
    std::map<std::string, RoomClearWatch> roomClearWatches_; ///< 条件の名前 → 様子
    std::map<const EnemyEntity*, std::vector<const EnemyEntity*>> flyerHits_; ///< 吹き飛んでいる敵 → もう当てた敵（同じ相手に2回当てない）
    std::map<const EnemyEntity*, int> flyerChain_; ///< 吹き飛んでいる敵 → 何段目の連鎖で飛ばされたか
    ThrownWeaponPool thrownWeapons_; ///< 締めで投げた武器（見た目は初期化で作って使い回す）
    bool weaponFinisherPending_ = false; ///< このフレームに締めの動作を出すか（ボスへの通常の当たりの後で出す）
    BossBattleDirector bossDirector_; ///< ステージボス戦の進行役（名乗り・第2段階・武器ごとの技）
    BossMotionPlayer bossMotion_; ///< ステージボスの技ごとの動きの再生役（boss_motion.json）
    FinalBossHud stageBossHud_; ///< ステージボスの画面下の体力バーと名乗り（ラスボスと同じ見せ方）
    bool bossSlamRequested_ = false; ///< 進行役が叩きつけを頼んだか（予告が始まったら下ろす）
    float bossWarningPulse_ = 0.0f; ///< 身構えの輪を次に出すまでの秒数
    float bossTrailTimer_ = 0.0f; ///< 走っている間の火花を次に出すまでの秒数
    float bossVortexDebrisTimer_ = 0.0f; ///< 渦のまわりの土煙を次に出すまでの秒数
    std::optional<Vector3> bossSafePosition_; ///< ボスが最後に立てていた所（追いかけて足場の外へ出た時に戻す）
    float bossVortexWarningTimer_ = 0.0f; ///< 渦の中心の輪を次に出すまでの秒数
    float ghostSpawnTimer_ = 0.0f;

    float auraTimer_ = 0.0f;
    float styleMeter_ = 0.0f;
    float peakStyle_ = 0.0f;
    float floorElapsedSeconds_ = 0.0f; ///< このフロアに入ってからの経過秒数（OnEditorLevelLoaded()でリセット、プレイログ用）
    /** @brief 右上のスタイリッシュランクHUD（採点はstyleMeter_側で行い、表示だけこれに委ねる） */
    StyleMeter styleRankHud_;
    std::wstring acquireBannerText_; ///< 手に入れた武器の名前（炎の剣 など）
    Vector4 acquireBannerColor_ = { 1.0f, 1.0f, 1.0f, 1.0f }; ///< 武器名の色（武器の演出色）
    float acquireBannerTimer_ = 0.0f; ///< 武器名を出している残り秒数（0なら出さない）
    StageIntro stageIntro_; ///< ステージ開始演出（Resources/Config/stage_intro.json）
    StageControlsGuide stageGuide_; ///< 1面の最初に一度だけ出す操作説明（Resources/Config/controls_guide.json）
    bool stageIntroStarted_ = false; ///< エディタでレベルを読み直しても演出をやり直さないための印
    PlayerDefeat playerDefeat_; ///< プレイヤーのやられ演出（Resources/Config/player_defeat.json）
    BossExecution bossExecution_; ///< 膝をついたボスへのとどめの演出（Resources/Config/boss_execution.json）
    FinalBossBattle finalBoss_; ///< ラスボス戦の進行役（Resources/Config/final_boss.json）
    int lastTechniqueId_ = -1;
    int repeatedTechniqueCount_ = 0;
    /** @brief 武器ごとの疲労（0〜1）。添字は近接武器のWeaponManager::GetList()順、末尾が銃 */
    std::vector<float> weaponFatigue_;
    int lastFatigueIndex_ = -1; ///< 最後に当てた武器の添字（この武器だけは疲労が抜けない）
    int pendingFatigueIndex_ = -1; ///< このフレームに当てた武器の添字（複数の敵に同時に当てても1回ぶんにする）
    float pendingFatigueAmount_ = 0.0f;
    float fatigueIdleTimer_ = 0.0f; ///< 最後に何かへ当ててからの経過秒数
    /** @brief 途切れずに当て続けたヒット数（雑魚・ボス・銃・近接を問わない。命中演出を段々派手にする） */
    int hitEscalationCount_ = 0;
    float hitEscalationTimer_ = 0.0f; ///< 連続ヒットが途切れるまでの残り秒数

    // フィニッシャースラッシュ演出の進行状態
    bool finisherActive_ = false;
    int finisherLineIdx_ = 0;
    float finisherBeatTimer_ = 0.0f;
    std::vector<finisherEffects::BeatSlash> finisherSlashes_; ///< 刻んだ斬撃線（解放の瞬間に、この線に沿って画面を割る）

    /** @brief 大技演出中の画面暗転オーバーレイ */
    std::unique_ptr<Sprite> finisherOverlay_;

    /** @brief 解放時に敵本体を切断破片へ差し替える演出 */
    MeshSliceEffect enemySlice_;

    /** @brief 空間に走るガラス質の刃パーティクル */
    BladeFlashEffect bladeFlash_;
    /** @brief 近接コンボの手応え演出（刃の軌跡・締めの重さ・振りの向きの斬撃線、F6で調整） */
    ComboFeelDirector comboFeel_;
    /** @brief 射撃が当たる相手（銃口に一番近い1体。弾が敵を貫通しないようにする） */
    ShotTargetPicker gunShotPicker_;

    /** @brief 敵中心の空間歪み（レンズ歪み+色収差） */
    SpaceDistortionEffect spaceWarp_;

    bool showResult_ = false; ///< フロアクリアの結果（スコア・セーブ）を反映し終えたか
    int lastScore_ = 0; ///< 直近のフロアクリアで加算したスコア（結果表示用）
    int lastPickupBonus_ = 0; ///< 直近のクリアで加えたエネルギーコア全回収のボーナス（結果表示用。無ければ0）
    int lastPickupCollected_ = 0; ///< 直近のクリアまでに集めたエネルギーコアの数（結果表示用）
    int lastPickupTotal_ = 0; ///< 直近のクリアしたステージに置かれたエネルギーコアの数（0なら結果に出さない）
    std::wstring scriptMessage_; ///< ShowMessage ノードが出した文字
    float scriptMessageSeconds_ = 0.0f; ///< その文字を出しておく残り秒数
    Vector3 scriptCameraFocus_ = {}; ///< FocusCamera ノードが向けた先
    float scriptCameraFocusSeconds_ = 0.0f; ///< カメラを向けておく残り秒数（0ならプレイヤーを追う）
    bool scriptControlLocked_ = false; ///< LockPlayerControl ノードで操作を止めているか

    FontRenderer fontRenderer_;
    CameraShaker cameraShaker_;

    GlassShatterEffect glassShatter_;

    /** @brief 解放時に暗転+斬撃線ごと凍った画面を砕いて素の世界を見せる演出 */
    GlassShatterEffect finisherShatter_;

    bool clearTriggered_ = false;
    bool weaponStealTriggered_ = false;
    bool pendingWeaponMandatory_ = false; ///< 交換待ちの武器が破棄できないボス武器か
    bool mainEnemyDefeatEffectEmitted_ = false;
    bool mainWeaponAbsorbing_ = false;
    float mainWeaponAbsorbTimer_ = 0.0f;
    bool requestClear_ = false;
    bool glassShatterDebugTest_ = false; ///< 確認用の再生か（結果は見せるが、進行とセーブに反映しない）
    bool bossFinishedByGun_ = false; ///< ボスを倒した最後の一撃が銃だったか（クリアの決め演出の見せ方を変える）
    bool bossFinishedByBomb_ = false; ///< ボスへのとどめが爆弾の型だったか（クリアの決め演出を爆発にする）
    bool bossHitByMelee_ = false; ///< ボスに近接武器（段か固有技）を当てたことがあるか
    WeaponType bossLastMeleeWeapon_ {}; ///< ボスに最後に当てた近接武器（とどめの型と姿勢をこの武器で決める）
    std::string executionBombName_; ///< とどめの演出で爆発させる、ボスにめり込んだ爆弾の名前（爆弾の型でなければ空）
    ClearFinish clearFinish_; ///< クリアの決め演出（Resources/Config/clear_finish.json）

    ResultBackdrop resultBackdrop_; ///< クリアの結果画面の背景（ランクの色の帯・透かし・漂う破片）

    // 一時停止メニュー（Pauseキーで開閉、ゲームプレイ中限定。クリア演出中・武器交換中は開けない）
    PauseMenu pauseMenu_;
    ComboMotionEditor motionEditor_; ///< F7 で開く、コンボの段ごとのモーションの調整（開いている間はゲームを止める）
};

} // namespace engine::game

/**
 * @file MyGame.h
 * @brief Frameworkを継承し、ゲーム固有の進行（初期化・更新・描画）を記述するファイル
 */
#pragma once
#include "Object3dRenderQueue.h"
#include "BaseScene.h"
#include "EngineConstants.h"
#include "FixedStepClock.h"
#include "Framework.h"
#include "SceneFactory.h"
#include "SmokeTestRunner.h"
#include "SoundAssignEditor.h"
#include "PostProcessOrderEditor.h"
#include <cstdint>
#include <memory>
namespace engine {
using engine::game::AbstractSceneFactory;
using engine::game::BaseScene;
using engine::game::SceneFactory;

/**
 * @brief ゲーム全体の具体的な進行を管理するクラス
 * @note Framework クラスを継承し、ゲーム固有のライフサイクルを実装します
 * シーンファクトリを用いてシーンを生成し、シーン遷移の制御を行います
 */
class MyGame : public Framework {
public:
    /**
     * @brief ゲーム固有の初期化処理
     * @note 基盤システムの初期化後、シーンファクトリの生成や
     * 最初のシーン（タイトルシーン等）のセットアップを行います
     */
    void Initialize() override;

    /**
     * @brief ゲーム固有の終了処理
     * @note 実行中のシーンの解放や、ゲーム全体で使用したリソースの破棄を行います
     */
    void Finalize() override;

    /**
     * @brief ゲーム固有の更新処理
     * @note 現在のシーンの更新処理を呼び出します
     * また、シーンの終了フラグを監視し、シーンの切り替え（遷移）を制御します
     */
    void Update() override;

    /**
     * @brief ゲーム固有の描画処理
     * @note 現在のシーンの描画関数を呼び出し、モデルやスプライトなどの描画コマンドを積み込みます
     */
    void Draw() override;

private:
    /** @brief ゲームを1回分進める（Update が速回しの時に何回か呼ぶ） */
    void UpdateOnce();
    /** @brief シーン生成を担う抽象ファクトリのスマートポインタ */
    std::unique_ptr<AbstractSceneFactory> sceneFactory_;
    /** @brief F8 で開閉するエンジン共通の実行時診断パネルを表示しているか */
    bool showEngineDiagnostics_ = false;
    uint32_t lastDrawnObjects_ = 0; ///< 前のフレームに描いた 3D の物の数（診断パネルに出す）
    uint32_t lastCulledObjects_ = 0; ///< 前のフレームに画面外なので描かなかった 3D の物の数
    engine::graphics::Object3dRenderQueue::Stats lastQueueStats_ {}; ///< 前のフレームに並べて描いた配置物の数（命令の数・まとめ描きの数）
    bool fastSmokeTest_ = false; ///< 自動テストを速回ししているか（--realtime なしで起動した時）
    /** @brief 効果音・BGMの割り当て画面（F8のパネルから開く） */
    engine::game::SoundAssignEditor soundAssignEditor_;
    /** @brief 画面全体へ掛けるエフェクトの順番を変えるエディタ（post_process.json） */
    engine::editor::PostProcessOrderEditor postProcessEditor_;
    bool showPostProcessEditor_ = false; ///< 画面の効果の順番のエディタを開いているか
    /** @brief 音の割り当て画面を表示しているか */
    bool showSoundAssignEditor_ = false;
    /** @brief 自動テストプレイ（--smoke-test で起動した時だけ動く） */
    engine::game::SmokeTestRunner smokeTest_;
    /** @brief 描画が遅れた時に、遅れた分だけ更新を回すための時計 */
    engine::FixedStepClock frameClock_ { engine::engineConstants::kFrameDeltaTime };
};

} // namespace engine

/**
 * @file SceneManager.h
 * @brief シーンの切り替え、更新、描画を一括管理するクラスを定義するファイル
 */
#pragma once
#include "SceneTransitionOverlay.h"
#include "EditorGamePreview.h"
#include "AbstractSceneFactory.h"
#include "Audio.h"
#include "BaseScene.h"
#include "DirectXCommon.h"
#include "ImGuiManager.h"
#include "Input.h"
#include "SpriteCommon.h"
#include "FontRenderer.h"
#include <Fade.h>
#include <memory>
#include <cstdint>
#include <string>
#include <vector>
namespace engine::game {
using engine::Audio;
using engine::DirectXCommon;
using engine::Input;
using engine::graphics::Fade;
using engine::graphics::ImGuiManager;
using engine::graphics::SpriteCommon;

/**
 * @brief シーン運用を統括するマネージャークラス
 * @note シングルトンパターンを採用しており、どこからでも ChangeScene() を呼び出して
 * シーン遷移を予約できます実際の切り替えは Update() の冒頭で安全に行われます
 */
class SceneManager {
public:
    /**
     * @brief SceneManagerの唯一のインスタンスを取得する
     * @return SceneManager* シングルトンインスタンスへのポインタ
     */
    static SceneManager* GetInstance();

    /**
     * @brief シーンマネージャーの初期化
     * @param dxCommon DirectX基盤のポインタ
     * @param input 入力管理のポインタ
     * @param audio 音響管理のポインタ
     * @param imgui ImGui管理のポインタ
     * @note 各シーンで共有する基盤システムのポインタを保持します
     */
    void Initialize(DirectXCommon* dxCommon, Input* input, Audio* audio, ImGuiManager* imgui);

    /**
     * @brief 終了処理
     * @note 実行中のシーンを破棄し、後片付けを行います
     */
    void Finalize();

    /**
     * @brief シーンの更新処理
     * @note 次のシーンが予約されている場合は切り替え処理を行い、
     * その後現在アクティブなシーンの Update() を呼び出します
     */
    void Update();

    /**
     * @brief シーンの描画処理
     * @note 現在アクティブなシーンの Draw() を呼び出します
     */
    void Draw();

    /** @brief ポストエフェクトの後に画面遷移を重ねる */
    void DrawTransition();
    /** @brief 編集中のゲーム映像を小窓用テクスチャに転送する */
    void CaptureEditorPreview();
    /** @brief 次回のDebug起動で表示するシーン */
    const std::string& GetDebugStartScene() const {
        return debugStartScene_;
    }
    /** @brief Debug開始シーンを設定ファイルへ保存する。失敗時はfalse */
    bool SetDebugStartScene(const std::string& sceneName);
    /** @brief Debug の開始シーンとして選べる数を返す */
    static int GetDebugStartSceneCount();
    /** @brief 指定した番号の開始シーンの名前を返す */
    static const char* GetDebugStartSceneName(int index);
    /** @brief 指定した番号の開始シーンのエディタでの表示名を返す */
    static const char* GetDebugStartSceneLabel(int index);
    /** @brief 名前から開始シーンの番号を探す（一覧に無ければ -1） */
    static int FindDebugStartSceneIndex(const std::string& sceneName);
    /** @brief エディタに出すシーンの表示名を返す（一覧に無いシーンは名前そのもの） */
    static std::string SceneLabelFor(const std::string& sceneName);
    /**
     * @brief エディタからシーンを開く（ラン中でないと開けない画面なら、先に新しいランを始める）
     * @param sceneName 開くシーンの名前
     */
    void OpenSceneFromEditor(const std::string& sceneName);
    /** @brief 1つ前に開いていたシーンへ戻れるかを返す */
    bool CanGoBack() const {
        return !sceneHistory_.empty();
    }
    /** @brief 1つ前に開いていたシーンの名前を返す（無ければ空） */
    std::string PreviousSceneName() const {
        return sceneHistory_.empty() ? std::string() : sceneHistory_.back();
    }
    /** @brief 1つ前に開いていたシーンへ戻る（戻った分は履歴から消える） */
    void GoBack();

    static constexpr float kDefaultFadeSeconds = 0.38f; // シーン切り替えの既定のフェード秒数

    /**
     * @brief 次のシーンを予約する
     * @param sceneName 生成したいシーンの名前（例: "TITLE", "GAMEPLAY"）
     * @note この関数を呼ぶと、次フレームの Update() の冒頭でシーンが切り替わります
     */
    void ChangeScene(const std::string& sceneName, float fadeOut = kDefaultFadeSeconds, float fadeIn = kDefaultFadeSeconds);

    /**
     * @brief シーン生成用工場をセットする
     * @param factory AbstractSceneFactoryを継承した具体的な工場のポインタ
     * @note シーン名から実際のクラスを生成するために必要です
     */
    void SetSceneFactory(AbstractSceneFactory* factory) {
        sceneFactory_ = factory;
    }

    /**
     * @brief 現在のシーンがポストエフェクト（グレースケール等）のオフスクリーンRTVリダイレクトに対応しているか
     * @note 未対応シーン中は MyGame.cpp 側で BeginScene/EndScene/Apply を呼ばないようにするためのガード
     */
    bool CurrentScenePostEffectsSupported() const {
        return currentScene_ && currentScene_->SupportsPostEffects();
    }

    /** @brief 今の画面の名前を返す（"TITLE" "MAP" "GAMEPLAY" など） */
    const std::string& GetCurrentSceneName() const {
        return currentSceneName_;
    }
    /** @brief 起動してから画面が切り替わった回数を返す（同じ名前の画面へ入り直したことも分かるように） */
    uint32_t GetSceneSwitchCount() const {
        return sceneSwitchCount_;
    }
    /** @brief 画面の切り替えの途中（フェードや読み込みの間）か */
    bool IsChangingScene() const {
        return transition_.IsChanging();
    }

private:
    /** @brief シーンが未登録の管理器を作る */
    SceneManager() = default;
    /** @brief 所有する資源はメンバのデストラクタで解放する */
    ~SceneManager() = default;
    SceneManager(const SceneManager&) = delete;
    const SceneManager& operator=(const SceneManager&) = delete;

    /** @brief フェードアウト完了後のシーン切替本体（Update()の冒頭から呼ばれる） */
    void PerformSceneSwitch();
    /** @brief cookモードで、全シーンを一度ずつ初期化・破棄して使う素材をpakへ記録させる（起動シーンを作る前に呼ぶ） */
    void CookAllScenes();
    void PrepareEditorPreview();
    EditorGamePreview editorPreview_;

    // 外部から提供される基盤システム
    DirectXCommon* dxCommon_ = nullptr;
    Input* input_ = nullptr;
    Audio* audio_ = nullptr;
    ImGuiManager* imguiManager_ = nullptr;

    // シーン管理メンバ

    /** @brief 現在アクティブなシーン */
    std::unique_ptr<BaseScene> currentScene_;

    /** @brief 次のフレームで切り替える予定のシーン */
    std::unique_ptr<BaseScene> nextScene_;

    /** @brief シーンを生成するための工場ポインタ（外部からセットされる） */
    AbstractSceneFactory* sceneFactory_ = nullptr;

    /** @brief フェード管理 **/
    SceneTransitionOverlay transition_;

    /** @brief 次に読み込むシーン名 **/
    std::string nextSceneName_;
    std::string currentSceneName_; ///< 今の画面の名前（GetCurrentSceneName 参照）
    uint32_t sceneSwitchCount_ = 0; ///< 画面が切り替わった回数
    std::string debugStartScene_ = "TITLE";
    std::vector<std::string> sceneHistory_; ///< これまでに開いたシーンの名前（古い順、エディタの戻るに使う）
    bool goingBack_ = false; ///< 戻る操作で切り替えている最中か（その時は履歴に積まない）

};

} // namespace engine::game

/** @file StageEditorTypes.h
 * @brief ステージエディタが外へ渡す型（戦う敵の参照・扉の条件の見張り・壊せる配置物の操作）を定義するファイル
 */
#pragma once
#include "LevelLoader.h"
#include "IPositionTarget.h"
#include <functional>
#include "Weapon.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine::graphics {
class Object3d;
}

namespace engine::game {
class EnemyEntity;
class UILayout;
namespace stageEditor {

/** @brief kind=="enemy_basic"かつ武器種別を持つ配置物1件ぶんの参照（GetCombatEnemies()の戻り値） */
struct CombatEnemyRef {
    /** @brief 生成された敵の一意な識別番号 */
    uint64_t instanceId = 0;
    std::string name;
    WeaponType weaponType;
    bool hasWeapon = true; ///< falseなら武器を持たないモンスター（倒しても奪う物が無く、消えて覚醒ゲージになる）
    bool isStageBoss = false;
    EnemyEntity* enemy = nullptr; // 非所有。StageEditorのobjects_が生存させる
};

/** @brief 敵グループの全滅を待つ条件が見ている敵1体の様子（自動テストプレイの見張り用） */
struct WatchedEnemyState {
    std::string name; ///< 配置物の名前
    Vector3 position; ///< 今の位置
    int hp = 0; ///< 残りの体力
    bool defeated = false; ///< 倒された扱いか
    bool visible = true; ///< 見えているか
    bool active = true; ///< 有効か（無効中の敵は攻撃が当たらないので、生きていると倒し切れない）
};

/** @brief 敵グループの全滅を待つ条件1つの様子（自動テストプレイの見張り用） */
struct EnemyGroupWatch {
    std::string conditionName; ///< 条件の配置物の名前
    std::string group; ///< 見ている敵グループ
    bool met = false; ///< 条件が成立しているか
    bool waitingSpawn = false; ///< まだ出ていない敵がいるか（出現待ちの間は成立しなくて当然）
    std::vector<WatchedEnemyState> enemies; ///< 出ている敵の様子
};

/** @brief 壊せる配置物のヒット判定と爆風に必要な読み取り専用の設定値 */
struct BreakableSettings {
    Vector3 scale;
    Vector4 color;
    float radius;
    int playerDamage;
    int enemyDamage;
    bool isWall; ///< 当たり判定ありで爆風なしの物（壊せる壁）。壊すと敵を巻き込みながら吹き飛ぶ
    std::string_view name;
};

/** @brief 壊せる配置物への操作。HPと破壊フラグの格納先は外部へ公開しない */
class BreakableRef {
public:
    /** @brief 配置物が所有する実体と状態に対する非所有の操作を構築する */
    BreakableRef(const ObjectDesc& desc, const Vector3& position, int& hp, bool& destroyed,
        engine::graphics::Object3d* object)
        : desc_(&desc), position_(position), hp_(&hp), destroyed_(&destroyed), object_(object) { }
    /** @brief 判定と爆風に必要な設定だけを値として返す */
    BreakableSettings Settings() const {
        return { desc_->scale, desc_->breakableColor, desc_->breakableRadius,
            desc_->breakablePlayerDamage, desc_->breakableEnemyDamage,
            desc_->solid && desc_->breakableRadius <= 0.0f, desc_->name };
    }
    /** @brief 親子階層を解決した現在位置を返す */
    Vector3 GetPosition() const {
        return position_;
    }
    /** @brief 射撃の的を識別する不透明なキーを返す */
    const void* TargetIdentity() const {
        return desc_;
    }
    /** @brief 外見の色を更新し描画行列へ反映する */
    void SetColor(const Vector4& color) const;
    /** @brief 1回の攻撃を受け、HPが尽きたか返す */
    bool TakeHit() const {
        return --*hp_ <= 0;
    }
    /** @brief 以降の描画と当たり判定から配置物を除く */
    void Destroy() const {
        *destroyed_ = true;
    }
private:
    const ObjectDesc* desc_;
    Vector3 position_;
    int* hp_;
    bool* destroyed_;
    engine::graphics::Object3d* object_;
};

/** @brief 外部実体の参照 */
struct ExternalEntityRef {
    std::string name; ///< 編集状態の値
    IPositionTarget* positionTarget = nullptr; ///< 編集状態の値
    std::function<int()> getVisualPreset; ///< 編集状態の値
    std::function<void(int)> setVisualPreset; ///< 編集状態の値
    std::function<void(const std::string&, const std::string&)> setStaticVisualModel; ///< (モデルパス, テクスチャパス)
    std::function<std::string()> getStaticVisualModel; ///< 編集状態の値
    std::function<std::string()> getStaticVisualTexture; ///< 編集状態の値
};

/** @brief 画面位置の記録 */
struct UIPositionRecord {
    UILayout* layout = nullptr; ///< 編集状態の値
    size_t index = 0; ///< 編集状態の値
    float x = 0.0f; ///< 編集状態の値
    float y = 0.0f; ///< 編集状態の値
};

/** @brief 型の点検で見つかった所の直し方 */
enum class GameModeFix {
    kNone, ///< 自動では直せない
    kResetMode, ///< 見つからない型をやめて横スクロールに戻す
    kPlaceKind, ///< 足りない種類の配置物を画面の中央に置く
    kWidenFloor, ///< 当たり判定のある物の奥行きを、動ける幅まで広げる
    kAddPathPoints, ///< 道の点を2つ以上にする
    kFixPathSpeed, ///< 速さが0の道の点を既定の速さにする
};

/** @brief 型の点検の1件（普通の言葉の文と、押すと直せる時の直し方） */
struct GameModeCheck {
    std::string text; ///< 何がおかしいか
    GameModeFix fix = GameModeFix::kNone; ///< 直し方
    std::string kind; ///< kPlaceKind で置く種類
    int index = -1; ///< kFixPathSpeed で直す点の番号
};

/** @brief レベルの編集状態 */
struct LevelSnapshot {
    std::vector<ObjectDesc> objects; ///< 編集状態の値
    std::vector<TriggerDesc> triggers; ///< 編集状態の値
    std::vector<CheckpointDesc> checkpoints; ///< 編集状態の値
    Vector3 playerSpawn; ///< 編集状態の値
    Vector3 enemySpawn; ///< 編集状態の値
    std::string graphPath; ///< 編集状態の値
    std::string sky; ///< 編集状態の値
    std::string gameMode; ///< ゲームの型の id
    std::vector<PathPointDesc> path; ///< 道に沿って進む型の道
    std::string comment; ///< 編集状態の値
    std::vector<FlagGraphBinding> flagGraphs; ///< 編集状態の値
    std::vector<UIPositionRecord> uiPositions; ///< 編集状態の値
};

/** @brief 素材の表示情報 */
struct DiscoveredAsset {
    std::string label; ///< 編集状態の値
    std::string model; ///< 編集状態の値
    std::string texture; ///< 編集状態の値
};

/** @brief ドラッグ開始時の変形 */
struct DragStartTransform {
    int index = -1; ///< 編集状態の値
    Vector3 world = { }; ///< 編集状態の値
    float localZ = 0.0f; ///< 編集状態の値
    Vector3 rotation = { }; ///< 編集状態の値
    Vector3 scale = { }; ///< 編集状態の値
    bool followsMove = true; ///< falseなら自分では動かない（親と一緒に動く子、画面座標の文章UI）
};

/** @brief ビューのドラッグ状態 */
struct ViewDragState {
    int activeDragAxis = 0; ///< ハンドルを掴んだドラッグ中の軸（0: 自由 / 1: X / 2: Y / 3: Z）。離すと0に戻る
    bool rotateDragging = false; ///< 回転ツールのドラッグ中か
    bool scaleDragging = false; ///< 拡縮ツールのドラッグ中か
    bool scaleUniform = false; ///< 拡縮ドラッグが全軸同時か
    bool boxSelecting = false; ///< 何も無い場所からの左ドラッグで範囲選択中か
    float boxStartX = 0.0f; ///< 編集状態の値
    float boxStartY = 0.0f; ///< 編集状態の値
    bool viewportDragging = false; ///< 3Dビュー上で選択物をドラッグ移動中か
    UILayout* uiDragLayout = nullptr; ///< ドラッグ中の画面UI位置項目（nullptrならUIを掴んでいない）
    size_t uiDragIndex = 0; ///< 編集状態の値
    float uiDragGrabOffsetX = 0.0f; ///< 掴んだ瞬間の(項目の位置 - マウスの画面座標)
    float uiDragGrabOffsetY = 0.0f; ///< 編集状態の値
    float dragRawZ = 0.0f; ///< Shift+ドラッグ(奥行き移動)中のスナップ前のZ累積値
    float dragGrabOffsetX = 0.0f; ///< 編集状態の値
    float dragGrabOffsetY = 0.0f; ///< 編集状態の値
    int gizmoAxis = 0; ///< 0は自由移動、1から3はX・Y・Z軸へ移動を制限する
    std::vector<DragStartTransform> dragStarts; ///< ドラッグ開始時の値（先頭が主選択）
    float dragStartMouseX = 0.0f; ///< 編集状態の値
    float dragStartMouseY = 0.0f; ///< 編集状態の値
    float dragLastMouseX = 0.0f; ///< 最後に分かったマウス位置（ウィンドウ外で座標が取れない時に使う）
    float dragLastMouseY = 0.0f; ///< 編集状態の値
    bool dragThresholdPassed = false; ///< 遊びの距離を超えて実際に動かし始めたか
    int clickPendingIndex = -1; ///< 選択中の物を押した時の番号。動かさず離したら選択を確定する
    bool clickPendingAdditive = false; ///< 押した時にCtrlを押していたか（動かさず離したら選択から外す）
    float rotateAccumRadians = 0.0f; ///< 回転ドラッグの開始からの累積角度
    float rotateLastAngle = 0.0f; ///< 前フレームの、ハンドル中心から見たマウスの角度
    bool rotateAngleValid = false; ///< rotateLastAngle が有効か
    bool hoverHandleActive = false; ///< マウスが変形ハンドルの上にあるか
    int hoverAxis = 0; ///< マウス直下のハンドルの軸（0: 中央 / 1: X / 2: Y / 3: Z）
    bool hoverNeedsRefresh = true; ///< マウスが止まっていても次のフレームで配置物を調べ直すか
    UILayout* uiHoverLayout = nullptr; ///< マウス直下の画面UIの印（nullptrなら無し）
    size_t uiHoverIndex = 0; ///< 編集状態の値
    float uiDragStartX = 0.0f; ///< ドラッグ開始時の印の座標
    float uiDragStartY = 0.0f; ///< 編集状態の値
};

 } // namespace stageEditor

using CombatEnemyRef = stageEditor::CombatEnemyRef;
using WatchedEnemyState = stageEditor::WatchedEnemyState;
using EnemyGroupWatch = stageEditor::EnemyGroupWatch;
using BreakableSettings = stageEditor::BreakableSettings;
using BreakableRef = stageEditor::BreakableRef;
using GameModeFix = stageEditor::GameModeFix;
using GameModeCheck = stageEditor::GameModeCheck;

} // namespace engine::game

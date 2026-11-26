/**
 * @file GamePlaySceneGameMode.cpp
 * @brief ゲームの型に合わせたカメラの付き方と、道に沿って自動で進む処理
 * @note 分割ファイル。クラスは GamePlayScene のまま。横スクロールの型では何もせず、今までの処理がそのまま動く
 */
#include "GamePlayScene.h"
#include "CombatDepth.h"
#include "EnemyEntity.h"
#include "GameActions.h"
#include "GameFlags.h"
#include "GameModeLibrary.h"
#include "StageEditor.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <string>

namespace engine::game {
namespace {
/** @brief 奥へ進む型で、道が無い時に進む向き（ステージの奥行きではなく +X へ進む） */
constexpr Vector3 kStageForward = { 1.0f, 0.0f, 0.0f };

/** @brief 道の点の並びが同じか（位置と速さがすべて同じなら同じ） */
bool SamePath(const std::vector<PathPointDesc>& a, const std::vector<PathPointDesc>& b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](const PathPointDesc& left, const PathPointDesc& right) {
        return left.position.x == right.position.x && left.position.y == right.position.y
            && left.position.z == right.position.z && left.speed == right.speed;
    });
}
} // namespace

/** @brief レベルの道が変わっていたら作り直す */
void GamePlayScene::RebuildPathIfChanged(const std::vector<PathPointDesc>& points) {
    if (pathTrack_.IsValid() && SamePath(points, builtPath_)) {
        return;
    }
    builtPath_ = points;
    std::vector<PathTrack::Point> trackPoints;
    trackPoints.reserve(points.size());
    for (const PathPointDesc& point : points) {
        trackPoints.push_back({ point.position, point.speed });
    }
    pathTrack_.Build(trackPoints);
}

/** @brief 道に沿って進む型なら、道の上の今の位置を進め、プレイヤーをその周りに収める */
void GamePlayScene::UpdatePathScroll(float dt) {
    StageEditor& editor = GetStageEditor();
    const GameModeDesc& mode = GameModeLibrary::GetInstance()->Find(editor.GetGameMode());
    if (!mode.path.follow) {
        return;
    }
    RebuildPathIfChanged(editor.GetPath());
    if (!pathTrack_.IsValid()) {
        return;
    }
    // レベルが変わった時と、エディタでテストを始めた時は、プレイヤーに一番近い所から進め直す
    const bool playTest = editor.IsPlayTestMode();
    if (editor.GetLevelPath() != pathLevel_ || (playTest && !lastPlayTest_)) {
        pathLevel_ = editor.GetLevelPath();
        pathDistance_ = pathTrack_.NearestDistance(player_->GetPosition());
        pathEndRaised_ = false;
    }
    lastPlayTest_ = playTest;

    // 道の速さで進める（ヒットストップ中は dt が 0 なので止まる）
    pathDistance_ = (std::min)(pathDistance_ + pathTrack_.SpeedAt(pathDistance_) * dt, pathTrack_.TotalLength());
    const Vector3 anchor = pathTrack_.PositionAt(pathDistance_);

    // プレイヤーを、道の上の今の位置から決まった幅の中に収める（横は両側、縦は上だけ。足元は床に任せる）
    Vector3 position = player_->GetPosition();
    const Vector3 before = position;
    position.x = std::clamp(position.x, anchor.x - mode.path.playerHalfWidth, anchor.x + mode.path.playerHalfWidth);
    position.y = (std::min)(position.y, anchor.y + mode.path.playerHalfHeight);
    if (position.x != before.x || position.y != before.y) {
        player_->SetPosition(position);
    }

    // 道の終わりに着いたら、ノードグラフで受け取れるフラグを1回だけ立てる
    if (!pathEndRaised_ && pathDistance_ >= pathTrack_.TotalLength()) {
        pathEndRaised_ = true;
        if (!mode.path.endFlag.empty()) {
            GameFlags::GetInstance()->SetFlag(mode.path.endFlag, true);
        }
    }
}

/** @brief 前後左右のキーを横スクロールの操作へ置き換える（前後は先へ進む・下がる、左右は奥行き） */
void GamePlayScene::PrepareDepthInput() {
    depthSide_ = 0.0f;
    depthForward_ = 0.0f;
    const GameModeDesc& mode = GameModeLibrary::GetInstance()->Find(GetStageEditor().GetGameMode());
    // 奥行きにも動く型の時だけ、当たりの箱の奥行きを位置の前後に合わせる（横スクロールでは今までどおり）
    combatDepth::SetBand(mode.player.depthMovement ? mode.player.hitHalfDepth : 0.0f);
    if (!mode.player.depthMovement) {
        return;
    }
    // 置き換える前に、闘技場と同じ前後左右とジャンプのキーを読む
    depthForward_ = (input_->PushAction(GameAction::kArenaForward) ? 1.0f : 0.0f)
        - (input_->PushAction(GameAction::kArenaBack) ? 1.0f : 0.0f);
    depthSide_ = (input_->PushAction(GameAction::kArenaRight) ? 1.0f : 0.0f)
        - (input_->PushAction(GameAction::kArenaLeft) ? 1.0f : 0.0f);
    input_->SetActionOverride(GameAction::kMoveRight, depthForward_ > 0.0f);
    input_->SetActionOverride(GameAction::kMoveLeft, depthForward_ < 0.0f);
    input_->SetActionOverride(GameAction::kJump, input_->PushAction(GameAction::kArenaJump));
    input_->SetActionOverride(GameAction::kDown, false);
    depthOverrideActive_ = true;
}

/** @brief 左右のキーの分だけ奥行きへ動かし、敵を同じ奥行きへ寄せてから、操作の置き換えを戻す */
void GamePlayScene::ApplyDepthMovement(float dt) {
    if (!depthOverrideActive_) {
        return;
    }
    // 置き換えは同じフレームのうちに戻す（ポーズのメニューなどへ持ち越さない）
    input_->ClearActionOverrides();
    depthOverrideActive_ = false;
    const GameModeDesc& mode = GameModeLibrary::GetInstance()->Find(GetStageEditor().GetGameMode());
    const GameModePlayerRules& rules = mode.player;

    // カメラから見た右へ動く（進む向きが +X なら右は -Z。道があれば道の向きから求める）
    const bool pathActive = mode.path.follow && pathTrack_.IsValid();
    const Vector3 forward = pathActive ? pathTrack_.DirectionAt(pathDistance_) : kStageForward;
    const Vector3 right = { forward.z, 0.0f, -forward.x };
    Vector3 position = player_->GetPosition();
    if (depthSide_ != 0.0f) {
        const float step = depthSide_ * rules.depthSpeed * dt;
        position.x += right.x * step;
        position.z = std::clamp(position.z + right.z * step, rules.depthMin, rules.depthMax);
        player_->SetPosition(position);
    }
    // 横だけに動いている間は、向きを変えずに横歩きにする
    player_->SetStrafeMoving(depthSide_ != 0.0f && depthForward_ == 0.0f);

    // 敵はプレイヤーと同じ奥行きへ少しずつ寄ってくる（当たりは今までどおり横と縦で決まるので、並んで戦える）
    if (!rules.enemiesFollowDepth) {
        return;
    }
    const float maxStep = rules.enemyDepthSpeed * dt;
    for (const CombatEnemyRef& ref : GetStageEditor().GetCombatEnemies()) {
        if (ref.enemy == nullptr || ref.enemy->IsDefeated()) {
            continue;
        }
        Vector3 enemyPosition = ref.enemy->GetPosition();
        const float delta = std::clamp(position.z - enemyPosition.z, -maxStep, maxStep);
        if (std::abs(delta) > 0.0f) {
            enemyPosition.z += delta;
            ref.enemy->SetPosition(enemyPosition);
        }
    }
}

/** @brief ゲームの型に操作の表があれば、その型を初めて遊ぶ時に操作説明の板で出す準備をする */
void GamePlayScene::ArmModeControlsGuide() {
    const GameModeDesc& mode = GameModeLibrary::GetInstance()->Find(GetStageEditor().GetGameMode());
    // アプリを起動している間に、型ごとに一度だけ出す
    static std::set<std::string> sShownModes;
    if (mode.controls.empty() || sShownModes.contains(mode.id)) {
        return;
    }
    sShownModes.insert(mode.id);
    std::string lines;
    for (const GameModeControl& control : mode.controls) {
        lines += control.keys + "  " + control.label + "\n";
    }
    stageGuide_.ArmCustom(mode.name + " の操作", lines);
}

/** @brief ノードからのゲームオーバーの頼みを受け取り、やられ演出を始める */
bool GamePlayScene::UpdateRuleRequests() {
    GameFlags* flags = GameFlags::GetInstance();
    if (!flags->GetFlag(GameModeLibrary::kGameOverRequestFlag)) {
        return false;
    }
    // 頼みは1回で消す。クリアの演出ややられ演出の最中なら受け取らない
    flags->SetFlag(GameModeLibrary::kGameOverRequestFlag, false);
    if (clearTriggered_ || playerDefeat_.IsActive()) {
        return false;
    }
    StartPlayerDefeat();
    return true;
}

/** @brief ゲームの型が横スクロール以外のカメラを使うなら、その部品でカメラを置く */
bool GamePlayScene::UpdateModeCamera() {
    const GameModeDesc& mode = GameModeLibrary::GetInstance()->Find(GetStageEditor().GetGameMode());
    const bool pathActive = mode.path.follow && pathTrack_.IsValid();
    // 横からでステージの端で止める型と、演出がカメラを決めている間は、今までのカメラに任せる
    const bool defaultCamera = mode.cameraType == "side" && mode.useStageBoundsCamera && !pathActive;
    const bool cinematic = stageIntro_.IsPlaying() || playerDefeat_.IsActive() || bossExecution_.IsActive();
    if (defaultCamera || cinematic) {
        // 型のカメラから戻ったら、型のカメラを使う前の向きに戻す
        if (modeCameraActive_) {
            cameraTargetRot_ = savedCameraRot_;
            modeCameraActive_ = false;
        }
        return false;
    }
    if (!modeCameraActive_) {
        savedCameraRot_ = cameraTargetRot_;
        modeCameraActive_ = true;
    }
    // 種類が変わった時だけ部品を作り直す
    if (!cameraRig_ || cameraRigType_ != mode.cameraType) {
        cameraRig_ = graphics::CreateCameraRig(mode.cameraType);
        cameraRigType_ = mode.cameraType;
    }
    // 道があれば道の上の今の位置と向き、無ければプレイヤーとステージの進む向きを見る
    const Vector3 focus = pathActive ? pathTrack_.PositionAt(pathDistance_) : player_->GetPosition();
    const Vector3 forward = pathActive ? pathTrack_.DirectionAt(pathDistance_) : kStageForward;
    const graphics::CameraShotPose pose = cameraRig_->Evaluate(focus, forward, mode.camera);
    cameraTargetPos_ = pose.eye;
    cameraTargetRot_ = graphics::LookAtRotation(pose.eye, pose.target);
    UpdateCameraSmoothing();
    return true;
}

} // namespace engine::game

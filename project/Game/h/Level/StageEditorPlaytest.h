/**
 * @file StageEditorPlaytest.h
 * @brief ステージエディタのテストプレイの記録（被弾・やられた位置と区間ごとにいた時間）を宣言するファイル
 */
#pragma once
#ifdef USE_IMGUI
#include "Vector3.h"

namespace engine::game {
class StageEditor;
namespace stagePlaytest {
/** @brief 現在のレベルで一回分の記録を開始する */
void Begin(const StageEditor& editor);
/** @brief 指定のエディタで進行中の記録を保存する */
void End(const StageEditor& editor);
/** @brief プレイヤーがいる区間に経過時間を足す */
void Tick(const StageEditor& editor, const Vector3& position, float seconds);
/** @brief 攻撃を受けた位置を記録する */
void Hit(const StageEditor& editor, const Vector3& position);
/** @brief 体力と敗北状態の変化を記録する */
void ObservePlayer(const StageEditor& editor, const Vector3& position, bool defeated);
/** @brief 記録の表示設定と削除の確認、記録から分かる直す所の気づきを描く（見るボタンでカメラを寄せる） */
void Controls(StageEditor& editor);
/** @brief 中央ビューに滞在時間と被弾地点を描く */
void Draw(const StageEditor& editor, bool hovered);
}
}
#endif

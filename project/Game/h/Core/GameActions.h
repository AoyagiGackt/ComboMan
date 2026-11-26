/** @file GameActions.h
 * @brief 操作の定義と共通の入力判定
 */
#pragma once

#include "Input.h"

namespace engine::game {

/** @brief ゲーム内の操作を識別する番号 */
enum class GameAction {
    kMoveLeft,
    kMoveRight,
    kJump,
    kDown,
    kAttack,
    kShoot,
    kSkill,
    kAwaken,
    kFinisher,
    kGunSwitch,
    kDodge,
    kWarp,
    kSteal,
    kInteract,
    kLockOn,
    kPause,
    kArenaForward, ///< 闘技場で奥（カメラの向き）へ進む
    kArenaBack, ///< 闘技場で手前へ下がる
    kArenaLeft, ///< 闘技場で左へ回り込む
    kArenaRight, ///< 闘技場で右へ回り込む
    kArenaJump, ///< 闘技場でのジャンプ（前進と同じキーにしない）
    kThrowWeapon,
    kCount
};

/** @brief 操作番号順の既定割り当てを返す */
const std::vector<Input::ActionDef>& GameActionTable();

/** @brief 移動操作をまとめた案内の表記を返す */
std::wstring ResolveMovementPrompt(const Input& input, const std::wstring& name);

/** @brief 決定・攻撃・ジャンプで演出を飛ばすかを返す */
bool TriggerCinematicSkip(const Input& input);

} // namespace engine::game

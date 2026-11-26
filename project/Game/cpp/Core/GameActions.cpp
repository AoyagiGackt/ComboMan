/** @file GameActions.cpp
 * @brief 操作の既定割り当てと共通の入力判定の実装
 */
#include "GameActions.h"
#include <iterator>

namespace engine::game {
namespace {
using StickDirection = Input::StickDirection;
constexpr BYTE kNoKey = 0;
constexpr WORD kNoButton = 0;
constexpr float kStickDownThreshold = 0.6f;
constexpr Input::ActionDef kActionDefs[] = {
    { "MoveLeft", DIK_A, DIK_LEFT, kNoButton, StickDirection::kLeft, Input::kUseDeadzone },
    { "MoveRight", DIK_D, DIK_RIGHT, kNoButton, StickDirection::kRight, Input::kUseDeadzone },
    { "Jump", DIK_W, DIK_UP, XINPUT_GAMEPAD_A, StickDirection::kNone, Input::kUseDeadzone },
    { "Down", DIK_S, DIK_DOWN, kNoButton, StickDirection::kDown, kStickDownThreshold },
    { "Attack", DIK_L, kNoKey, XINPUT_GAMEPAD_X, StickDirection::kNone, Input::kUseDeadzone },
    { "Shoot", DIK_K, kNoKey, XINPUT_GAMEPAD_Y, StickDirection::kNone, Input::kUseDeadzone },
    { "Skill", DIK_SPACE, kNoKey, XINPUT_GAMEPAD_RIGHT_SHOULDER, StickDirection::kNone, Input::kUseDeadzone },
    { "Awaken", DIK_R, kNoKey, XINPUT_GAMEPAD_LEFT_SHOULDER, StickDirection::kNone, Input::kUseDeadzone },
    { "Finisher", DIK_F, kNoKey, XINPUT_GAMEPAD_LEFT_SHOULDER, StickDirection::kNone, Input::kUseDeadzone },
    { "GunSwitch", DIK_G, kNoKey, XINPUT_GAMEPAD_DPAD_LEFT, StickDirection::kNone, Input::kUseDeadzone },
    { "Dodge", DIK_I, kNoKey, XINPUT_GAMEPAD_B, StickDirection::kNone, Input::kUseDeadzone },
    { "Warp", DIK_C, kNoKey, kNoButton, StickDirection::kNone, Input::kUseDeadzone },
    { "Steal", DIK_J, kNoKey, Input::kGamepadLeftTrigger, StickDirection::kNone, Input::kUseDeadzone },
    { "Interact", DIK_RETURN, kNoKey, Input::kGamepadLeftTrigger, StickDirection::kNone, Input::kUseDeadzone },
    { "LockOn", DIK_RSHIFT, kNoKey, Input::kGamepadRightTrigger, StickDirection::kNone, Input::kUseDeadzone },
    { "Pause", DIK_P, kNoKey, XINPUT_GAMEPAD_START, StickDirection::kNone, Input::kUseDeadzone },
    { "ArenaForward", DIK_W, DIK_UP, kNoButton, StickDirection::kUp, Input::kUseDeadzone },
    { "ArenaBack", DIK_S, DIK_DOWN, kNoButton, StickDirection::kDown, Input::kUseDeadzone },
    { "ArenaLeft", DIK_A, DIK_LEFT, kNoButton, StickDirection::kLeft, Input::kUseDeadzone },
    { "ArenaRight", DIK_D, DIK_RIGHT, kNoButton, StickDirection::kRight, Input::kUseDeadzone },
    { "ArenaJump", DIK_U, kNoKey, XINPUT_GAMEPAD_A, StickDirection::kNone, Input::kUseDeadzone },
    { "ThrowWeapon", DIK_LSHIFT, kNoKey, kNoButton, StickDirection::kNone, Input::kUseDeadzone },
};
static_assert(std::size(kActionDefs) == static_cast<size_t>(GameAction::kCount));
} // namespace

/** @brief 操作番号順の既定割り当てを返す */
const std::vector<Input::ActionDef>& GameActionTable() {
    static const std::vector<Input::ActionDef> kTable(std::begin(kActionDefs), std::end(kActionDefs));
    return kTable;
}

/** @brief 移動操作をまとめた案内の表記を返す */
std::wstring ResolveMovementPrompt(const Input& input, const std::wstring& name) {
    if (name == L"Move") {
        return input.IsUsingGamepad() ? L"左スティック"
            : input.GetActionLabel(GameAction::kMoveLeft) + L" " + input.GetActionLabel(GameAction::kMoveRight);
    }
    if (name == L"ArenaMove") {
        return input.IsUsingGamepad() ? L"左スティック"
            : input.GetActionLabel(GameAction::kArenaForward) + input.GetActionLabel(GameAction::kArenaLeft)
                + input.GetActionLabel(GameAction::kArenaBack) + input.GetActionLabel(GameAction::kArenaRight);
    }
    return {};
}

/** @brief 決定・攻撃・ジャンプで演出を飛ばすかを返す */
bool TriggerCinematicSkip(const Input& input) {
    return input.TriggerMenuConfirm() || input.TriggerAction(GameAction::kAttack) || input.TriggerAction(GameAction::kJump);
}

} // namespace engine::game

/**
 * @file ScriptEffectReceiver.cpp
 * @brief ノードグラフから頼まれた画面の演出の受け口を実装するファイル
 */
#include "ScriptEffectReceiver.h"
#include "GameActions.h"
#include "FontRenderer.h"
#include "GameConstants.h"
#include "Input.h"
#include "SceneEffectBridge.h"
#include "StringUtility.h"
#include "UILayout.h"
#include "Vector4.h"
#include <algorithm>

namespace engine::game {

namespace {
// ShowMessage で出す文字（画面の下寄り中央。最後の時間で薄く消える）
constexpr float kMessageY = 560.0f;
constexpr float kMessageScale = 1.6f;
constexpr Vector4 kMessageColor = { 1.0f, 1.0f, 1.0f, 1.0f };
constexpr Vector4 kMessageShadowColor = { 0.0f, 0.0f, 0.0f, 0.85f };
constexpr float kMessageShadowOffset = 2.0f;
constexpr float kMessageFadeSeconds = 0.3f;
constexpr float kCenterFactor = 0.5f; // 文字を左右の中央にそろえる時に、幅へ掛ける割合
} // namespace

/** @brief SceneEffectBridge へ文字・カメラ・操作停止の受け口を登録する */
void ScriptEffectReceiver::Register(Input* input) {
    input_ = input;
    auto* bridge = SceneEffectBridge::GetInstance();
    bridge->SetMessageHandler([this](const std::string& text, float seconds) {
        message_ = StringUtility::ConvertString(text);
        messageSeconds_ = seconds;
    });
    bridge->SetCameraFocusHandler([this](float x, float y, float seconds) {
        cameraFocus_ = { x, y, 0.0f };
        cameraFocusSeconds_ = seconds;
    });
    bridge->SetControlLockHandler([this](bool locked) {
        // 戻した時は上書きを外して、実機の入力をそのまま使う
        if (controlLocked_ && !locked && input_ != nullptr) {
            input_->ClearActionOverrides();
        }
        controlLocked_ = locked;
    });
}

/** @brief 受け口を外し、操作を止めたままなら戻す */
void ScriptEffectReceiver::Unregister() {
    auto* bridge = SceneEffectBridge::GetInstance();
    bridge->SetMessageHandler({ });
    bridge->SetCameraFocusHandler({ });
    bridge->SetControlLockHandler({ });
    // 操作を止めたままシーンを抜けても、次の画面に上書きを残さない
    if (controlLocked_ && input_ != nullptr) {
        input_->ClearActionOverrides();
    }
    controlLocked_ = false;
}

/** @brief 操作を止めている間は、ポーズ以外の操作を押していない扱いにする */
void ScriptEffectReceiver::ApplyControlLock() {
    if (!controlLocked_ || input_ == nullptr) {
        return;
    }
    for (int i = 0; i < static_cast<int>(GameAction::kCount); ++i) {
        const auto action = static_cast<GameAction>(i);
        if (action != GameAction::kPause) {
            input_->SetActionOverride(action, false);
        }
    }
}

/** @brief カメラを向ける先があれば取り出し、残り時間を1フレームぶん減らす */
bool ScriptEffectReceiver::TakeCameraFocus(Vector3& focus) {
    if (cameraFocusSeconds_ <= 0.0f) {
        return false;
    }
    cameraFocusSeconds_ -= gameConstants::kFrameDeltaTime;
    focus = cameraFocus_;
    return true;
}

/** @brief 出している文字を画面中央の下寄りに描き、残り時間を減らす */
void ScriptEffectReceiver::DrawMessage(FontRenderer& font, const char* layoutName) {
    if (messageSeconds_ <= 0.0f || message_.empty()) {
        return;
    }
    messageSeconds_ -= gameConstants::kFrameDeltaTime;
    UILayout& layout = UILayout::Get(layoutName);
    const float y = layout.Float("script_message.y", kMessageY);
    const float scale = layout.Float("script_message.scale", kMessageScale);
    // 最後の少しの間で薄くして消す
    const float alpha = std::clamp(messageSeconds_ / kMessageFadeSeconds, 0.0f, 1.0f);
    Vector4 shadow = kMessageShadowColor;
    shadow.w *= alpha;
    Vector4 color = layout.Color("script_message.color", kMessageColor);
    color.w *= alpha;
    // 影は少し右下にずらすため、中央揃えの位置を同じだけずらした文字で描く
    const float width = FontRenderer::MeasureStringW(message_, scale).x;
    const float x = gameConstants::kScreenCenterX - width * kCenterFactor;
    font.DrawStringW(message_, x + kMessageShadowOffset, y + kMessageShadowOffset, scale, shadow);
    font.DrawStringW(message_, x, y, scale, color);
}

} // namespace engine::game

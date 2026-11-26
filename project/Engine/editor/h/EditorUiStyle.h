/**
 * @file EditorUiStyle.h
 * @brief エディタの画面で共通に使う色・パネルの設定・ボタンの幅を定義するファイル
 */
#pragma once
#ifdef USE_IMGUI
#include <imgui.h>

namespace engine::editor::uiStyle {

// メッセージの色
inline constexpr ImVec4 kOkColor = { 0.4f, 1.0f, 0.5f, 1.0f };
inline constexpr ImVec4 kWarningColor = { 1.0f, 0.85f, 0.4f, 1.0f };
inline constexpr ImVec4 kNoticeColor = { 1.0f, 0.85f, 0.2f, 1.0f };
inline constexpr ImVec4 kChangedColor = { 1.0f, 0.85f, 0.3f, 1.0f };
inline constexpr ImVec4 kErrorColor = { 1.0f, 0.4f, 0.4f, 1.0f };
inline constexpr ImVec4 kCautionColor = { 1.0f, 0.7f, 0.3f, 1.0f };
inline constexpr ImVec4 kHeadingColor = { 0.6f, 0.85f, 1.0f, 1.0f };
inline constexpr ImVec4 kMissingSourceColor = { 1.0f, 0.55f, 0.3f, 1.0f };
inline constexpr ImVec4 kFlagOnColor = { 0.5f, 1.0f, 0.6f, 1.0f };
inline constexpr ImVec4 kFlagOffColor = { 0.6f, 0.6f, 0.6f, 1.0f };

// 画面に固定するパネルの共通設定（動かせず、クリックしても後から開いた補助パネルより前に出ない）
inline constexpr ImGuiWindowFlags kFixedPanelFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
    | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

// 標準のボタン幅
inline constexpr float kWideButtonWidth = 140.0f;
inline constexpr float kButtonWidth = 120.0f;

} // namespace engine::editor::uiStyle
#endif
